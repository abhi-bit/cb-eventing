#!/usr/bin/python

import couchbase
from couchbase.bucket import Bucket
from random import randint
import sys

host = sys.argv[1]
bucket = sys.argv[2]
doc_count = sys.argv[3]
prefix = sys.argv[4]

conn_str = "couchbase://" + host + "/" + bucket
cb = Bucket(conn_str)

for i in xrange(int(doc_count)):
    user_id = prefix + str(randint(10000, 100000))
    booking_ids = list()
    for i in xrange(0, randint(0, 3)):
        booking_id = "bid_" + str(randint(10, 10000))
        booking_ids.append(booking_id)

    value = {'user_id': user_id, 'booking_ids': booking_ids}

    try:
        cb.upsert(user_id, value, format=couchbase.FMT_JSON)
    except:
        print "Upsert failed for doc id: ", user_id
        continue
