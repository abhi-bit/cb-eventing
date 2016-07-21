package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	_ "net/http/pprof"
	"strconv"
	"strings"

	"github.com/couchbase/indexing/secondary/logging"
)

const (
	DeploymentConfigLocation = "/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/deployment.json"
	AppHandlerLocation       = "/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/handle_event.js"
	AppMetaDataLocation      = "/var/tmp/metadata.json"
	IDDataLocation           = "/var/tmp/id"
	DeploymentStatusLocation = "/var/tmp/deploystatus"
	ExpandLocation           = "/var/tmp/expand"
	AppNameLocation          = "/var/tmp/appname"
	EventHandlerLocation     = "/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/handle_event.js"
)

type httpRequest struct {
	Path   string            `json:"path"`
	Host   string            `json:"host"`
	Params map[string]string `json:"params"`
}

type application struct {
	Name             string `json:"name"`
	ID               uint64 `json:"id"`
	DeploymentStatus bool   `json:"deploy"`
	Expand           bool   `json:"expand"`
	DeploymentConfig string `json:"depcfg"`
	AppHandlers      string `json:"handlers"`
}

func handleJsRequests(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {

		req := httpRequest{
			// TODO: make it cleaner for http based application request partitioning
			Path: strings.Split(r.URL.Path, "/")[2],
			Host: r.URL.Host,
		}
		request, err := json.Marshal(req)
		if err != nil {
			logging.Infof("json marshalling of http request failed")
		}
		res := handle.SendHTTPGet(string(request))
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, "%s\n", res)

	} else if r.Method == "POST" {

		urlValues := make(map[string]string)
		params := r.URL.Query()
		for k, v := range params {
			urlValues[k] = v[0]
		}

		req := httpRequest{
			Path:   r.URL.Path,
			Host:   r.URL.Host,
			Params: urlValues,
		}
		request, err := json.Marshal(req)
		if err != nil {
			logging.Infof("json marshalling of http request failed")
		}
		res := handle.SendHTTPPost(string(request))
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, "%s\n", res)
	}
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
	var app application
	err := json.Unmarshal(content, &app)
	if err != nil {
		log.Println("Failed to decode application config", err)
	}

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
	logging.Infof("Going to send message to quit channel")
	quit <- 0
	logging.Infof("Sent message to quit channel")
	fmt.Fprintf(w, "Stored application config to disk\n")
}
