#ifndef __N1QL_H__
#define __N1QL_H__

#include <map>
#include <string>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "worker.h"

using namespace std;
using namespace v8;

struct Rows {
    vector<string> rows;
    string metadata;
    lcb_error_t rc;
    short htcode;
    Rows() : rc(LCB_ERROR), htcode(0) {
    }
};

class N1QL {
  public:
    N1QL(Worker* w, const char* bname, const char* ep, const char* alias);
    ~N1QL();

    virtual bool Initialize(Worker* w,
                            map<string, string>* n1ql);

    Isolate* GetIsolate() { return isolate_; }
    string GetBucketName() { return bucket_name; }
    string GetEndPoint() { return endpoint; }

    Global<ObjectTemplate> n1ql_map_template_;

    lcb_t n1ql_lcb_obj;

  private:
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

#endif
