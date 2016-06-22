package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"net/http"
	_ "net/http/pprof"
	"os"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	v8 "github.com/abhi-bit/eventing/v8worker"
	"github.com/couchbase/cbauth"
	"github.com/couchbase/indexing/secondary/common"
	"github.com/couchbase/indexing/secondary/dcp"
	mcd "github.com/couchbase/indexing/secondary/dcp/transport"
	mc "github.com/couchbase/indexing/secondary/dcp/transport/client"
	"github.com/couchbase/indexing/secondary/logging"
)

var options struct {
	buckets    []string // buckets to connect with
	maxVbno    int      // maximum number of vbuckets
	kvaddrs    []string
	stats      int // periodic timeout(ms) to print stats, 0 will disable
	printflogs bool
	auth       string
	info       bool
	debug      bool
	trace      bool
}

var done = make(chan bool, 16)
var rch = make(chan []interface{}, 10000)
var tick <-chan time.Time
var ops uint64

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
	runWorker()
}

func startBucket(cluster, bucketn string, kvaddrs []string) int {
	defer func() {
		if r := recover(); r != nil {
			logging.Errorf("%s:\n%s\n", r, logging.StackTrace())
		}
	}()

	logging.Infof("Connecting with %q\n", bucketn)
	b, err := common.ConnectBucket(cluster, "default", bucketn)
	mf(err, "bucket")

	dcpConfig := map[string]interface{}{
		"genChanSize":    10000,
		"dataChanSize":   10000,
		"numConnections": 4,
	}
	dcpFeed, err := b.StartDcpFeedOver(
		couchbase.NewDcpFeedName("rawupr"),
		uint32(0), options.kvaddrs, 0xABCD, dcpConfig)
	mf(err, "- upr")

	vbnos := listOfVbnos(options.maxVbno)

	flogs, err := b.GetFailoverLogs(0xABCD, vbnos, dcpConfig)
	mf(err, "- dcp failoverlogs")

	if options.printflogs {
		printFlogs(vbnos, flogs)
	}

	go startDcp(dcpFeed, flogs)

	for {
		e, ok := <-dcpFeed.C
		if ok == false {
			logging.Infof("Closing for bucket %q\n", b.Name)
		}
		rch <- []interface{}{b.Name, e}
	}
}

func startDcp(dcpFeed *couchbase.DcpFeed, flogs couchbase.FailoverLog) {
	start, end := uint64(0), uint64(0xFFFFFFFFFFFFFFFF)
	snapStart, snapEnd := uint64(0), uint64(0)
	for vbno, flog := range flogs {
		x := flog[len(flog)-1] // map[uint16][][2]uint64
		opaque, flags, vbuuid := uint16(vbno), uint32(0), x[0]
		err := dcpFeed.DcpRequestStream(
			vbno, opaque, flags, vbuuid, start, end, snapStart, snapEnd)
		mf(err, fmt.Sprintf("stream-req for %v failed", vbno))
	}
}

func mf(err error, msg string) {
	if err != nil {
		logging.Fatalf("%v: %v", msg, err)
	}
}

// DiscardSendSync function
func DiscardSendSync(msg string) string { return "" }

func runWorker() {
	var numCPUs = runtime.NumCPU()
	var wg sync.WaitGroup

	wg.Add(numCPUs)

	go func() {
		for {
			select {
			case <-tick:
				logging.Infof("Processed %d mutations\n", atomic.LoadUint64(&ops))
			}
		}
	}()

	for i := 0; i < numCPUs; i++ {
		logging.Infof("Spawned goroutine %d\n", i)
		go func() {
			defer wg.Done()
			handle := v8.New(func(msg string) {
				/*var message mc.DcpEvent
				if err := json.Unmarshal([]byte(msg), &message); err == nil {
				}*/
				return
			}, DiscardSendSync)
			file, _ := ioutil.ReadFile("handle_event.js")
			handle.Load("handle_event.js", string(file))
			for msg := range rch {
				m := msg[1].(*mc.DcpEvent)
				if m.Opcode == mcd.DCP_MUTATION {
					msg, err := json.Marshal(m)
					if err != nil {
						logging.Infof("Failed to marshal event: %#v\n", m)
						continue
					}
					if err := handle.SendUpdate(string(msg)); err == nil {
						atomic.AddUint64(&ops, 1)
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
	}
	wg.Wait()
}

func sprintCounts(counts map[mcd.CommandCode]int) string {
	line := ""
	for i := 0; i < 256; i++ {
		opcode := mcd.CommandCode(i)
		if n, ok := counts[opcode]; ok {
			line += fmt.Sprintf("%s:%v ", mcd.CommandNames[opcode], n)
		}
	}
	return strings.TrimRight(line, " ")
}

func listOfVbnos(maxVbno int) []uint16 {
	// list of vbuckets
	vbnos := make([]uint16, 0, maxVbno)
	for i := 0; i < maxVbno; i++ {
		vbnos = append(vbnos, uint16(i))
	}
	return vbnos
}

func printFlogs(vbnos []uint16, flogs couchbase.FailoverLog) {
	for i, vbno := range vbnos {
		logging.Infof("Failover log for vbucket %v\n", vbno)
		logging.Infof("   %#v\n", flogs[uint16(i)])
	}
	logging.Infof("\n")
}
