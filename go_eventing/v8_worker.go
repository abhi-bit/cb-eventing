package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	_ "net/http/pprof"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	worker "github.com/abhi-bit/eventing/worker"
	"github.com/couchbase/go-couchbase"
	mcd "github.com/couchbase/indexing/secondary/dcp/transport"
	mc "github.com/couchbase/indexing/secondary/dcp/transport/client"
	"github.com/couchbase/indexing/secondary/logging"
)

const (
	// JSONType marker for JSON Dcp events
	JSONType = 0x2000000
)

var workerTable = make(map[string]*worker.Worker)
var workerHTTPReferrerTable = make(map[string]*worker.Worker)
var workerHTTPReferrerTableBackIndex = make(map[*worker.Worker]string)
var workerChannel chan *worker.Worker
var workerWG sync.WaitGroup
var tableLock sync.Mutex

type eventMeta struct {
	Key    string `json:"key"`
	Type   string `json:"type"`
	Cas    string `json:"cas"`
	Expiry string `json:"expiry"`
}

func loadApp(appName string) *worker.Worker {
	data, err := ioutil.ReadFile("./apps/" + appName)
	if err != nil {
		logging.Infof("Failed to load application JS file\n")
	}
	var app application

	err = json.Unmarshal(data, &app)
	if err != nil {
		logging.Infof("Failed to unmarshal configs for application: %s\n", appName)
	}

	logging.Infof("Loading application handler for app: %s\n", appName)

	newHandle := worker.New(appName)
	newHandle.Quit = make(chan string, 1)
	newHandle.Load(appName, app.AppHandlers)

	tableLock.Lock()
	workerTable[appName] = newHandle
	tableLock.Unlock()

	config := app.DeploymentConfig.(map[string]interface{})
	httpConfigs := config["http"].([]interface{})

	for appIndex := 0; appIndex < len(httpConfigs); appIndex++ {
		httpConfig := httpConfigs[appIndex].(map[string]interface{})
		referrer := fmt.Sprintf("localhost:%s", httpConfig["port"].(string))
		tableLock.Lock()
		workerHTTPReferrerTable[referrer] = newHandle
		workerHTTPReferrerTableBackIndex[newHandle] = referrer
		tableLock.Unlock()
	}
	return newHandle
}

func handleDcpEvent(handle *worker.Worker, msg []interface{},
	bucket *couchbase.Bucket, ops *uint64) {
	m := msg[1].(*mc.DcpEvent)
	if m.Opcode == mcd.DCP_MUTATION {

		bucketName := msg[0].(string)
		// Fetching CAS value from KV to ensure idempotent-ness of callback handlers
		casValue := fmt.Sprintf("%d", m.Cas)
		_, err := bucket.GetRaw(casValue)

		if err != nil {
			atomic.AddUint64(ops, 1)
			logging.Infof("Sending key: %s from bucket: %s to handle: %s",
				string(m.Key), bucketName,
				workerHTTPReferrerTableBackIndex[handle])
			logging.Tracef("DCP_MUTATION opcode flag %x cas: %x error: %#v\n",
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
					logging.Infof("Sending DCP_MUTATION to: %s\n",
						workerHTTPReferrerTableBackIndex[handle])
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
		} else {
			logging.Tracef("Skipped mutation triggered by handler code, cas: %s\n", casValue)
			bucket.Delete(casValue)
		}

	} else if m.Opcode == mcd.DCP_DELETION {

		msg, err := json.Marshal(m)
		if err != nil {
			logging.Infof("Failed to marshal delete event: %#v\n", m)
		}
		if err := handle.SendDelete(string(msg)); err == nil {
			atomic.AddUint64(ops, 1)
		}
	}
}

func runWorker(rch <-chan []interface{}, tick <-chan time.Time,
	aName string, handle *worker.Worker, bucket *couchbase.Bucket) {
	defer workerWG.Done()
	var appName string
	var ops uint64

	go func() {
		for {
			select {
			case <-tick:
				logging.Infof("Appname: %s Processed %d mutations\n",
					aName, atomic.LoadUint64(&ops))
			}
		}
	}()

	tableLock.Lock()
	logging.Tracef("INIT: handle: %#v BI: %#v chan item left count: %d\n",
		handle, workerHTTPReferrerTableBackIndex[handle], len(workerChannel))
	tableLock.Unlock()
	for {
		tableLock.Lock()
		logging.Tracef("GOROUTINE handle:%#v BI: %#v BI Dump: %#v WHTTP: %#v WT: %#v chan size: %d\n",
			handle, workerHTTPReferrerTableBackIndex[handle],
			workerHTTPReferrerTableBackIndex, workerHTTPReferrerTable,
			workerTable, len(handle.Quit))
		tableLock.Unlock()
		select {
		case appName = <-handle.Quit:
			tableLock.Lock()
			referrer := workerHTTPReferrerTableBackIndex[handle]
			delete(workerHTTPReferrerTable, referrer)
			delete(workerHTTPReferrerTableBackIndex, handle)
			tableLock.Unlock()
			logging.Infof("Got message on quit channel for appname: %s\n", appName)
			handle.TerminateExecution()

			handle = loadApp(appName)

		case msg := <-rch:
			handleDcpEvent(handle, msg, bucket, &ops)
		}
	}

}
