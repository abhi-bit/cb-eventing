package main

import (
	"encoding/json"
	"io/ioutil"

	"github.com/couchbase/indexing/secondary/logging"
)

func setUpMailServer(appName string) {
	mailChan := make(chan smtpFields, 10000)
	smtpServer, smtpPort, senderMailID, mailPassword :=
		getMailSettings(appName)

	if smtpServer == "" || smtpPort == "" ||
		senderMailID == "" || mailPassword == "" {
		return
	}

	logging.Infof("Setting up mail server for appname: %s chan ptr: %#v",
		appName, mailChan)

	tableLock.Lock()
	defer tableLock.Unlock()
	appMailChanMapping[appName] = mailChan
	appMailSettings[appName] = make(map[string]string)
	appMailSettings[appName]["smtpServer"] = smtpServer
	appMailSettings[appName]["smtpPort"] = smtpPort
	appMailSettings[appName]["senderMailID"] = senderMailID
	appMailSettings[appName]["password"] = mailPassword
}

func getMailSettings(appName string) (string, string, string, string) {
	logging.Infof("Reading mail configs for app: %s \n", appName)

	appData, err := ioutil.ReadFile("./apps/" + appName)
	if err == nil {
		var app application
		err := json.Unmarshal(appData, &app)
		if err != nil {
			logging.Infof("Failed parse application data for app: %s", appName)
		}

		config := app.DeploymentConfig.(map[string]interface{})
		if _, ok := config["mail_settings"]; ok {
			mailSettings := config["mail_settings"].(map[string]interface{})

			smtpServer := mailSettings["smtp_server"].(string)
			smtpPort := mailSettings["smtp_port"].(string)
			senderMailID := mailSettings["sender_mail_id"].(string)
			mailPassword := mailSettings["password"].(string)
			return smtpServer, smtpPort, senderMailID, mailPassword
		}
	}
	return "", "", "", ""
}
