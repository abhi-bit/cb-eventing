package main

import (
	"encoding/json"
	"log"
)

// Message struct represents the protocol spec for
// communication between Golang and C++ binding
//
// CommandType would represent message type like dcp update,
//             dcpdelete, V8 debugger commands
// ExtraMetadata would capture additional info i.e. in case of
//               CommandType being V8 debug command,
//               ExtraMetadata would capture debug command type
//               like setbreakpoint, clearbreakpoint etc
// RawMessage string capture the actual message to be sent
type Message struct {
	CommandType   string
	ExtraMetadata string
	RawMessage    string
}

func EncodeMessage(commandType,
	extraMetadata,
	rawMessage string) (string, error) {

	msg := Message{
		CommandType:   commandType,
		ExtraMetadata: extraMetadata,
		RawMessage:    rawMessage,
	}
	content, err := json.Marshal(msg)
	if err != nil {
		log.Fatal(err)
		return "", err
	} else {
		return string(content), err
	}
}

func DecodeMessage(resp string) (*Message, error) {
	byt := []byte(resp)
	var msg Message
	if err := json.Unmarshal(byt, &msg); err != nil {
		log.Fatal(err)
		return &msg, err
	} else {
		return &msg, nil
	}
}
