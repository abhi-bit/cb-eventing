package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	_ "net/http/pprof"
	"strconv"
	"sync"
	"sync/atomic"

	worker "github.com/abhi-bit/eventing/worker"
	mcd "github.com/couchbase/indexing/secondary/dcp/transport"
	mc "github.com/couchbase/indexing/secondary/dcp/transport/client"
	"github.com/couchbase/indexing/secondary/logging"
)

const (
	// JSONType marker for JSON Dcp events
	JSONType = 0x2000000
)

var handle *worker.Worker
var quit chan int

type eventMeta struct {
	Key    string `json:"key"`
	Type   string `json:"type"`
	Cas    string `json:"cas"`
	Expiry string `json:"expiry"`
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
		var msg []interface{}

		// Spawns up a brand new runtime env on top of V8
		handle = worker.New()

		file, err := ioutil.ReadFile(EventHandlerLocation)
		if err != nil {
			logging.Infof("Failed to load application JS file\n")
		}
		handle.Load("handle_event.js", string(file))

		go func() {
			startTimerProcessing(handle)
		}()

		for {
			select {
			case <-quit:
				logging.Infof("Got message on quit channel")
				handle.TerminateExecution()
				logging.Infof("Reloading application handler to pick up updates\n")
				handle = worker.New()

				file, err := ioutil.ReadFile(EventHandlerLocation)
				if err != nil {
					logging.Infof("Failed to load application JS file\n")
				}
				handle.Load("handle_event.js", string(file))
			case msg = <-rch:
				atomic.AddUint64(&ops, 1)

				m := msg[1].(*mc.DcpEvent)
				if m.Opcode == mcd.DCP_MUTATION {

					// Fetching CAS value from KV to ensure idempotent-ness of callback handlers
					casValue := fmt.Sprintf("%d", m.Cas)
					_, err := bucket.GetRaw(casValue)

					if err != nil {
						logging.Infof("DCP_MUTATION opcode flag %d cas: %d error: %#v\n",
							m.Flags, m.Cas, err.Error())

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

						}
					} else {
						logging.Infof("Skipped mutation triggered by handler code, cas: %s\n", casValue)
						bucket.Delete(casValue)
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
		}
	}()

	wg.Wait()
}
