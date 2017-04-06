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
    ssn1 = str(randint(100, 999))
    ssn2 = str(randint(10, 99))
    ssn3 = str(randint(1000, 9999))
    ssn = ssn1 + "_" + ssn2 + "_" + ssn3
    key = prefix + ssn
    credit_score = randint(600, 800)
    credit_card_count = randint(1, 5)
    total_credit_limit = randint(10000, 100000)
    credit_limit_used = int((randint(0, 9) / 10.0) * total_credit_limit)
    missed_emi_payments = randint(0, 12)

    value = {'ssn': ssn, 'credit_score': credit_score,
             'credit_card_count': credit_card_count,
             'total_credit_limit': total_credit_limit,
             'credit_limit_used': credit_limit_used,
             'missed_emi_payments': missed_emi_payments,
             'type': 'credit_score'}

    try:
        cb.upsert(key, value, ttl=0, format=couchbase.FMT_JSON)
    except:
        print "Upsert failed for doc id: ", key
        continue
