#include <include/v8.h>
#include <include/libplatform/libplatform.h>

using namespace std;
using namespace v8;

class N1QL {
  public:
    N1QL(worker* w, char* bname, char* ep);
    //virtual ~Bucket();

    virtual bool Initialize(map<string, string>* bucket,
                           Local<String> script);
    int send_doc_update_bucket(const char *msg);
    int send_doc_delete_bucket(const char *msg);

    string bucket_name;
    string endpoint;

  private:
    Global<ObjectTemplate> map_template_;
    bool ExecuteScript(Local<String> source);

    bool InstallMaps(map<string, string>* bucket);

    static Local<ObjectTemplate> MakeMapTemplate(Isolate* isolate);

    static void BucketGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketDelete(Local<Name> name,
                             const PropertyCallbackInfo<Boolean>& info);

    Local<Object> WrapMap(map<string, string> *bucket);
    static map<string, string>* UnwrapMap(Local<Object> obj);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Isolate* isolate_;
    Persistent<Context> context_;
    Persistent<Function> on_update_;
    Persistent<Function> on_delete_;
};
