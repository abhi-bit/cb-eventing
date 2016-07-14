CXX=clang++
CXXFLAGS=-O3 -c -fPIC -std=c++11

CBDEPS_DIR=/Users/asingh/.cbdepscache/

SOURCE_FILES=bucket.cc http_response.cc n1ql.cc parse_deployment.cc queue.cc worker.cc
OBJECT_FILES=bucket.o http_response.o n1ql.o parse_deployment.o queue.o worker.o

INCLUDE_DIRS=-I$(CBDEPS_DIR) -I/usr/local/include/hiredis
LDFLAGS=-dynamiclib -L$(CBDEPS_DIR)lib/ -lv8 -lcouchbase -ljemalloc -lhiredis
V8_BINDING_LIB=libv8_binding.dylib

all:
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) $(SOURCE_FILES)
	$(CXX) $(LDFLAGS) $(OBJECT_FILES) -o $(V8_BINDING_LIB)
	mv $(V8_BINDING_LIB) $(CBDEPS_DIR)lib
	rm -rf $(OBJECT_FILES)

clean:
	rm -rf $(OBJECT_FILES)
