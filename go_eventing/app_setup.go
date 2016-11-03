package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"regexp"
	"strings"
	"time"

	"github.com/couchbase/cbauth"
	"github.com/couchbase/go-couchbase"
	"github.com/couchbase/indexing/secondary/logging"
)

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

			logging.Infof("Started listening on server:port: %s on endpoint: %s\n",
				serverPortCombo, serverURIRegexp)

			tableLock.Lock()
			if _, ok := appHTTPservers[appName]; ok {
				appHTTPservers[appName] = append(
					appHTTPservers[appName],
					httpServer)
			} else {
				appHTTPservers = make(map[string][]*HTTPServer)
				appHTTPservers[appName] = make([]*HTTPServer, 1)
				appHTTPservers[appName][0] = httpServer
			}
			tableLock.Unlock()

			_, metaBucket, srcEndpoint := getSource(appName)
			connStr := "http://" + srcEndpoint + ":" + options.restport

			c, err := couchbase.Connect(connStr)
			pool, err := c.GetPool("default")
			bucket, err := pool.GetBucket(metaBucket)

			info := &staticAssetInfo{
				appName: appName,
				bucket:  bucket,
			}

			path := strings.Split(URIPath, "/")[1]
			tableLock.Lock()
			uriPrefixAppMap[path] = info
			uriPrefixAppnameBackIndex[appName] = path
			tableLock.Unlock()

			go func(httpServer *HTTPServer,
				regexpHandler *RegexpHandler) {
				defer appServerWG.Done()
				logging.Infof("HTTPServer started up")
				http.Serve(httpServer, regexpHandler)
				logging.Infof("HTTPServer cleanly closed")
			}(httpServer, regexpHandler)
		}
	}
}

func setUpEventingApp(appName string) {
	srcBucket, metaBucket, srcEndpoint := getSource(appName)
	// TODO: return if any of the above fields are missing
	logging.Infof("srcBucket: %s metadata bucket: %s srcEndpoint: %s",
		srcBucket, metaBucket, srcEndpoint)

	connStr := "http://" + srcEndpoint + ":" + options.restport

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

	srcBucketHandle, err := pool.GetBucket(srcBucket)

	sleep = 1
	for err != nil {
		logging.Infof("Bucket: %s missing, retrying after %d seconds, err: %#v",
			srcBucket, sleep, err)
		time.Sleep(time.Second * sleep)
		c, _ = couchbase.Connect(connStr)
		pool, _ = c.GetPool("default")
		srcBucketHandle, err = pool.GetBucket(srcBucket)
		if sleep < 8 {
			sleep = sleep * 2
		}
	}

	cluster := srcEndpoint + ":" + options.restport
	logging.Infof("cluster: %s auth: %#v\n", cluster, options.auth)
	if options.auth != "" {
		up := strings.Split(options.auth, ":")
		if _, err := cbauth.InternalRetryDefaultInit(cluster, up[0], up[1]); err != nil {
			logging.Fatalf("Failed to initialize cbauth: %s", err)
		}
	}

	kvaddr := srcEndpoint + ":" + options.kvport
	kvaddrs := []string{kvaddr}

	// Mutation channel for source bucket
	chans := handleChans{
		dcpStreamClose:  make(chan string, 1),
		timerEventClose: make(chan bool, 1),
		rch:             make(chan []interface{}, 10000),
	}

	tableLock.Lock()
	appDoneChans[appName] = chans
	appNameBucketHandleMapping[appName] = srcBucketHandle
	tableLock.Unlock()

	fmt.Printf("Setting up local tcp port for communication with C++ binding\n")
	setUpLocalTcpServer(appName)

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
