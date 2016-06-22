package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"

	"github.com/abhi-bit/eventing/v8worker"
)

// DiscardSendSync function
func DiscardSendSync(msg string) string { return "" }

type message struct {
	MessageType string      `json:"messageType"`
	Content     interface{} `json:"content,omitempty"`
	Handled     bool        `json:"handled"`
}

func runWorker(in <-chan message) <-chan message {
	out := make(chan message)
	go func() {
		worker := v8worker.New(func(msg string) {
			/*var message message
			if err := json.Unmarshal([]byte(msg), &message); err == nil {
				out <- message
			}*/
			return
		}, DiscardSendSync)
		file, _ := ioutil.ReadFile("handle-json.js")
		worker.Load("handle-json.js", string(file))
		for m := range in {
			bytes, err := json.Marshal(m)
			if err != nil {
				fmt.Fprintf(os.Stderr, "%v\n", err)
			}
			msg := string(bytes)
			fmt.Println(msg)
			if err := worker.SendDelete(msg); err != nil {
				fmt.Fprintf(os.Stderr, "error: %v\n", err)
			}
			if err := worker.SendUpdate(msg); err != nil {
				fmt.Fprintf(os.Stderr, "error printed from Golang: %v\n", err)
			}
		}
		close(out)
		worker.TerminateExecution()
	}()
	return out
}

func main() {
	messages := []message{message{
		MessageType: "msg",
		Content:     "foo",
	}, message{
		MessageType: "msg",
		Content:     "bar",
	}}

	out := make(chan message)
	in := runWorker(out)
	go func() {
		for _, m := range messages {
			out <- m
		}
		close(out)
	}()
	for m := range in {
		fmt.Printf("got message: %#v\n", m)
	}
}
