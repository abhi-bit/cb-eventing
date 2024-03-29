#ifndef __WORKER_H__
#define __WORKER_H__

#include <string>
#include <include/v8.h>
#include <include/v8-debug.h>
#include <include/libplatform/libplatform.h>

#include <libcouchbase/api3.h>
#include <libcouchbase/couchbase.h>

#include "binding.h"

using namespace v8;
using namespace std;

/*#ifdef __cplusplus
extern "C" {
#endif*/

class Bucket;
class HTTPResponse;
class N1QL;
class Queue;
class Worker;

struct worker_s {
    Worker* w;
};

struct Result {
    string value;
    lcb_CAS cas;
    lcb_U32 itmflags;
    lcb_error_t status;

    Result() : cas(0), itmflags(0), status(LCB_SUCCESS) {
    }
};

Local<String> createUtf8String(Isolate *isolate, const char *str);

string ObjectToString(Local<Value> value);

string ToString(Isolate* isolate, Handle<Value> object);

lcb_t* UnwrapLcbInstance(Local<Object> obj);

lcb_t* UnwrapWorkerLcbInstance(Local<Object> obj);

Worker* UnwrapWorkerInstance(Local<Object> obj);

map<string, string>* UnwrapMap(Local<Object> obj);

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
    Worker(int table_index, const char* app_name);
    ~Worker();

    int WorkerLoad(char* name_s, char* source_s);
    const char* WorkerLastException();
    const char* WorkerVersion();

    int SendUpdate(const char* value, const char* meta, const char* doc_type);
    int SendDelete(const char* msg);
    const char* SendHTTPGet(const char* http_req);
    const char* SendHTTPPost(const char* http_req);
    void SendTimerCallback(const char* keys);

    void StartV8Debugger();
    void StopV8Debugger();

    const char* SendContinueRequest(const char* request);
    const char* SendEvaluateRequest(const char* request);
    const char* SendLookupRequest(const char* request);
    const char* SendBacktraceRequest(const char* request);
    const char* SendFrameRequest(const char* request);
    const char* SendSourceRequest(const char* request);
    const char* SendSetBreakpointRequest(const char* request);
    const char* SendClearBreakpointRequest(const char* request);
    const char* SendListBreakpointsRequest(const char* request);

    void WorkerDispose();
    void WorkerTerminateExecution();

    Isolate* GetIsolate() { return isolate_; }
    Persistent<Context> context_;

    Persistent<Function> on_delete_;
    Persistent<Function> on_update_;
    Persistent<Function> on_http_get_;
    Persistent<Function> on_http_post_;

    Global<ObjectTemplate> worker_template;

    lcb_t cb_instance;
    string script_to_execute_;
    int table_index;
    string app_name_;
    bool start_debug_flag;

  private:
    bool ExecuteScript(Local<String> script);

    int x;

    ArrayBufferAllocator allocator;
    Isolate* isolate_;

    string last_exception;

    Bucket* bucket_handle;
    N1QL* n1ql_handle;
    HTTPResponse* http_response_handle;
    Queue* queue_handle;

    map<string, string> bucket;
    map<string, string> n1ql;
    map<string, string> queue;
};

const char* worker_version();

/*#ifdef __cplusplus
} // extern "C"
#endif*/

#endif
