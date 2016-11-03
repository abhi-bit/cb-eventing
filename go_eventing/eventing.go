package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"runtime"
	"sync"

	"github.com/abhi-bit/eventing/worker"
	"github.com/abhi-bit/gouch/jobpool"
	"github.com/couchbase/go-couchbase"
	"github.com/couchbase/indexing/secondary/logging"
)

var appSetup chan string
var appServerWG sync.WaitGroup
var workerWG sync.WaitGroup
var appHTTPservers map[string][]*HTTPServer

var appMailChanMapping map[string]chan smtpFields
var appMailSettings map[string]map[string]string
var appNameBucketHandleMapping map[string]*couchbase.Bucket

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

type smtpFields struct {
	To      string
	Subject string
	Body    string
}

var appDoneChans map[string]handleChans
var workPool *pool.WorkPool

func argParse() {

	flag.IntVar(&options.maxVbno, "maxvb", 1024,
		"maximum number of vbuckets")
	flag.StringVar(&options.kvport, "kvport", "",
		"kv port to connect")
	flag.StringVar(&options.restport, "restport", "",
		"ns_server port to connect")
	flag.IntVar(&options.stats, "stats", 100000,
		"periodic timeout in mS, to print statistics, `0` will disable stats")
	flag.BoolVar(&options.printflogs, "flogs", false,
		"display failover logs")
	flag.StringVar(&options.auth, "auth", "Administrator:asdasd",
		"Auth user and password")
	flag.BoolVar(&options.info, "info", true,
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
	fmt.Fprintf(os.Stderr, "Supplied command: %s \n",
		os.Args)
	flag.PrintDefaults()
}

func init() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	workerChannel = make(chan *worker.Worker, 100)
	timerEventWorkerChannel = make(chan v8handleBucketConfig, 100)

	appMailChanMapping = make(map[string]chan smtpFields)
	appMailSettings = make(map[string]map[string]string)
	appLocalPortMapping = make(map[int]string)

	appDoneChans = make(map[string]handleChans)
	appNameBucketHandleMapping = make(map[string]*couchbase.Bucket)
	uriPrefixAppMap = make(map[string]*staticAssetInfo)
	uriPrefixAppnameBackIndex = make(map[string]string)

	workPool = pool.New(runtime.NumCPU(), 100)

	http.HandleFunc("/debug", v8DebugHandler)
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
		source := config["source"].(map[string]interface{})

		srcBucket := source["source_bucket"].(string)
		metaBucket := workspace["metadata_bucket"].(string)
		srcEndpoint := "localhost"
		return srcBucket, metaBucket, srcEndpoint
	}
	return "", "", ""
}

func main() {
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
		http.HandleFunc("/start_dbg/", startV8Debugger)
		http.HandleFunc("/stop_dbg/", stopV8Debugger)
		http.HandleFunc("/sendmail/", sendMail)
		http.HandleFunc("/v8debug/", forwardDebugCommand)

		// exposing http endpoint to store blobs in CB via REST call
		http.HandleFunc("/store_blob/", storeBlob)

		log.Fatal(http.ListenAndServe("localhost:6061", nil))
	}()

	pwd, _ := os.Getwd()
	logging.Infof("Eventing Service: Current working dir: %s", pwd)
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
			logging.Infof("Got message to setup appname: %s", appName)
			go performAppHTTPSetup(appName)
			setUpMailServer(appName)
			go processMails(appName)
			logging.Infof("Got message to load app: %s", appName)
		}
	}
	appServerWG.Wait()
	workerWG.Wait()
}
