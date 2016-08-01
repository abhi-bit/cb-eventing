package main

import (
	"strings"
	"sync"
	"time"

	"github.com/couchbase/indexing/secondary/logging"
	"github.com/jehiah/go-strftime"
)

var timerEventWorkerChannel chan v8handleBucketConfig
var timerWG sync.WaitGroup
var fixedZone = time.FixedZone("", 0)

// NewISO8601 function
func NewISO8601(t time.Time) time.Time {
	baseTime := time.Date(
		t.Year(),
		t.Month(),
		t.Day(),
		t.Hour(),
		t.Minute(),
		t.Second(),
		0,
		fixedZone)

	//Delay the timestamp by 10 second to allow callbacks to ge processed
	date := baseTime.Add(-2 * time.Second)
	return time.Time(date)
}

func processTimerEvent(v8handleBucket v8handleBucketConfig) {
	timerTick := time.Tick(time.Second)
	defer timerWG.Done()
	for {
		select {
		case <-timerTick:
			t := NewISO8601(time.Now().UTC())

			handle := v8handleBucket.handle
			bucket := v8handleBucket.bucket

			docID := strftime.Format("%Y-%m-%dT%H:%M:%S", t)
			value, flag, cas, err := bucket.GetsRaw(docID)

			if err != nil {
				logging.Tracef("docid: %s fetch failed with error %#v", docID, err.Error())
			} else {
				logging.Infof("Timer event processing started, docid: %#v value: %#v cas: %d flag: %d bucket handle: %s\n",
					docID, string(value), cas, flag,
					workerHTTPReferrerTableBackIndex[handle])
				handle.SendTimerCallback(string(value))

				// Purge all timer event and docid once they are processed
				bucket.Delete(docID)
				keys := strings.Split(string(value), ";")
				for index := range keys {
					bucket.Delete(keys[index])
				}
			}
		}
	}

}

func startTimerProcessing() {
	for {
		select {
		case v8handleBucket := <-timerEventWorkerChannel:
			go processTimerEvent(v8handleBucket)
			timerWG.Add(1)

		}
	}
	timerWG.Wait()
}
