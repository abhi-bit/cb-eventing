#include <iostream>

#include "cluster.h"

#include <libcouchbase/couchbase++.h>
#include <libcouchbase/couchbase++/views.h>
#include <libcouchbase/couchbase++/query.h>

#include <rapidjson/document.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>


using namespace std;
using namespace v8;

Couchbase::Client* N1QL::n1ql_conn_obj;

string ObjectToString(Local<Value> value);

static Local<String> createUtf8String(Isolate *isolate, const char *str) {
  return String::NewFromUtf8(isolate, str,
          NewStringType::kNormal).ToLocalChecked();
}

/*
string ObjectToString(Local<Value> value) {
  String::Utf8Value utf8_value(value);
  return string(*utf8_value);
}
*/

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

  cout << "Registering handler for bucket_alias: " << n1ql_alias.c_str() << endl;
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
    cout << "ERROR: Couldn't issue query: " << status << endl;
  }

  for (auto row : q) {
      //cout << "Row: " << row.json() << endl;
  }

  rapidjson::Document doc;
  if (doc.Parse(q.meta().body().data()).HasParseError()) {
    std::cout << "ERROR: Unable to parse meta, exiting!" << std::endl;
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
                  createUtf8String(info.GetIsolate(),
                         row.json().to_string().c_str()));
      index++;
  }

  info.GetReturnValue().Set(result);
}
