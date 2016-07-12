CXX=clang++
CXXFLAGS=-O3 -c -fPIC -std=c++11

CBDEPS_DIR=/Users/asingh/.cbdepscache/

SOURCE_FILES=parse_deployment.cc worker.cc bucket.cc
OBJECT_FILES=parse_deployment.o worker.o bucket.o

INCLUDE_DIRS=-I$(CBDEPS_DIR) -I/var/tmp/repos/libcouchbase-cxx/include/ -I/usr/local/include/hiredis
LDFLAGS=-dynamiclib -L$(CBDEPS_DIR)lib/ -lv8 -lcouchbase -ljemalloc -lhiredis
V8_BINDING_LIB=libv8_binding.dylib

all:
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) $(SOURCE_FILES)
	$(CXX) $(LDFLAGS) $(OBJECT_FILES) -o $(V8_BINDING_LIB)
	mv $(V8_BINDING_LIB) $(CBDEPS_DIR)lib
	rm -rf $(OBJECT_FILES)

clean:
	rm -rf $(OBJECT_FILES)
