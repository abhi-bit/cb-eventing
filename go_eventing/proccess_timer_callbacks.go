package main

import (
	"strings"
	"sync"
	"time"

	"github.com/couchbase/go-couchbase"
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

	// Delay the timestamp by 2 second
	// to allow callbacks to get processed
	date := baseTime.Add(-2 * time.Second)
	return time.Time(date)
}

func bucketGet(bucket *couchbase.Bucket, docID string) string {
	value, _, _, _ := bucket.GetsRaw(docID)
	return string(value)
}

func processTimerEvent(v8handleBucket v8handleBucketConfig) {
	defer func() {
		if r := recover(); r != nil {
			logging.Errorf("%s:\n%s\n", r, logging.StackTrace())
		}
	}()
	defer timerWG.Done()

	timerTicker := time.NewTicker(time.Second)
	for {
		select {
		case <-timerTicker.C:
			t := NewISO8601(time.Now().UTC())

			handle := v8handleBucket.handle
			bucket := v8handleBucket.bucket

			docID := strftime.Format("%Y-%m-%dT%H:%M:%S", t)

			valueCh := make(chan string, 1)
			// Enforcing timeout on bucket operations
			select {
			case valueCh <- bucketGet(bucket, docID):
				value := <-valueCh
				tableLock.Lock()
				logging.Tracef("Processed timer event for docid: %#v bucket: %s",
					docID, workerHTTPReferrerTableBackIndex[handle])
				tableLock.Unlock()
				handle.SendTimerCallback(value)

				// Purge all timer event and docid once they are processed
				bucket.Delete(docID)
				keys := strings.Split(value, ";")
				for index := range keys {
					bucket.Delete(keys[index])
				}

			case <-time.After(time.Second * 1):
				// Enforced 1 second timeout on bucket fetch operation
				logging.Errorf("Timer event docid: %s fetch failed", docID)
			}
		case <-v8handleBucket.hChans.timerEventClose:
			logging.Infof("Recieved message. Going to stop timer routine")
			timerTicker.Stop()
			v8handleBucket.bucket.Close()
			return
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
