CXX=clang++
CXXFLAGS=-O3 -c -fPIC -std=c++11 -Wall

CBDEPS_DIR=/Users/$(USER)/.cbdepscache/
PHOSPHOR_INCLUDE=/var/tmp/repos/phosphor/include/
CGO_LDFLAGS="-L/Users/$(USER)/.cbdepscache/lib -lv8_binding"
DYLD_LIBRARY_PATH=/Users/$(USER)/.cbdepscache/lib

SOURCE_FILES=worker/binding/bucket.cc worker/binding/http_response.cc \
						 worker/binding/n1ql.cc worker/binding/parse_deployment.cc \
						 worker/binding/queue.cc worker/binding/worker.cc
OBJECT_FILES=bucket.o http_response.o n1ql.o parse_deployment.o queue.o worker.o

INCLUDE_DIRS=-I$(CBDEPS_DIR) -I/usr/local/include/hiredis -I$(PHOSPHOR_INCLUDE)
LDFLAGS=-dynamiclib -L$(CBDEPS_DIR)lib/ -lv8 \
				-lcouchbase -ljemalloc -lhiredis -lcurl -lphosphor
V8_BINDING_LIB=libv8_binding.dylib

binding:
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) $(SOURCE_FILES) $(FLAGS)
	$(CXX) $(LDFLAGS) $(OBJECT_FILES) -o $(V8_BINDING_LIB)
	mv $(V8_BINDING_LIB) $(CBDEPS_DIR)lib
	rm -rf $(OBJECT_FILES)

go:
	cd go_eventing; CGO_LDFLAGS=$(CGO_LDFLAGS) GOOS=darwin go build -ldflags="-s -w"

all: binding go

run:
	cd go_eventing; \
	DYLD_LIBRARY_PATH=$(DYLD_LIBRARY_PATH) ./go_eventing -auth "Administrator:asdasd" -info -stats=1000000 -kvport 11210 -restport 8091

clean:
	rm -rf $(OBJECT_FILES) go_eventing/go_eventing
