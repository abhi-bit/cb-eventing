
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
#include <rapidjson/prettywriter.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "http_response.h"

using namespace std;
using namespace v8;

HTTPBody::HTTPBody(Isolate* isolate) {
  isolate_ = isolate;
}

HTTPBody::~HTTPBody() {
}

void HTTPBody::HTTPBodySet(Local<Name> name, Local<Value> value_obj,
                           const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ToString(info.GetIsolate(), value_obj);

  map<string, string>* body = UnwrapMap(info.Holder());
  (*body)[key] = value;

  info.GetReturnValue().Set(value_obj);
}

Local<Object> HTTPBody::WrapHTTPBodyMap() {
  EscapableHandleScope handle_scope(GetIsolate());

  Local<FunctionTemplate> body_map_template = FunctionTemplate::New(GetIsolate());
  body_map_template->InstanceTemplate()->SetHandler(
          NamedPropertyHandlerConfiguration(NULL, HTTPBodySet));
  body_map_template->InstanceTemplate()->SetInternalFieldCount(1);
  Local<Object> result = body_map_template->GetFunction()->NewInstance();

  Local<External> map_ptr = External::New(GetIsolate(), &http_body);

  result->SetInternalField(0, map_ptr);

  return handle_scope.Escape(result);
}

HTTPResponse::HTTPResponse(Worker* w) {
  isolate_ = w->GetIsolate();
  context_.Reset(isolate_, w->context_);
  worker = w;

  http_body = new HTTPBody(GetIsolate());

  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), w->context_);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);
}

HTTPResponse::~HTTPResponse() {
    context_.Reset();
}

void HTTPResponse::HTTPResponseGet(Local<Name> name,
                                  const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  Local<External> field = Local<External>::Cast(info.Holder()->GetInternalField(1));
  void* ptr = field->Value();
  HTTPBody* body = static_cast<HTTPBody*>(ptr);

  Local<Object> body_map = body->WrapHTTPBodyMap();
  info.GetReturnValue().Set(body_map);
}

Local<ObjectTemplate> HTTPResponse::MakeHTTPResponseMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(2);
  result->SetHandler(NamedPropertyHandlerConfiguration(HTTPResponseGet));

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

  Local<External> map_ptr = External::New(GetIsolate(), &http_response);
  Local<External> body = External::New(GetIsolate(), http_body);

  result->SetInternalField(0, map_ptr);
  result->SetInternalField(1, body);

  return handle_scope.Escape(result);
}

const char* HTTPResponse::ConvertMapToJson() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);

  writer.StartObject();

  for (auto elem : this->http_body->http_body) {
      writer.Key(elem.first.c_str());
      writer.RawValue(elem.second.c_str(), elem.second.length(), rapidjson::kObjectType);
  }

  writer.EndObject();
  http_response.clear();

  return s.GetString();
}
