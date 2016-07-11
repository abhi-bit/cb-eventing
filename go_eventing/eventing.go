package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"net/http"
	_ "net/http/pprof"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	worker "github.com/abhi-bit/eventing/worker"
	"github.com/couchbase/cbauth"
	mcd "github.com/couchbase/indexing/secondary/dcp/transport"
	mc "github.com/couchbase/indexing/secondary/dcp/transport/client"
	"github.com/couchbase/indexing/secondary/logging"
)

const (
	// JSONType marker for JSON Dcp events
	JSONType = 0x2000000
)

var handle *worker.Worker

type eventMeta struct {
	Key    string `json:"key"`
	Type   string `json:"type"`
	Cas    string `json:"cas"`
	Expiry string `json:"expiry"`
}

type httpRequest struct {
	Path string `json:"path"`
	Host string `json:"host"`
}

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

func handleJsRequests(w http.ResponseWriter, r *http.Request) {
	req := httpRequest{
		Path: r.URL.Path,
		Host: r.URL.Host,
	}
	request, err := json.Marshal(req)
	if err != nil {
		logging.Infof("json marshalling of http request failed")
	}
	res := handle.SendHTTPGet(string(request))
	fmt.Fprintf(w, "%s\n", res)
}

func main() {
	cluster := argParse()

	go func() {
		http.ListenAndServe("localhost:6060", nil)
	}()

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

	// http callbacks for application
	regexpHandler := &RegexpHandler{}
	regexpHandler.HandleFunc(regexp.MustCompile("/*"), handleJsRequests)

	http.ListenAndServe("localhost:6061", regexpHandler)
}

func runWorker() {
	var wg sync.WaitGroup

	go func() {
		for {
			select {
			case <-tick:
				logging.Infof("Processed %d mutations\n", atomic.LoadUint64(&ops))
			}
		}
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()

		// Spawns up a brand new runtime env on top of V8
		handle = worker.New()

		file, _ := ioutil.ReadFile("handle_event.js")
		handle.Load("handle_event.js", string(file))

		for msg := range rch {
			atomic.AddUint64(&ops, 1)

			m := msg[1].(*mc.DcpEvent)
			if m.Opcode == mcd.DCP_MUTATION {
				if m.Flags == JSONType {

					meta := eventMeta{Key: string(m.Key),
						Type:   "json",
						Cas:    strconv.FormatUint(m.Cas, 16),
						Expiry: fmt.Sprint(m.Expiry),
					}

					mEvent, err := json.Marshal(meta)
					if err == nil {
						//TODO: check for return code of SendUpdate
						handle.SendUpdate(string(m.Value), string(mEvent), "json")
					} else {
						logging.Infof("Failed to marshal update event: %#v\n", meta)
					}
				} else {

					meta := eventMeta{Key: string(m.Key),
						Type:   "base64",
						Cas:    strconv.FormatUint(m.Cas, 16),
						Expiry: fmt.Sprint(m.Expiry),
					}

					mEvent, err := json.Marshal(meta)
					if err == nil {
						//TODO: check for return code of SendUpdate
						handle.SendUpdate(string(m.Value), string(mEvent), "non-json")
					} else {
						logging.Infof("Failed to marshal update event: %#v\n", meta)
					}

				}

			} else if m.Opcode == mcd.DCP_DELETION {

				msg, err := json.Marshal(m)
				if err != nil {
					logging.Infof("Failed to marshal delete event: %#v\n", m)
					continue
				}
				if err := handle.SendDelete(string(msg)); err == nil {
					atomic.AddUint64(&ops, 1)
				}
			}
		}
		handle.TerminateExecution()
	}()

	wg.Wait()
}
