package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
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

const (
	DeploymentConfigLocation = "/var/tmp/deployment.json"
	AppHandlerLocation       = "/var/tmp/handle_event.js"
	AppMetaDataLocation      = "/var/tmp/metadata.json"
	IDDataLocation           = "/var/tmp/id"
	DeploymentStatusLocation = "/var/tmp/deploystatus"
	ExpandLocation           = "/var/tmp/expand"
	AppNameLocation          = "/var/tmp/appname"
	EventHandlerLocation     = "/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/handle_event.js"
)

var handle *worker.Worker
var quit chan int

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

type application struct {
	Name             string `json:"name"`
	ID               uint64 `json:"id"`
	DeploymentStatus bool   `json:"deploy"`
	Expand           bool   `json:"expand"`
	DeploymentConfig string `json:"depcfg"`
	AppHandlers      string `json:"handlers"`
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
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, "%s\n", res)
}

func fetchAppSetup(w http.ResponseWriter, r *http.Request) {

	name, _ := ioutil.ReadFile(AppNameLocation)
	id, _ := ioutil.ReadFile(IDDataLocation)
	ds, _ := ioutil.ReadFile(DeploymentStatusLocation)
	exp, _ := ioutil.ReadFile(ExpandLocation)
	depCfg, _ := ioutil.ReadFile(DeploymentConfigLocation)
	appHan, _ := ioutil.ReadFile(AppHandlerLocation)

	appID, _ := binary.Uvarint(id)
	deployStatus, _ := strconv.ParseBool(string(ds))
	expand, _ := strconv.ParseBool(string(exp))

	appConfig := application{
		Name:             string(name),
		ID:               appID - 48,
		DeploymentStatus: deployStatus,
		Expand:           expand,
		DeploymentConfig: string(depCfg),
		AppHandlers:      string(appHan),
	}

	data, _ := json.Marshal(appConfig)
	fmt.Fprintf(w, "%s\n", data)
}

func storeAppSetup(w http.ResponseWriter, r *http.Request) {
	content, _ := ioutil.ReadAll(r.Body)
	log.Println("Content", string(content))
	var app application
	err := json.Unmarshal(content, &app)
	if err != nil {
		log.Println("Failed to decode application config", err)
	}

	fmt.Printf("Data recieved from angular js: %#v\n", app)
	id := fmt.Sprintf("%d", app.ID)

	ioutil.WriteFile(IDDataLocation, []byte(id), 0644)
	ioutil.WriteFile(DeploymentStatusLocation,
		[]byte(strconv.FormatBool(app.DeploymentStatus)), 0644)
	ioutil.WriteFile(ExpandLocation,
		[]byte(strconv.FormatBool(app.Expand)), 0644)

	ioutil.WriteFile(DeploymentConfigLocation, []byte(app.DeploymentConfig), 0644)
	ioutil.WriteFile(AppHandlerLocation, []byte(app.AppHandlers), 0644)
	ioutil.WriteFile(AppNameLocation, []byte(app.Name), 0644)

	// Sending control message to reload update application handlers
	quit <- 0
	fmt.Fprintf(w, "Stored application config to disk\n")
}

func main() {
	cluster := argParse()
	quit = make(chan int)

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

		log.Fatal(http.ListenAndServe("localhost:6061", nil))
	}()

	// http callbacks for application
	regexpHandler := &RegexpHandler{}
	regexpHandler.HandleFunc(regexp.MustCompile("/*"), handleJsRequests)

	log.Fatal(http.ListenAndServe("localhost:6062", regexpHandler))
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

		file, err := ioutil.ReadFile(EventHandlerLocation)
		logging.Infof("handler file dump: %s\n", string(file))
		if err != nil {
			logging.Infof("Failed to load application JS file\n")
		}
		handle.Load("handle_event.js", string(file))

		for msg := range rch {
			select {
			case <-quit:
				handle.TerminateExecution()
				logging.Infof("Reloading application handler to pick up updates\n")
				handle = worker.New()

				file, err := ioutil.ReadFile(EventHandlerLocation)
				if err != nil {
					logging.Infof("Failed to load application JS file\n")
				}
				handle.Load("handle_event.js", string(file))
			default:

			}
			atomic.AddUint64(&ops, 1)

			m := msg[1].(*mc.DcpEvent)
			if m.Opcode == mcd.DCP_MUTATION {
				logging.Infof("DCP_MUTATION opcode flag %d\n", m.Flags)
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
						// TODO: Figure how to set JSON document with proper flag,
						// Right now treating everything as json type
						// This would blow up when you've base64 doc
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
	}()

	wg.Wait()
}
