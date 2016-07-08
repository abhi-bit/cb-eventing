#ifndef __WORKER_H__
#define __WORKER_H__

#include <string>
#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "binding.h"

using namespace v8;
using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

class Bucket;
class HTTPResponse;
class N1QL;
class Worker;

struct worker_s {
    Worker* w;
};

class ArrayBufferAllocator : public ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

class Worker {
  public:
    Worker(int table_index);
    ~Worker();

    int WorkerLoad(char* name_s, char* source_s);
    const char* WorkerLastException();
    const char* WorkerVersion();

    int SendUpdate(const char* value, const char* meta, const char* doc_type);
    int SendDelete(const char* msg);
    const char* SendHTTPGet(const char* http_req);

    void WorkerDispose();
    void WorkerTerminateExecution();

    Isolate* GetIsolate() { return isolate_; }
    Persistent<Context> context_;

    Persistent<Function> on_delete_;
    Persistent<Function> on_update_;
    Persistent<Function> on_http_get_;
    Persistent<Function> on_http_post_;
    Persistent<Function> on_timer_event_;

    static map<string, string> http_response;

  private:
    bool ExecuteScript(Local<String> script);

    int x;
    int table_index;

    ArrayBufferAllocator allocator;
    Isolate* isolate_;

    string last_exception;

    Bucket* b;
    N1QL* n;
    HTTPResponse* r;

    map<string, string> bucket;
    map<string, string> n1ql;
};

const char* worker_version();

/*void v8_init();

Worker* worker_new(int table_index);

int worker_load(Worker* w, char* name_s, char* source_s);

const char* worker_last_exception(Worker* w);

int worker_send_update(Worker* w, const char* msg);
int worker_send_delete(Worker* w, const char* msg);

void worker_dispose(Worker* w);
void worker_terminate_execution(Worker* w);*/

#ifdef __cplusplus
} // extern "C"
#endif

#endif
