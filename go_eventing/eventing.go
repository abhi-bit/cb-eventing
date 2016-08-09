package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net"
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
var workerWG sync.WaitGroup
var appHTTPservers map[string][]*HTTPServer

type staticAssetInfo struct {
	appName string
	bucket  *couchbase.Bucket
}

var uriPrefixAppMap map[string]*staticAssetInfo
var uriPrefixAppnameBackIndex map[string]string

type handleChans struct {
	dcpStreamClose  chan string
	timerEventClose chan bool
	rch             chan []interface{}
}

type v8handleBucketConfig struct {
	bucket *couchbase.Bucket
	handle *worker.Worker
	hChans *handleChans
}

var appDoneChans map[string]handleChans

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
	fmt.Fprintf(os.Stderr, "Usage : %s [OPTIONS] <cluster-addr> \n",
		os.Args[0])
	flag.PrintDefaults()
}

func performAppHTTPSetup(appName string) {
	logging.Infof("Reading config for app: %s \n", appName)

	appData, err := ioutil.ReadFile("./apps/" + appName)
	if err == nil {
		var app application
		err := json.Unmarshal(appData, &app)
		if err != nil {
			logging.Infof("Failed parse application data for app: %s",
				appName)
		}

		config := app.DeploymentConfig.(map[string]interface{})
		httpConfigs := config["http"].([]interface{})

		appServerWG.Add(len(httpConfigs))

		for appIndex := 0; appIndex < len(httpConfigs); appIndex++ {
			httpConfig := httpConfigs[appIndex].(map[string]interface{})

			// TODO: Flip to using to copy/append
			// benchmarks - https://play.golang.org/p/BPJzQHUbqq
			URIPath := httpConfig["root_uri_path"].(string)
			serverURIRegexp := fmt.Sprintf("%s*", URIPath)
			serverPortCombo := fmt.Sprintf("localhost:%s",
				httpConfig["port"].(string))
			staticAssetURIRegexp := fmt.Sprintf("%sstatic_assets/",
				URIPath)

			tcpListener, err := net.Listen("tcp", serverPortCombo)
			if err != nil {
				logging.Errorf("Regexp http server error: %s", err.Error())
			}
			httpServer := createHTTPServer(tcpListener)

			regexpHandler := &RegexpHandler{}
			// Handling static asset requests
			regexpHandler.HandleFunc(regexp.MustCompile(staticAssetURIRegexp),
				handleStaticAssets)

			regexpHandler.HandleFunc(regexp.MustCompile(serverURIRegexp),
				handleJsRequests)

			log.Printf("Started listening on server:port: %s on endpoint: %s\n",
				serverPortCombo, serverURIRegexp)

			tableLock.Lock()
			defer tableLock.Unlock()
			if _, ok := appHTTPservers[appName]; ok {
				appHTTPservers[appName] = append(
					appHTTPservers[appName],
					httpServer)
			} else {
				appHTTPservers = make(map[string][]*HTTPServer)
				appHTTPservers[appName] = make([]*HTTPServer, 1)
				appHTTPservers[appName][0] = httpServer
			}

			_, metaBucket, srcEndpoint := getSource(appName)
			connStr := "http://" + srcEndpoint + ":8091"

			c, err := couchbase.Connect(connStr)
			pool, err := c.GetPool("default")
			bucket, err := pool.GetBucket(metaBucket)

			info := &staticAssetInfo{
				appName: appName,
				bucket:  bucket,
			}

			path := strings.Split(URIPath, "/")[1]
			uriPrefixAppMap[path] = info
			uriPrefixAppnameBackIndex[appName] = path

			go func(httpServer *HTTPServer,
				regexpHandler *RegexpHandler) {
				defer appServerWG.Done()
				logging.Tracef("HTTPServer started up")
				http.Serve(httpServer, regexpHandler)
				logging.Infof("HTTPServer cleanly closed")
			}(httpServer, regexpHandler)
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
			logging.Infof("Failed parse application data for app: %s", appName)
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
	log.Printf("srcBucket: %s metadata bucket: %s srcEndpoint: %s",
		srcBucket, metaBucket, srcEndpoint)

	connStr := "http://" + srcEndpoint + ":8091"

	c, err := couchbase.Connect(connStr)
	pool, err := c.GetPool("default")
	bucket, err := pool.GetBucket(metaBucket)

	var sleep time.Duration
	sleep = 1
	for err != nil {
		logging.Infof("Bucket: %s missing, retrying after %d seconds, err: %#v",
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
	chans := handleChans{
		dcpStreamClose:  make(chan string, 1),
		timerEventClose: make(chan bool, 1),
		rch:             make(chan []interface{}, 10000),
	}

	tableLock.Lock()
	appDoneChans[appName] = chans
	tableLock.Unlock()

	fmt.Printf("Starting up bucket dcp feed for appName: %s with bucket: %s\n",
		appName, srcBucket)
	go startBucket(cluster, srcBucket, kvaddrs, chans)

	var ticker *time.Ticker

	if options.stats > 0 {
		ticker = time.NewTicker(time.Millisecond * time.Duration(options.stats))
	}

	handle := loadApp(appName)

	config := v8handleBucketConfig{
		bucket: bucket,
		handle: handle,
		hChans: &chans,
	}

	timerEventWorkerChannel <- config

	workerWG.Add(1)
	go runWorker(chans, ticker, appName, handle, bucket)
}

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	workerChannel = make(chan *worker.Worker, 100)
	timerEventWorkerChannel = make(chan v8handleBucketConfig, 100)

	appDoneChans = make(map[string]handleChans)
	uriPrefixAppMap = make(map[string]*staticAssetInfo)
	uriPrefixAppnameBackIndex = make(map[string]string)

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
			go performAppHTTPSetup(appName)
			logging.Infof("Got message to load app: %s", appName)
		}
	}
	appServerWG.Wait()
	workerWG.Wait()
}
