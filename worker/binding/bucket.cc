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

#include "cluster.h"

#include <libcouchbase/couchbase++.h>
#include <libcouchbase/couchbase++/views.h>
#include <libcouchbase/couchbase++/query.h>

using namespace std;
using namespace v8;

map<string, string> Worker::http_response;
Couchbase::Client* Bucket::bucket_conn_obj;
Couchbase::Client* N1QL::n1ql_conn_obj;
Isolate* HTTPResponse::http_isolate_;

static Local<String> createUtf8String(Isolate *isolate, const char *str) {
  return String::NewFromUtf8(isolate, str,
          NewStringType::kNormal).ToLocalChecked();
}

string ObjectToString(Local<Value> value) {
  String::Utf8Value utf8_value(value);
  return string(*utf8_value);
}

string ToString(Isolate* isolate, Handle<Value> object) {
  HandleScope handle_scope(isolate);

  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> global = context->Global();

  Local<Object> JSON = global->Get(String::NewFromUtf8(isolate, "JSON"))->ToObject();
  Local<Function> JSON_stringify = Local<Function>::Cast(
                                          JSON->Get(
                                              String::NewFromUtf8(isolate, "stringify")));

  Local<Value> result;
  Local<Value> args[1];
  args[0] = { object };
  result = JSON_stringify->Call(context->Global(), 1, args);
  //String::Utf8Value str(result->ToString());
  return ObjectToString(result);
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
  bucket_conn_obj = new Couchbase::Client(connstr);
  Couchbase::Status rv = bucket_conn_obj->connect();
  if (!rv.success()) {
      cout << "Bucket class: Couldn't connect to '"
           << connstr << "'. Reason" << rv << endl;
      exit(1);
  }
}

Bucket::~Bucket() {
    context_.Reset();
}

bool Bucket::Initialize(Worker* w,
                        map<string, string>* bucket,
                        Local<String> source) {

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

  result->SetInternalField(0, map_ptr);

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

map<string, string>* Bucket::UnwrapMap(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(0));
  void* ptr = field->Value();
  return static_cast<map<string, string>*>(ptr);
}

void Bucket::BucketGet(Local<Name> name,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  auto res = bucket_conn_obj->get(key.c_str());

  const string& value = res.value();
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), value.c_str(),
                          NewStringType::kNormal,
                          static_cast<int>(value.length())).ToLocalChecked());
}

void Bucket::BucketSet(Local<Name> name, Local<Value> value_obj,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ObjectToString(value_obj);

  bucket_conn_obj->upsert(key.c_str(), value.c_str());
  info.GetReturnValue().Set(value_obj);
}

void Bucket::BucketDelete(Local<Name> name,
                       const PropertyCallbackInfo<Boolean>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  bucket_conn_obj->remove(key.c_str());

  info.GetReturnValue().Set(true);
}

Local<ObjectTemplate> Bucket::MakeBucketMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(1);
  result->SetHandler(NamedPropertyHandlerConfiguration(BucketGet,
                                                       BucketSet,
                                                       NULL,
                                                       BucketDelete));

  return handle_scope.Escape(result);
}


N1QL::N1QL(Worker* w,
          const char* bname, const char* ep,
          const char* alias) {
  isolate_ = w->GetIsolate();
  context_.Reset(isolate_, w->context_);

  bucket_name.assign(bname);
  endpoint.assign(ep);
  n1ql_alias.assign(alias);

  string connstr = "couchbase://" + GetEndPoint() + "/" + GetBucketName();
  n1ql_conn_obj = new Couchbase::Client(connstr);
  Couchbase::Status rv = n1ql_conn_obj->connect();
  if (!rv.success()) {
      cout << "Couldn't connect to '" << connstr << "'. Reason" << rv << endl;
      exit(1);
  }
}

N1QL::~N1QL() {
    context_.Reset();
}

bool N1QL::Initialize(Worker* w,
                      map<string, string>* n1ql, Local<String> source) {

  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), w->context_);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  if (!InstallMaps(n1ql))
      return false;

  return true;
}

Local<ObjectTemplate> N1QL::MakeN1QLMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(1);
  result->SetHandler(NamedPropertyHandlerConfiguration(N1QLEnumGetCall));

  return handle_scope.Escape(result);
}

Local<Object> N1QL::WrapN1QLMap(map<string, string>* obj) {
  EscapableHandleScope handle_scope(GetIsolate());

  if (n1ql_map_template_.IsEmpty()) {
    Local<ObjectTemplate> raw_template = MakeN1QLMapTemplate(GetIsolate());
    n1ql_map_template_.Reset(GetIsolate(), raw_template);
  }
  Local<ObjectTemplate> templ =
      Local<ObjectTemplate>::New(GetIsolate(), n1ql_map_template_);

  Local<Object> result =
      templ->NewInstance(GetIsolate()->GetCurrentContext()).ToLocalChecked();

  Local<External> map_ptr = External::New(GetIsolate(), obj);

  result->SetInternalField(0, map_ptr);

  return handle_scope.Escape(result);
}


bool N1QL::InstallMaps(map<string, string> *n1ql) {
  HandleScope handle_scope(GetIsolate());

  Local<Object> n1ql_obj = WrapN1QLMap(n1ql);

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  cout << "Registering handler for n1ql_alias: " << n1ql_alias.c_str() << endl;
  // Set the options object as a property on the global object.

  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                n1ql_alias.c_str(),
                                NewStringType::kNormal)
                .ToLocalChecked(),
            n1ql_obj)
      .FromJust();

  return true;
}

void N1QL::N1QLEnumGetCall(Local<Name> name,
                           const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string query = ObjectToString(Local<String>::Cast(name));

  Couchbase::Status status;
  Couchbase::QueryCommand qcmd(query.c_str());
  Couchbase::Query q(*n1ql_conn_obj, qcmd, status);

  if (!status) {
    cerr << "ERROR: Couldn't issue query: " << status << endl;
  }

  for (auto row : q) {
      //cout << "Row: " << row.json() << endl;
  }

  rapidjson::Document doc;
  if (doc.Parse(q.meta().body().data()).HasParseError()) {
    cerr << "ERROR: Unable to parse meta, exiting!" << std::endl;
    exit(1);
  }

  assert(doc.IsObject());
  rapidjson::Value& resultCount = doc["metrics"]["resultCount"];
  int count = resultCount.GetInt();

  Couchbase::Query q1(*n1ql_conn_obj, qcmd, status);
  Handle<Array> result = Array::New(info.GetIsolate(), count);
  int index = 0;
  for (auto row : q1) {
      result->Set(Integer::New(info.GetIsolate(), index),
                  v8::JSON::Parse(createUtf8String(info.GetIsolate(),
                         row.json().to_string().c_str())));
      index++;
  }

  info.GetReturnValue().Set(result);
}

HTTPResponse::HTTPResponse(Worker* w) {
  HTTPResponse::http_isolate_ = w->GetIsolate();
  isolate_ = w->GetIsolate();
  context_.Reset(isolate_, w->context_);
  worker = w;

  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), w->context_);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  InstallHTTPResponseMaps();
}

HTTPResponse::~HTTPResponse() {
    context_.Reset();
}

void HTTPResponse::HTTPResponseSet(Local<Name> name, Local<Value> value_obj,
                                const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ToString(HTTPResponse::http_isolate_, value_obj);

  Worker::http_response[key] = value;

  info.GetReturnValue().Set(value_obj);
}

Local<ObjectTemplate> HTTPResponse::MakeHTTPResponseMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(1);
  result->SetHandler(NamedPropertyHandlerConfiguration(NULL,
                                                       HTTPResponseSet));

  return handle_scope.Escape(result);
}

Local<Object> HTTPResponse::WrapHTTPResponseMap() {
  EscapableHandleScope handle_scope(GetIsolate());

  if (http_response_map_template_.IsEmpty()) {
    Local<ObjectTemplate> raw_template = MakeHTTPResponseMapTemplate(GetIsolate());
    http_response_map_template_.Reset(GetIsolate(), raw_template);
  }
  Local<ObjectTemplate> templ =
      Local<ObjectTemplate>::New(GetIsolate(), http_response_map_template_);

  Local<Object> result =
      templ->NewInstance(GetIsolate()->GetCurrentContext()).ToLocalChecked();

  Local<External> map_ptr = External::New(GetIsolate(), &Worker::http_response);

  result->SetInternalField(0, map_ptr);

  return handle_scope.Escape(result);
}

bool HTTPResponse::InstallHTTPResponseMaps() {
  HandleScope handle_scope(GetIsolate());

  Local<Object> http_response_obj = WrapHTTPResponseMap();

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  cout << "Registering handler for http_response as 'res' " << endl;
  // Set the options object as a property on the global object.
  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                "res",
                                NewStringType::kNormal)
                .ToLocalChecked(),
            http_response_obj)
      .FromJust();

  return true;
}

const char* HTTPResponse::ConvertMapToJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);

  writer.StartObject();

  for (auto elem : Worker::http_response) {
      writer.Key(elem.first.c_str());
      writer.String(elem.second.c_str());
  }

  writer.EndObject();
  Worker::http_response.clear();

  return s.GetString();
}
