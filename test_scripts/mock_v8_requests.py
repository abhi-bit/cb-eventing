import couchbase
from couchbase.bucket import Bucket
import json
from random import randint
import requests
import threading
import time

SLEEP_DURATION = 3
v8_debug_endpoint = "http://localhost:6061/v8debug/"
conn_str = "couchbase://localhost/default"
cb = Bucket(conn_str)


def populate_one_doc():
    ssn1 = str(randint(100, 999))
    ssn2 = str(randint(10, 99))
    ssn3 = str(randint(1000, 9999))
    ssn = ssn1 + "_" + ssn2 + "_" + ssn3
    key = "v8_debug_test_" + ssn
    credit_score = randint(600, 800)
    credit_card_count = randint(1, 5)
    total_credit_limit = randint(10000, 100000)
    credit_limit_used = int((randint(0, 9) / 10.0) * total_credit_limit)
    missed_emi_payments = randint(0, 12)

    value = {'ssn': ssn, 'credit_score': credit_score,
             'credit_card_count': credit_card_count,
             'total_credit_limit': total_credit_limit,
             'credit_limit_used': credit_limit_used,
             'missed_emi_payments': missed_emi_payments}

    try:
        cb.upsert(key, value, ttl=0, format=couchbase.FMT_JSON)
    except:
        print "Upsert failed for doc id: ", key


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
    # command["arguments"]["expression"] = "100 + 300"
    # command["arguments"]["expression"] = "JSON.stringify(updated_doc)"
    # command["arguments"]["expression"] = "updated_doc.toString()"
    # command["arguments"]["expression"] = "JSON.stringify(updated_doc)"
    command["arguments"]["expression"] = "v1"
    command["arguments"]["global"] = True
    # command["arguments"]["additional_context"] = list()
    # context = dict()
    # context["name"] = "string"
    # context["handle"] = 11
    # command["arguments"]["additional_context"].append(context)
    # command["arguments"]["disable_break"] = True

    command_to_fire = json.dumps(command)
    print command_to_fire
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


def cb_store():
    # debugging cycle being followed is:
    # setbreakpoint -> 3 continue -> clearbreakpoint
    # -> 2 continue -> listbreakpoints
    populate_one_doc()
    time.sleep(2)
    populate_one_doc()

    for i in xrange(3):
        time.sleep(SLEEP_DURATION)
        populate_one_doc()

    time.sleep(SLEEP_DURATION)
    populate_one_doc()

    for i in xrange(2):
        time.sleep(SLEEP_DURATION)
        populate_one_doc()

    time.sleep(SLEEP_DURATION)
    populate_one_doc()


def test_eval():
    populate_one_doc()
    time.sleep(2)
    populate_one_doc()

    for i in xrange(6):
        time.sleep(SLEEP_DURATION)
        populate_one_doc()

    time.sleep(SLEEP_DURATION)
    for i in xrange(10):
        populate_one_doc()
        time.sleep(2)


def main():
    seq = randint(100, 1000)
    # fire_continue_request(seq)
    # fire_evaluate_request(seq + 1)
    # fire_lookup_request(seq + 2)
    # fire_backtrace_request(seq + 3)
    # fire_frame_request(seq + 4)
    # fire_source_request(seq + 5)
    # fire_setbreakpoint_request(seq + 6)
    # fire_clearbreakpoint_request(seq + 7)
    # fire_listbreakpoints_request(seq + 8)

    """"t = threading.Thread(target=test_eval)
    t.start()

    fire_setbreakpoint_request(seq + 6)
    time.sleep(SLEEP_DURATION)

    for i in xrange(6):
        fire_continue_request(seq)
        time.sleep(SLEEP_DURATION)

    fire_evaluate_request(seq + 1)
    time.sleep(SLEEP_DURATION)

    t.join()"""

    t = threading.Thread(target=cb_store)
    t.start()

    fire_evaluate_request(seq + 1)

    fire_setbreakpoint_request(seq + 6)
    time.sleep(SLEEP_DURATION)

    for i in xrange(3):
        fire_continue_request(seq)
        time.sleep(SLEEP_DURATION)

    fire_clearbreakpoint_request(seq + 7)
    time.sleep(SLEEP_DURATION)

    for i in xrange(2):
        fire_continue_request(seq)
        time.sleep(SLEEP_DURATION)

    fire_listbreakpoints_request(seq + 8)
    time.sleep(SLEEP_DURATION)

    t.join()

if __name__ == "__main__":
    main()
