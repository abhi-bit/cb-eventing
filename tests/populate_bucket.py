#!/usr/bin/python

import couchbase
from couchbase.bucket import Bucket
import sys

host = sys.argv[1]
bucket = sys.argv[2]
doc_count = sys.argv[3]
prefix = sys.argv[4]

conn_str = "couchbase://" + host + "/" + bucket
cb = Bucket(conn_str)

for i in xrange(int(doc_count)):
    key = prefix + str(i)
    value = {'uuid': key, 'foo': 'bar'}
    try:
        cb.upsert(key, value, format=couchbase.FMT_JSON)
    except:
        print "Upsert failed for doc id: ", key
        continue
