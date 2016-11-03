package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	_ "net/http/pprof"
	"net/smtp"
	"runtime"
	"strings"

	"github.com/abhi-bit/eventing/worker"
	"github.com/couchbase/indexing/secondary/logging"
)

var v8TCPListener net.Listener

type httpRequest struct {
	Path   string            `json:"path"`
	Host   string            `json:"host"`
	Params map[string]string `json:"params"`
}

type application struct {
	Name             string                   `json:"name"`
	ID               uint64                   `json:"id"`
	DeploymentStatus bool                     `json:"deploy"`
	Expand           bool                     `json:"expand"`
	DeploymentConfig interface{}              `json:"depcfg"`
	AppHandlers      string                   `json:"handlers"`
	Assets           []map[string]interface{} `json:"assets"`
}

type staticAsset struct {
	MimeType string `json:"mime_type"`
	Content  string `json:"content"`
}

func handleStaticAssets(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		splits := strings.Split(r.RequestURI, "/")
		requestURI := splits[1]
		assetName := splits[3]

		info := uriPrefixAppMap[requestURI]
		assetCBKey := fmt.Sprintf("%s_%s", info.appName, assetName)
		content, err := info.bucket.GetRaw(assetCBKey)
		if err != nil {
			logging.Infof("Failed to fetch asset: %s with error: %s",
				assetCBKey, err.Error())
			fmt.Fprintf(w, "Failed to fetch asset\n")
		} else {
			sAsset := staticAsset{}
			err := json.Unmarshal(content, &sAsset)
			if err != nil {
				logging.Infof("Failed to unmarshal static asset: %s",
					assetCBKey)
				fmt.Fprintf(w, "Failed to fetch asset\n")
			}
			sDec, _ := base64.StdEncoding.DecodeString(sAsset.Content)
			w.Header().Set("Content-Type", sAsset.MimeType)
			fmt.Fprintf(w, "%s", string(sDec))
		}
	} else {
		fmt.Fprintf(w, "Operation not supported for static assets\n")
	}
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

	tableLock.Lock()
	path := uriPrefixAppnameBackIndex[appName]
	info := uriPrefixAppMap[path]
	tableLock.Unlock()

	var app application
	err := json.Unmarshal(content, &app)
	if err != nil {
		errString := fmt.Sprintf("Failed to unmarshal payload for appname: %s",
			appName)
		logging.Errorf("%s", errString)
		fmt.Fprintf(w, "%s\n", errString)
		return
	}

	// Creating a copy of assets supplied
	// in order to delte entries from there
	// Not a good idea to purge entries from
	// app.Assets when one is iterating using it
	assetList := app.Assets

	for index, asset := range assetList {
		assetCBKey := fmt.Sprintf("%s_%s",
			appName, asset["name"].(string))

		if _, ok := asset["operation"]; ok {
			switch asset["operation"].(string) {
			case "delete":
				info.bucket.Delete(assetCBKey)
				if len(app.Assets) > 1 {
					app.Assets = append(
						app.Assets[:index], app.Assets[index+1:]...)
				} else {
					app.Assets = app.Assets[:0]
				}
			case "add":
				splits := strings.Split(asset["content"].(string), ",")
				content := splits[1]
				mimeType := strings.Split(splits[0], ":")[1]

				asset["mimeType"] = mimeType
				sAsset := staticAsset{
					MimeType: mimeType,
					Content:  content,
				}
				assetBlob, err := json.Marshal(sAsset)
				if err != nil {
					logging.Infof("Failed to marshal static asset: %s",
						assetCBKey)
					continue
				}
				info.bucket.SetRaw(assetCBKey, 0, assetBlob)
				delete(asset, "operation")
				delete(asset, "content")
			}
		}
	}

	appContent, err := json.Marshal(app)
	if err != nil {
		errString := fmt.Sprintf("Failed to marshal payload for appname: %s",
			appName)
		logging.Errorf("%s", errString)
		fmt.Fprintf(w, "%s\n", errString)
		return
	}

	ioutil.WriteFile("./apps/"+appName, []byte(appContent), 0644)

	tableLock.Lock()
	defer tableLock.Unlock()

	// Cleaning up previous HTTPServer on app redeploy
	if httpServerList, ok := appHTTPservers[appName]; ok {
		for i, v := range httpServerList {
			logging.Infof("App: %s HTTPServer #%d stopped",
				appName, i)
			v.Listener.Close()
		}
	}
	delete(appHTTPservers, appName)

	if mailChan, ok := appMailChanMapping[appName]; ok {
		logging.Infof("Closing mail channel for appname: %s chan ptr: %#v",
			appName, mailChan)
		close(mailChan)
		delete(appMailChanMapping, appName)
	}
	logging.Infof("appMailChanMapping dump: %#v", appMailChanMapping)

	if handle, ok := workerTable[appName]; ok {
		logging.Infof("Sending %s workerTable dump: %#v",
			appName, workerTable)

		// Cleanup job
		path := uriPrefixAppnameBackIndex[appName]
		delete(uriPrefixAppnameBackIndex, appName)
		delete(uriPrefixAppMap, path)

		// Sending control message to reload update application handlers
		logging.Infof("Going to send message to quit channel")
		handle.Quit <- appName
		logging.Infof("Sent message to quit channel")
		appSetup <- appName
		go setUpEventingApp(appName)
	} else {
		go setUpEventingApp(appName)
		appSetup <- appName
	}
	fmt.Fprintf(w, "Stored application config to disk\n")
}

func startV8Debugger(w http.ResponseWriter, r *http.Request) {
	var err error
	v8TCPListener, err = net.Listen("tcp", ":6062")
	if err != nil {
		logging.Infof("Failed to start v8 debug thread, err: %s",
			err.Error())
		fmt.Fprintf(w, "Failed to start V8 debugger thread\n")
		return
	}

	values := r.URL.Query()
	appName := values["name"][0]

	var handle *worker.Worker
	var ok bool
	tableLock.Lock()
	if handle, ok = workerTable[appName]; ok {
		handle.StartV8Debugger()
	}
	tableLock.Unlock()

	go func(net.Listener, *worker.Worker) {
		runtime.LockOSThread()
		httpServer := createHTTPServer(v8TCPListener)
		server := http.Server{}
		server.Serve(httpServer)
		handle.StopV8Debugger()
		logging.Infof("Stopped V8 debugger goroutine cleanly")
	}(v8TCPListener, handle)

	fmt.Fprintf(w, "Started V8 debugger thread\n")
}

func stopV8Debugger(w http.ResponseWriter, r *http.Request) {
	v8TCPListener.Close()
	fmt.Fprintf(w, "Stopped V8 debugger thread\n")
}

func processMails(appName string) {
	tableLock.Lock()
	mailChan := appMailChanMapping[appName]

	smtpServer := appMailSettings[appName]["smtpServer"]
	smtpPort := appMailSettings[appName]["smtpPort"]
	senderMailID := appMailSettings[appName]["senderMailID"]
	mailPassword := appMailSettings[appName]["password"]
	tableLock.Unlock()

	serverPort := fmt.Sprintf("%s:%s", smtpServer, smtpPort)

	for {
		logging.Infof("Size of mailChan: %d mailchan ptr: %#v",
			len(mailChan), mailChan)
		sFields, more := <-mailChan
		if more {
			msg := "From: " + senderMailID + "\n" +
				"To: " + sFields.To + "\n" +
				"Subject: " + sFields.Subject + "\n\n" +
				sFields.Body

			err := smtp.SendMail(
				serverPort,
				smtp.PlainAuth("",
					senderMailID,
					mailPassword,
					smtpServer),
				senderMailID,
				[]string{sFields.To},
				[]byte(msg))
			if err != nil {
				logging.Infof("Error: attempting to send mail, err: %s",
					err.Error())
			} else {
				logging.Infof("Successfully sent mail")
			}
		} else {
			logging.Infof("mailChan closed for appname: %s mailchan ptr: %#v",
				appName, mailChan)
			return
		}
	}
}

func sendMail(w http.ResponseWriter, r *http.Request) {
	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		logging.Infof("ERROR: Failed to read request body, err :%s",
			err.Error())
	}

	fields := strings.Split(string(body), "&")
	appName := strings.Split(fields[0], "=")[1]
	mailTo := strings.Split(fields[1], "=")[1]
	subject := strings.Split(fields[2], "=")[1]
	mailBody := strings.Split(fields[3], "=")[1]

	sFields := smtpFields{
		To:      mailTo,
		Subject: subject,
		Body:    mailBody,
	}

	tableLock.Lock()
	mailChan := appMailChanMapping[appName]
	tableLock.Unlock()

	logging.Infof("Got call from CGO to send mail, struct dump: %#v",
		sFields)
	mailChan <- sFields
}

func forwardDebugCommand(w http.ResponseWriter, r *http.Request) {
	values := r.URL.Query()
	body, _ := ioutil.ReadAll(r.Body)
	command := values["command"][0]
	appName := values["appname"][0]

	url := "http://localhost:6062/debug?appname=" + appName + "&command=" + command
	req, _ := http.NewRequest("POST", url, bytes.NewBuffer(body))
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		logging.Infof("Failed to forward request to 6062")
	} else {
		body, _ := ioutil.ReadAll(resp.Body)
		resp.Body.Close()
		fmt.Fprintf(w, "%s", string(body))
	}
}

func storeBlob(w http.ResponseWriter, r *http.Request) {
	values := r.URL.Query()
	appName := values["appname"][0]

	logging.Infof("Setting one blob for app: %s", appName)
	tableLock.Lock()
	bucketHandle := appNameBucketHandleMapping[appName]
	tableLock.Unlock()

	_, err := bucketHandle.SetWithMeta("test_key", 33554432, 60, map[string]interface{}{
		"credit_card_count":   3,
		"credit_score":        781,
		"total_credit_limit":  15617,
		"ssn":                 "650_57_9991",
		"credit_limit_used":   7808,
		"missed_emi_payments": 3})

	if err != nil {
		fmt.Fprintf(w, "%s", err.Error())
	} else {
		fmt.Fprintf(w, "%s\n", "Stored blob in bucket")
	}
}

func v8DebugHandler(w http.ResponseWriter, r *http.Request) {
	values := r.URL.Query()
	command := values["command"][0]
	appName := values["appname"][0]

	logging.Infof("Forwaded request command: %s appName: %s",
		command, appName)
	if handle, ok := workerTable[appName]; ok {
		p := make([]byte, r.ContentLength)
		r.Body.Read(p)
		payload := string(p)
		var response string
		switch command {
		case "continue":
			response = handle.SendContinueRequest(payload)
		case "evaluate":
			response = handle.SendEvaluateRequest(payload)
		case "lookup":
			response = handle.SendLookupRequest(payload)
		case "backtrace":
			response = handle.SendBacktraceRequest(payload)
		case "frame":
			response = handle.SendFrameRequest(payload)
		case "source":
			response = handle.SendSourceRequest(payload)
		case "setbreakpoint":
			response = handle.SendSetBreakpointRequest(payload)
		case "clearbreakpoint":
			response = handle.SendClearBreakpointRequest(payload)
		case "listbreakpoints":
			response = handle.SendListBreakpoints(payload)
		}
		fmt.Fprintf(w, "%s", response)
	} else {
		fmt.Fprintf(w, "Application missing")
		return
	}
}
