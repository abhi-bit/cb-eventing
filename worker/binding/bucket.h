#ifndef __BUCKET_H__
#define __BUCKET_H__

#include <string>
#include <map>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "worker.h"

using namespace std;
using namespace v8;

namespace Couchbase { class Client; }

struct Result {
    string value;
    lcb_error_t status;

    Result() : status(LCB_SUCCESS) {
    }
};

class Bucket {
  public:
    Bucket(Worker* w, const char* bname, const char* ep, const char* alias);
    ~Bucket();

    // TODO: script not needed here
    virtual bool Initialize(Worker* w,
                            map<string, string>* bucket);
    // TODO: cleanup SendUpdate/SendDelete
    int SendUpdate(Worker* w, const char *msg);
    int SendDelete(Worker* w, const char *msg);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Global<ObjectTemplate> bucket_map_template_;

    lcb_t bucket_lcb_obj;

  private:
    bool InstallMaps(map<string, string>* bucket);

    static Local<ObjectTemplate> MakeBucketMapTemplate(Isolate* isolate);

    // TODO: Cleanup MakeN1QLMapTemplate
    static Local<ObjectTemplate> MakeN1QLMapTemplate(Isolate* isolate);

    static void BucketGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketDelete(Local<Name> name,
                             const PropertyCallbackInfo<Boolean>& info);

    Local<Object> WrapBucketMap(map<string, string> *bucket);

    Isolate* isolate_;
    Persistent<Context> context_;

    string bucket_name;
    string endpoint;
    string bucket_alias;

};

#endif
