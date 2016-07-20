#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <string>
#include <map>

#include <hiredis.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "worker.h"

using namespace std;
using namespace v8;

class Queue {
  public:
    Queue(Worker* w, const char* provider, const char* ep,
          const char* alias, const char* qname);
    ~Queue();

    virtual bool Initialize(Worker* w,
                            map<string, string>* queue);

    Isolate* GetIsolate() { return isolate_; }

    Global<ObjectTemplate> queue_map_template_;

    redisContext *c;

  private:
    bool InstallQueueMaps(map<string, string>* queue);

    Local<ObjectTemplate> MakeQueueMapTemplate(Isolate* isolate);

    static void QueueGetCall(Local<Name> name,
                             const PropertyCallbackInfo<Value>& info);

    Local<Object> WrapQueueMap(map<string, string> *queue);

    Isolate* isolate_;
    Persistent<Context> context_;

    string provider;
    string queue_name;
    string endpoint;
    string queue_alias;
};

#endif
