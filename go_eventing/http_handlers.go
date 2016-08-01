package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	_ "net/http/pprof"
	"strings"

	"github.com/couchbase/indexing/secondary/logging"
)

type httpRequest struct {
	Path   string            `json:"path"`
	Host   string            `json:"host"`
	Params map[string]string `json:"params"`
}

type application struct {
	Name             string      `json:"name"`
	ID               uint64      `json:"id"`
	DeploymentStatus bool        `json:"deploy"`
	Expand           bool        `json:"expand"`
	DeploymentConfig interface{} `json:"depcfg"`
	AppHandlers      string      `json:"handlers"`
}

func handleJsRequests(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {

		req := httpRequest{
			// TODO: make it cleaner for http based application request partitioning
			Path: strings.Split(r.URL.Path, "/")[2],
			Host: r.URL.Host,
		}

		referrer := r.Host
		request, err := json.Marshal(req)
		if err != nil {
			logging.Infof("json marshalling of http request failed")
		}
		res := workerHTTPReferrerTable[referrer].SendHTTPGet(string(request))
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, "%s\n", res)

	} else if r.Method == "POST" {

		urlValues := make(map[string]string)
		params := r.URL.Query()
		for k, v := range params {
			urlValues[k] = v[0]
		}

		req := httpRequest{
			Path:   strings.Split(r.URL.Path, "/")[2],
			Host:   r.URL.Host,
			Params: urlValues,
		}
		referrer := r.Host
		request, err := json.Marshal(req)
		if err != nil {
			logging.Infof("json marshalling of http request failed")
		}
		res := workerHTTPReferrerTable[referrer].SendHTTPPost(string(request))
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, "%s\n", res)
	}
}

func fetchAppSetup(w http.ResponseWriter, r *http.Request) {

	files, _ := ioutil.ReadDir("./apps/")
	respData := make([]application, len(files))
	for index, file := range files {
		data, _ := ioutil.ReadFile("./apps/" + file.Name())
		var app application
		json.Unmarshal(data, &app)
		respData[index] = app
	}

	data, _ := json.Marshal(respData)
	fmt.Fprintf(w, "%s\n", data)
}

func storeAppSetup(w http.ResponseWriter, r *http.Request) {
	values := r.URL.Query()
	content, _ := ioutil.ReadAll(r.Body)
	appName := values["name"][0]

	ioutil.WriteFile("./apps/"+appName, []byte(content), 0644)

	if handle, ok := workerTable[appName]; ok {
		logging.Infof("Sending %s workerTable dump: %#v\n", appName, workerTable)
		// Sending control message to reload update application handlers
		logging.Infof("Going to send message to quit channel")
		handle.Quit <- appName
		logging.Infof("Sent message to quit channel")
	} else {

		/*handle := loadApp(appName)
		workerChannel <- handle
		go setUpEventingApp()
		workerWG.Add(1)*/
		logging.Infof("Sending message to setup http handlers for app: %s", appName)
		//appSetup <- appName
	}
	fmt.Fprintf(w, "Stored application config to disk\n")
}

func v8DebugHandler(w http.ResponseWriter, r *http.Request) {
	values := r.URL.Query()
	command := values["command"][0]
	appName := values["appname"][0]

	if handle, ok := workerTable[appName]; ok {
		payload := make([]byte, r.ContentLength)
		r.Body.Read(payload)
		var response string
		switch command {
		case "continue":
			response = handle.SendContinueRequest(string(payload))
		case "evaluate":
			response = handle.SendEvaluateRequest(string(payload))
		case "lookup":
			response = handle.SendLookupRequest(string(payload))
		case "backtrace":
			response = handle.SendBacktraceRequest(string(payload))
		case "frame":
			response = handle.SendFrameRequest(string(payload))
		case "source":
			response = handle.SendSourceRequest(string(payload))
		case "setbreakpoint":
			response = handle.SendSetBreakpointRequest(string(payload))
		case "clearbreakpoint":
			response = handle.SendClearBreakpointRequest(string(payload))
		case "listbreakpoints":
			response = handle.SendListBreakpoints(string(payload))
		}
		fmt.Fprintf(w, "%s", response)
	} else {
		fmt.Fprintf(w, "Application missing")
		return
	}
}
