package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/couchbase/indexing/secondary/common"
	"github.com/couchbase/indexing/secondary/dcp"
	mcd "github.com/couchbase/indexing/secondary/dcp/transport"
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

func startBucket(cluster, bucketn string, kvaddrs []string, rch chan<- []interface{}) int {
	defer func() {
		if r := recover(); r != nil {
			logging.Errorf("%s:\n%s\n", r, logging.StackTrace())
		}
	}()

	logging.Infof("Trying to connect with %q cluster: %q kvaddrs: %q\n",
		bucketn, cluster, kvaddrs)
	b, err := common.ConnectBucket(cluster, "default", bucketn)

	var sleep time.Duration
	sleep = 1
	for err != nil {
		logging.Infof("bucket: %q unavailable, retrying after %d seconds\n",
			bucketn, sleep)
		time.Sleep(time.Second * sleep)
		b, err = common.ConnectBucket(cluster, "default", bucketn)
		if sleep < 8 {
			sleep = sleep * 2
		}
	}
	logging.Infof("Connected with %q\n", bucketn)

	dcpConfig := map[string]interface{}{
		"genChanSize":    10000,
		"dataChanSize":   10000,
		"numConnections": 4,
	}

	dcpFeed, err := b.StartDcpFeedOver(
		couchbase.NewDcpFeedName("rawupr"),
		uint32(0), options.kvaddrs, 0xABCD, dcpConfig)

	sleep = 1
	for err != nil {
		logging.Infof("Unable to open DCP Feed, retrying after %d seconds\n", sleep)
		time.Sleep(time.Second * sleep)

		dcpFeed, err = b.StartDcpFeedOver(couchbase.NewDcpFeedName("rawupr"),
			uint32(0), options.kvaddrs, 0xABCD, dcpConfig)

		if sleep < 8 {
			sleep = sleep * 2
		}
	}

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
