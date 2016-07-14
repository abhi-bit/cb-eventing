#!/bin/bash

cp /Users/asingh/repo/go/src/github.com/abhi-bit/eventing/v8worker/binding/binding.cc .
cp /Users/asingh/repo/go/src/github.com/abhi-bit/eventing/v8worker/binding/cluster.h .
icc -O3 -c -fPIC -std=c++11 -I.. -I/var/tmp/repos/libcouchbase-cxx/include/ binding.cc
icc -O3 -dynamiclib -std=c++11 -L../out/x64.release/ -lv8 -lcouchbase -ljemalloc binding.o -o libv8_binding.dylib
cp libv8_binding.dylib ~/v8_libs
