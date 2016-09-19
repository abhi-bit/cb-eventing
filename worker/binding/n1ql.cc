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

#include <libcouchbase/api3.h>
#include <libcouchbase/couchbase.h>
#include <libcouchbase/n1ql.h>

#include "n1ql.h"

using namespace std;
using namespace v8;

static void query_callback(lcb_t, int, const lcb_RESPN1QL *resp) {
    Rows *rows = reinterpret_cast<Rows*>(resp->cookie);

    if (resp->rflags & LCB_RESP_F_FINAL) {
        rows->rc = resp->rc;
        rows->metadata.assign(resp->row, resp->nrow);
    } else {
        rows->rows.push_back(string(resp->row, resp->nrow));
    }
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

  // LCB setup
  lcb_create_st crst;
  memset(&crst, 0, sizeof crst);

  crst.version = 3;
  crst.v.v3.connstr = connstr.c_str();

  lcb_create(&n1ql_lcb_obj, &crst);
  lcb_connect(n1ql_lcb_obj);
  lcb_wait(n1ql_lcb_obj);
}

N1QL::~N1QL() {
    lcb_destroy(n1ql_lcb_obj);
    context_.Reset();
}

bool N1QL::Initialize(Worker* w,
                      map<string, string>* n1ql) {

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
  result->SetInternalFieldCount(2);
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
  Local<External> n1ql_lcb_obj_ptr = External::New(GetIsolate(),
                                                    &n1ql_lcb_obj);

  result->SetInternalField(0, map_ptr);
  result->SetInternalField(1, n1ql_lcb_obj_ptr);

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

  lcb_t* n1ql_lcb_obj_ptr = UnwrapLcbInstance(info.Holder());

  lcb_error_t rc;
  lcb_N1QLPARAMS *params;
  lcb_CMDN1QL qcmd= { 0 };
  Rows rows;

  params = lcb_n1p_new();
  rc = lcb_n1p_setstmtz(params, query.c_str());
  qcmd.callback = query_callback;
  rc = lcb_n1p_mkcmd(params, &qcmd);
  rc = lcb_n1ql_query(*n1ql_lcb_obj_ptr, &rows, &qcmd);
  lcb_wait(*n1ql_lcb_obj_ptr);

  auto begin = rows.rows.begin();
  auto end = rows.rows.end();

  Handle<Array> result = Array::New(info.GetIsolate(), distance(begin, end));

  if (rows.rc == LCB_SUCCESS) {
      cout << "Query successful!, rows retrieved: " << distance(begin, end) << endl;
      int index = 0;
      for (auto& row : rows.rows) {
          result->Set(Integer::New(info.GetIsolate(), index),
                      v8::JSON::Parse(createUtf8String(info.GetIsolate(),
                                      row.c_str())));
          index++;
      }
  } else {
      cerr << "Query failed!";
      cerr << "(" << int(rows.rc) << "). ";
      cerr << lcb_strerror(NULL, rows.rc) << endl;
  }

  info.GetReturnValue().Set(result);
}
