#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "bucket.h"

using namespace std;
using namespace v8;

static void get_callback(lcb_t, int, const lcb_RESPBASE *rb) {
    const lcb_RESPGET *resp = reinterpret_cast<const lcb_RESPGET*>(rb);
    Result *result = reinterpret_cast<Result*>(rb->cookie);

    result->status = resp->rc;
    result->value.clear();
    if (resp->rc == LCB_SUCCESS) {
        result->value.assign(
                reinterpret_cast<const char*>(resp->value),
                resp->nvalue);
    }
}

Bucket::Bucket(Worker* w,
               const char* bname,
               const char* ep, const char* alias) {
  isolate_ = w->GetIsolate();
  context_.Reset(isolate_, w->context_);

  bucket_name.assign(bname);
  endpoint.assign(ep);
  bucket_alias.assign(alias);

  string connstr = "couchbase://" + GetEndPoint() + "/" + GetBucketName();

  // lcb related setup
  lcb_create_st crst;
  memset(&crst, 0, sizeof crst);

  crst.version = 3;
  crst.v.v3.connstr = connstr.c_str();

  lcb_create(&bucket_lcb_obj, &crst);
  lcb_connect(bucket_lcb_obj);
  lcb_wait(bucket_lcb_obj);

  lcb_install_callback3(bucket_lcb_obj, LCB_CALLBACK_GET, get_callback);

}

Bucket::~Bucket() {
    lcb_destroy(bucket_lcb_obj);
    context_.Reset();
}

bool Bucket::Initialize(Worker* w,
                        map<string, string>* bucket) {

  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), w->context_);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  if (!InstallMaps(bucket))
      return false;

  return true;
}

Local<Object> Bucket::WrapBucketMap(map<string, string>* obj) {
  EscapableHandleScope handle_scope(GetIsolate());

  if (bucket_map_template_.IsEmpty()) {
    Local<ObjectTemplate> raw_template = MakeBucketMapTemplate(GetIsolate());
    bucket_map_template_.Reset(GetIsolate(), raw_template);
  }
  Local<ObjectTemplate> templ =
      Local<ObjectTemplate>::New(GetIsolate(), bucket_map_template_);

  Local<Object> result =
      templ->NewInstance(GetIsolate()->GetCurrentContext()).ToLocalChecked();

  Local<External> map_ptr = External::New(GetIsolate(), obj);
  Local<External> bucket_lcb_obj_ptr = External::New(GetIsolate(),
                                                     &bucket_lcb_obj);
  result->SetInternalField(0, map_ptr);
  result->SetInternalField(1, bucket_lcb_obj_ptr);

  return handle_scope.Escape(result);
}

bool Bucket::InstallMaps(map<string, string>* bucket) {
  HandleScope handle_scope(GetIsolate());

  Local<Object> bucket_obj = WrapBucketMap(bucket);

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  cout << "Registering handler for bucket_alias: " << bucket_alias.c_str() << endl;
  // Set the options object as a property on the global object.
  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                bucket_alias.c_str(),
                                NewStringType::kNormal)
                .ToLocalChecked(),
            bucket_obj)
      .FromJust();

  return true;
}

void Bucket::BucketGet(Local<Name> name,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  lcb_t* bucket_lcb_obj_ptr = UnwrapLcbInstance(info.Holder());

  Result result;
  lcb_CMDGET gcmd = { 0 };
  LCB_CMD_SET_KEY(&gcmd, key.c_str(), key.length());
  lcb_sched_enter(*bucket_lcb_obj_ptr);
  lcb_get3(*bucket_lcb_obj_ptr, &result, &gcmd);
  lcb_sched_leave(*bucket_lcb_obj_ptr);
  lcb_wait(*bucket_lcb_obj_ptr);

  const string& value = result.value;
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), value.c_str(),
                          NewStringType::kNormal,
                          static_cast<int>(value.length())).ToLocalChecked());
}

void Bucket::BucketSet(Local<Name> name, Local<Value> value_obj,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ToString(info.GetIsolate(), value_obj);

  lcb_t* bucket_lcb_obj_ptr = UnwrapLcbInstance(info.Holder());

  lcb_CMDSTORE scmd = { 0 };
  LCB_CMD_SET_KEY(&scmd, key.c_str(), key.length());
  LCB_CMD_SET_VALUE(&scmd, value.c_str(), value.length());
  scmd.operation = LCB_SET;

  lcb_sched_enter(*bucket_lcb_obj_ptr);
  lcb_store3(*bucket_lcb_obj_ptr, NULL, &scmd);
  lcb_sched_leave(*bucket_lcb_obj_ptr);
  lcb_wait(*bucket_lcb_obj_ptr);

  info.GetReturnValue().Set(value_obj);
}

void Bucket::BucketDelete(Local<Name> name,
                       const PropertyCallbackInfo<Boolean>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  lcb_t* bucket_lcb_obj_ptr = UnwrapLcbInstance(info.Holder());

  lcb_CMDREMOVE rcmd = { 0 };
  LCB_CMD_SET_KEY(&rcmd, key.c_str(), key.length());

  lcb_sched_enter(*bucket_lcb_obj_ptr);
  lcb_remove3(*bucket_lcb_obj_ptr, NULL, &rcmd);
  lcb_sched_leave(*bucket_lcb_obj_ptr);
  lcb_wait(*bucket_lcb_obj_ptr);

  info.GetReturnValue().Set(true);
}

Local<ObjectTemplate> Bucket::MakeBucketMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(2);
  result->SetHandler(NamedPropertyHandlerConfiguration(BucketGet,
                                                       BucketSet,
                                                       NULL,
                                                       BucketDelete));

  return handle_scope.Escape(result);
}