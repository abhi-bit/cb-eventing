import json
import random
import string
import sys

TYPES = ['SET', 'DELETE', 'EXPIRATION']
OUTFILE = 'events.in'


def id_generator(size=10, chars=string.ascii_uppercase):
    return ''.join(random.SystemRandom().choice(chars) for _ in range(size))


def type_generator():
    return TYPES[int(''.join(random.SystemRandom().choice(string.digits[:3])))]


def ts_generator():
    return int(''.join(random.SystemRandom().choice(string.digits)
                       for _ in range(4)))


def json_gen(count):
    for i in xrange(count):
        doc = dict()
        doc['id'] = id_generator()
        doc['type'] = type_generator()
        doc['ts'] = ts_generator()
        with open(OUTFILE, "a") as file:
            file.write(json.dumps(doc) + '\n')

if __name__ == "__main__":
    count = int(sys.argv[1])
    json_gen(count)
