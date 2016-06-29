#include <include/v8.h>
#include <include/libplatform/libplatform.h>

using namespace std;
using namespace v8;

class Cluster {
  public:
    Cluster(worker* w, const char* bname, const char* ep, const char* alias);
    ~Cluster();

    virtual bool Initialize(map<string, string>* bucket,
                            map<string, string>* n1ql,
                            Local<String> script);
    int SendUpdate(const char *msg);
    int SendDelete(const char *msg);

    Global<ObjectTemplate> bucket_map_template_;
    Global<ObjectTemplate> n1ql_map_template_;
    string bucket_name;
    string endpoint;
    string bucket_alias;
    string n1ql_alias;

  private:
    bool ExecuteScript(Local<String> source);

    bool InstallMaps(map<string, string>* bucket, map<string, string>* n1ql);

    static Local<ObjectTemplate> MakeBucketMapTemplate(Isolate* isolate);

    static Local<ObjectTemplate> MakeN1QLMapTemplate(Isolate* isolate);

    static void BucketGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketDelete(Local<Name> name,
                             const PropertyCallbackInfo<Boolean>& info);
    static void N1QLEnumGetCall(Local<Name> name,
                             const PropertyCallbackInfo<Value>& info);

    Local<Object> WrapBucketMap(map<string, string> *bucket);
    Local<Object> WrapN1QLMap(map<string, string> *bucket);
    static map<string, string>* UnwrapMap(Local<Object> obj);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Isolate* isolate_;
    Persistent<Context> context_;
    Persistent<Function> on_update_;
    Persistent<Function> on_delete_;
};
