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

    virtual bool Initialize(Worker* w,
                            map<string, string>* bucket,
                            Local<String> script);
    int SendUpdate(Worker* w, const char *msg);
    int SendDelete(Worker* w, const char *msg);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Global<ObjectTemplate> bucket_map_template_;

    static Couchbase::Client* bucket_conn_obj;

  private:
    bool ExecuteScript(Local<String> source);

    bool InstallMaps(map<string, string>* bucket);

    static Local<ObjectTemplate> MakeBucketMapTemplate(Isolate* isolate);

    static Local<ObjectTemplate> MakeN1QLMapTemplate(Isolate* isolate);

    static void BucketGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketDelete(Local<Name> name,
                             const PropertyCallbackInfo<Boolean>& info);

    Local<Object> WrapBucketMap(map<string, string> *bucket);
    static map<string, string>* UnwrapMap(Local<Object> obj);

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

    static Couchbase::Client* n1ql_conn_obj;
  private:
    bool ExecuteScript(Local<String> source);

    bool InstallMaps(map<string, string>* n1ql);

    static Local<ObjectTemplate> MakeN1QLMapTemplate(Isolate* isolate);

    static void N1QLEnumGetCall(Local<Name> name,
                             const PropertyCallbackInfo<Value>& info);

    Local<Object> WrapN1QLMap(map<string, string> *bucket);
    //static map<string, string>* UnwrapMap(Local<Object> obj);

    Isolate* isolate_;
    Persistent<Context> context_;

    string bucket_name;
    string endpoint;
    string n1ql_alias;

};
#endif
