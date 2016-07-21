package main

import (
	"fmt"
	"os"
	"sync"
	"time"

	worker "github.com/abhi-bit/eventing/worker"
	"github.com/jehiah/go-strftime"
)

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

func startTimerProcessing(handle *worker.Worker) {
	timerTick := time.Tick(time.Second)

	var timerWG sync.WaitGroup
	timerWG.Add(1)

	go func() {
		defer timerWG.Done()
		for {
			select {
			case <-timerTick:
				t := NewISO8601(time.Now().UTC())

				docID := strftime.Format("%Y-%m-%dT%H:%M:%S", t)
				value, flag, cas, err := bucket.GetsRaw(docID)

				if err != nil {
					fmt.Fprintf(os.Stderr, "docid: %s fetch failed with error %#v\n", docID, err.Error())
				} else {
					fmt.Printf("Timer event processing started, docid: %#v value: %#v cas: %d flag: %d\n",
						docID, string(value), cas, flag)
					handle.SendTimerCallback(string(value))

				}
			}
		}
	}()
	timerWG.Wait()
}
