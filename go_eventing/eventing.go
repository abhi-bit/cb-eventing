package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	_ "net/http/pprof"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/couchbase/cbauth"
	"github.com/couchbase/indexing/secondary/logging"
)

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

func main() {
	cluster := argParse()
	quit = make(chan int, 1)

	// setup cbauth
	if options.auth != "" {
		up := strings.Split(options.auth, ":")
		if _, err := cbauth.InternalRetryDefaultInit(cluster, up[0], up[1]); err != nil {
			logging.Fatalf("Failed to initialize cbauth: %s", err)
		}
	}

	for _, bucket := range options.buckets {
		go startBucket(cluster, bucket, options.kvaddrs)
	}

	if options.stats > 0 {
		tick = time.Tick(time.Millisecond * time.Duration(options.stats))
	}

	go func() {
		runWorker()
	}()

	go func() {
		fs := http.FileServer(http.Dir("/var/tmp/ciad"))
		http.Handle("/", fs)
		http.HandleFunc("/get_application/", fetchAppSetup)
		http.HandleFunc("/set_application/", storeAppSetup)

		log.Fatal(http.ListenAndServe("localhost:6064", nil))
	}()

	// http callbacks for application
	regexpHandler := &RegexpHandler{}
	regexpHandler.HandleFunc(regexp.MustCompile("/*"), handleJsRequests)

	log.Fatal(http.ListenAndServe("localhost:6063", regexpHandler))
}
