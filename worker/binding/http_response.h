#ifndef __HTTP_RESPONSE_H__
#define __HTTP_RESPONSE_H__

#include <string>
#include <map>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "worker.h"

using namespace std;
using namespace v8;

class HTTPBody;

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

    HTTPBody* http_body;

  private:
    static Local<ObjectTemplate> MakeHTTPResponseMapTemplate(Isolate* isolate);

    static void HTTPResponseGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);

    Persistent<Context> context_;
    Isolate* isolate_;
};

class HTTPBody {
  public:
    HTTPBody(Isolate* isolate);
    ~HTTPBody();

    Local<Object> WrapHTTPBodyMap();

    Isolate* GetIsolate() { return isolate_; }

    Global<ObjectTemplate> http_body_map_template_;

    const char* ConvertMapToJson();

    map<string, string> http_body;

  private:
    static void HTTPBodySet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);

    Isolate* isolate_;
};

#endif
