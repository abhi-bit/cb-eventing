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

	"github.com/couchbase/cbauth"
	"github.com/couchbase/go-couchbase"
	"github.com/couchbase/indexing/secondary/logging"
)

var bucket *couchbase.Bucket
var appSetup chan string
var appServerWG sync.WaitGroup

func argParse() string {
	var buckets string
	var kvaddrs string

	flag.StringVar(&buckets, "buckets", "default",
		"buckets to listen")
	flag.StringVar(&kvaddrs, "kvaddrs", "",
		"list of kv-nodes to connect")
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

	options.buckets = strings.Split(buckets, ",")
	if options.debug {
		logging.SetLogLevel(logging.Debug)
	} else if options.trace {
		logging.SetLogLevel(logging.Trace)
	} else {
		logging.SetLogLevel(logging.Info)
	}
	if kvaddrs == "" {
		logging.Fatalf("please provided -kvaddrs")
	}
	options.kvaddrs = strings.Split(kvaddrs, ",")

	args := flag.Args()
	if len(args) < 1 {
		usage()
		os.Exit(1)
	}
	return args[0]
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

func main() {
	cluster := argParse()

	connStr := "http://donut:8091"
	c, err := couchbase.Connect(connStr)
	mf(err, "connect failure")

	pool, err := c.GetPool("default")
	mf(err, "pool")

	bucket, err = pool.GetBucket("eventing")

	var sleep time.Duration
	sleep = 1
	for err != nil {
		logging.Infof("Bucket eventing missing, retrying after %d seconds, err: %#v\n",
			sleep, err)
		time.Sleep(time.Second * sleep)
		c, _ = couchbase.Connect(connStr)
		pool, _ = c.GetPool("default")
		bucket, err = pool.GetBucket("eventing")
		if sleep < 8 {
			sleep = sleep * 2
		}
	}

	// setup cbauth
	if options.auth != "" {
		up := strings.Split(options.auth, ":")
		if _, err := cbauth.InternalRetryDefaultInit(cluster, up[0], up[1]); err != nil {
			logging.Fatalf("Failed to initialize cbauth: %s", err)
		}
	}

	for _, b := range options.buckets {
		go startBucket(cluster, b, options.kvaddrs)
	}

	if options.stats > 0 {
		tick = time.Tick(time.Millisecond * time.Duration(options.stats))
	}

	go func() {
		runWorker()
	}()

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
