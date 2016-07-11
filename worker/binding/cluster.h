#ifndef __CLUSTER_H__
#define __CLUSTER_H__

#include <string>
#include <map>

#include "worker.h"

using namespace std;
using namespace v8;

namespace Couchbase { class Client; }

class Bucket {
  public:
    Bucket(Worker* w, const char* bname, const char* ep, const char* alias);
    ~Bucket();

    // TODO: script not needed here
    virtual bool Initialize(Worker* w,
                            map<string, string>* bucket,
                            Local<String> script);
    // TODO: cleanup SendUpdate/SendDelete
    int SendUpdate(Worker* w, const char *msg);
    int SendDelete(Worker* w, const char *msg);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Global<ObjectTemplate> bucket_map_template_;

    Couchbase::Client* bucket_conn_obj;

  private:
    // TODO: cleanup ExecuteScript
    bool ExecuteScript(Local<String> source);

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

class N1QL {
  public:
    N1QL(Worker* w, const char* bname, const char* ep, const char* alias);
    ~N1QL();

    virtual bool Initialize(Worker* w,
                            map<string, string>* n1ql,
                            Local<String> script);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Global<ObjectTemplate> n1ql_map_template_;

    Couchbase::Client* n1ql_conn_obj;
  private:
    // TODO: cleanup ExecuteScript
    bool ExecuteScript(Local<String> source);

    bool InstallMaps(map<string, string>* n1ql);

    Local<ObjectTemplate> MakeN1QLMapTemplate(Isolate* isolate);

    static void N1QLEnumGetCall(Local<Name> name,
                             const PropertyCallbackInfo<Value>& info);

    Local<Object> WrapN1QLMap(map<string, string> *bucket);

    Isolate* isolate_;
    Persistent<Context> context_;

    string bucket_name;
    string endpoint;
    string n1ql_alias;

};

class HTTPResponse {
  public:
    HTTPResponse(Worker* w);
    ~HTTPResponse();

    Local<Object> WrapHTTPResponseMap();

    Isolate* GetIsolate() { return isolate_; }

    Global<ObjectTemplate> http_response_map_template_;

    const char* ConvertMapToJson();

    Worker* worker;
    map<string, string> http_response;

  private:

    bool InstallHTTPResponseMaps();

    static Local<ObjectTemplate> MakeHTTPResponseMapTemplate(Isolate* isolate);

    static void HTTPResponseSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);

    Persistent<Context> context_;
    Isolate* isolate_;
};

#endif
