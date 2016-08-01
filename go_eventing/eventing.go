package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	_ "net/http/pprof"
	"os"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/abhi-bit/eventing/worker"
	"github.com/couchbase/cbauth"
	"github.com/couchbase/go-couchbase"
	"github.com/couchbase/indexing/secondary/logging"
)

var appSetup chan string
var appServerWG sync.WaitGroup

type v8handleBucketConfig struct {
	bucket *couchbase.Bucket
	handle *worker.Worker
}

func argParse() {

	flag.IntVar(&options.maxVbno, "maxvb", 1024,
		"maximum number of vbuckets")
	flag.IntVar(&options.stats, "stats", 1000,
		"periodic timeout in mS, to print statistics, `0` will disable stats")
	flag.BoolVar(&options.printflogs, "flogs", false,
		"display failover logs")
	flag.StringVar(&options.auth, "auth", "",
		"Auth user and password")
	flag.BoolVar(&options.info, "info", false,
		"display informational logs")
	flag.BoolVar(&options.debug, "debug", false,
		"display debug logs")
	flag.BoolVar(&options.trace, "trace", false,
		"display trace logs")

	flag.Parse()

	if options.debug {
		logging.SetLogLevel(logging.Debug)
	} else if options.trace {
		logging.SetLogLevel(logging.Trace)
	} else {
		logging.SetLogLevel(logging.Info)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, "Usage : %s [OPTIONS] <cluster-addr> \n", os.Args[0])
	flag.PrintDefaults()
}

func processApp(appName string) {
	logging.Infof("Reading config for app: %s \n", appName)

	appData, err := ioutil.ReadFile("./apps/" + appName)
	if err == nil {
		var app application
		err := json.Unmarshal(appData, &app)
		if err != nil {
			logging.Infof("Failed parse application data for app: %s\n", appName)
		}

		config := app.DeploymentConfig.(map[string]interface{})
		httpConfigs := config["http"].([]interface{})

		appServerWG.Add(len(httpConfigs))

		for appIndex := 0; appIndex < len(httpConfigs); appIndex++ {
			httpConfig := httpConfigs[appIndex].(map[string]interface{})

			serverURIRegexp := fmt.Sprintf("%s*", httpConfig["root_uri_path"].(string))
			serverPortCombo := fmt.Sprintf("localhost:%s", httpConfig["port"].(string))

			go func() {
				defer appServerWG.Done()
				regexpHandler := &RegexpHandler{}
				regexpHandler.HandleFunc(regexp.MustCompile(serverURIRegexp), handleJsRequests)

				log.Printf("Started listening on server_port_combo: %s on endpoint: %s\n",
					serverPortCombo, serverURIRegexp)
				log.Fatal(http.ListenAndServe(serverPortCombo, regexpHandler))
			}()
		}
	}
}

func getSource(appName string) (string, string, string) {
	logging.Infof("Reading config for app: %s \n", appName)

	appData, err := ioutil.ReadFile("./apps/" + appName)
	if err == nil {
		var app application
		err := json.Unmarshal(appData, &app)
		if err != nil {
			logging.Infof("Failed parse application data for app: %s\n", appName)
		}

		config := app.DeploymentConfig.(map[string]interface{})
		workspace := config["workspace"].(map[string]interface{})

		srcBucket := workspace["source_bucket"].(string)
		metaBucket := workspace["metadata_bucket"].(string)
		srcEndpoint := workspace["source_endpoint"].(string)
		return srcBucket, metaBucket, srcEndpoint
	}
	return "", "", ""
}

func setUpEventingApp(appName string) {
	srcBucket, metaBucket, srcEndpoint := getSource(appName)
	// TODO: return if any of the above fields are missing
	log.Printf("srcBucket: %s metadata bucket: %s srcEndpoint: %s\n",
		srcBucket, metaBucket, srcEndpoint)

	connStr := "http://" + srcEndpoint + ":8091"

	c, err := couchbase.Connect(connStr)
	pool, err := c.GetPool("default")
	bucket, err := pool.GetBucket(metaBucket)

	var sleep time.Duration
	sleep = 1
	for err != nil {
		logging.Infof("Bucket: %s missing, retrying after %d seconds, err: %#v\n",
			metaBucket, sleep, err)
		time.Sleep(time.Second * sleep)
		// TODO: more error logging
		c, _ = couchbase.Connect(connStr)
		pool, _ = c.GetPool("default")
		bucket, err = pool.GetBucket(metaBucket)
		if sleep < 8 {
			sleep = sleep * 2
		}
	}

	cluster := srcEndpoint + ":8091"
	logging.Infof("clutser: %s auth: %#v\n", cluster, options.auth)
	if options.auth != "" {
		up := strings.Split(options.auth, ":")
		if _, err := cbauth.InternalRetryDefaultInit(cluster, up[0], up[1]); err != nil {
			logging.Fatalf("Failed to initialize cbauth: %s", err)
		}
	}

	kvaddr := srcEndpoint + ":11210"
	kvaddrs := []string{kvaddr}

	// Mutation channel for source bucket
	rch := make(chan []interface{}, 10000)

	// TODO: Figure right way to terminate this goroutine on app undeploy
	fmt.Printf("Starting up bucket dcp feed for appName: %s with bucket: %s\n",
		appName, srcBucket)
	go startBucket(cluster, srcBucket, kvaddrs, rch)

	var tick <-chan time.Time

	if options.stats > 0 {
		tick = time.Tick(time.Millisecond * time.Duration(options.stats))
	}

	handle := loadApp(appName)

	config := v8handleBucketConfig{
		bucket: bucket,
		handle: handle,
	}

	timerEventWorkerChannel <- config

	go runWorker(rch, tick, appName, handle, bucket)
}

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	workerChannel = make(chan *worker.Worker, 100)
	timerEventWorkerChannel = make(chan v8handleBucketConfig, 100)

	argParse()
	files, _ := ioutil.ReadDir("./apps/")
	for _, file := range files {
		setUpEventingApp(file.Name())
	}

	go startTimerProcessing()

	go func() {
		fs := http.FileServer(http.Dir("../ui"))
		http.Handle("/", fs)
		http.HandleFunc("/get_application/", fetchAppSetup)
		http.HandleFunc("/set_application/", storeAppSetup)
		http.HandleFunc("/debug", v8DebugHandler)

		log.Fatal(http.ListenAndServe("localhost:6061", nil))
	}()

	files, err := ioutil.ReadDir("./apps/")
	if err != nil {
		logging.Infof("Failed to read application directory, is it missing?\n")
		os.Exit(1)
	}

	appSetup = make(chan string, 100)
	for _, file := range files {
		appSetup <- file.Name()
	}

	for {
		select {
		case appName := <-appSetup:
			go processApp(appName)
			logging.Infof("Got message to load app: %s", appName)
		}
	}
	appServerWG.Wait()
}
