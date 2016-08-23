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
    user_id = str(randint(100, 9999))
    key = prefix + "_" + user_id
    missed_emi_payments = randint(0, 12)

    value = {'user_id': key,
             'payment_ids': [],
             'type': "user_blob"}

    try:
        cb.upsert(key, value, ttl=0, format=couchbase.FMT_JSON)
    except:
        print "Upsert failed for doc id: ", key
        continue
