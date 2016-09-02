import json
from random import randint
import requests

v8_debug_endpoint = "http://localhost:6061/v8debug/"


def fire_continue_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "continue"
    command["arguments"] = dict()
    command["arguments"]["stepaction"] = "next"
    command["arguments"]["stepcount"] = 1  # default is 1

    command_to_fire = json.dumps(command)
    query_params = {"command": "continue", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Continue call: ", r.text


def fire_evaluate_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "evaluate"
    command["arguments"] = dict()
    command["arguments"]["global"] = True
    # command["arguments"]["expression"] = "JSON.stringify(process.version)"
    command["arguments"]["expression"] = "100 + 300"
    command["arguments"]["disable_break"] = True

    command_to_fire = json.dumps(command)
    query_params = {"command": "evaluate", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Evaluate call: ", r.text


def fire_lookup_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "lookup"
    command["arguments"] = dict()
    command["arguments"]["handles"] = list()
    command["arguments"]["handles"].append("meta")
    command["arguments"]["includeSource"] = True

    command_to_fire = json.dumps(command)
    query_params = {"command": "lookup", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Lookup call: ", r.text


def fire_backtrace_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "backtrace"
    command["arguments"] = dict()
    command["arguments"]["fromFrame"] = 0
    command["arguments"]["toFrame"] = 1
    command["arguments"]["bottom"] = True

    command_to_fire = json.dumps(command)
    query_params = {"command": "backtrace", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Backtrace call: ", r.text


def fire_frame_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "frame"
    command["arguments"] = dict()
    command["arguments"]["number"] = 1

    command_to_fire = json.dumps(command)
    query_params = {"command": "frame", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Frame call: ", r.text


def fire_source_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "source"
    command["arguments"] = dict()
    command["arguments"]["frame"] = 2
    command["arguments"]["fromLine"] = 10
    command["arguments"]["toLine"] = 20

    command_to_fire = json.dumps(command)
    query_params = {"command": "source", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "Source call: ", r.text


def fire_setbreakpoint_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "setbreakpoint"
    command["arguments"] = dict()
    command["arguments"]["type"] = "function"
    command["arguments"]["target"] = "OnUpdate"
    command["arguments"]["line"] = 1

    command_to_fire = json.dumps(command)
    query_params = {"command": "setbreakpoint", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "setbreakpoint: ", r.text


def fire_clearbreakpoint_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "clearbreakpoint"
    command["arguments"] = dict()
    command["arguments"]["type"] = "function"
    command["arguments"]["breakpoint"] = 1  # no. of breakpoints to clear

    command_to_fire = json.dumps(command)
    query_params = {"command": "clearbreakpoint", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "clearbreakpoint call: ", r.text


def fire_listbreakpoints_request(seq):
    command = dict()
    command["seq"] = seq
    command["type"] = "request"
    command["command"] = "listbreakpoints"

    command_to_fire = json.dumps(command)
    query_params = {"command": "listbreakpoints", "appname": "credit_score"}
    r = requests.post(v8_debug_endpoint, data=command_to_fire,
                      params=query_params)

    print "listbreakpoints call: ", r.text


def main():
    seq = randint(100, 1000)
    fire_continue_request(seq)
    # fire_evaluate_request(seq + 1)
    # fire_lookup_request(seq + 2)
    # fire_backtrace_request(seq + 3)
    # fire_frame_request(seq + 4)
    # fire_source_request(seq + 5)
    # fire_setbreakpoint_request(seq + 6)
    # fire_clearbreakpoint_request(seq + 7)
    # fire_listbreakpoints_request(seq + 8)

if __name__ == "__main__":
    main()
