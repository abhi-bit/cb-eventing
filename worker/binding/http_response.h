#ifndef __HTTP_RESPONSE_H__
#define __HTTP_RESPONSE_H__

#include <string>
#include <map>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "worker.h"

using namespace std;
using namespace v8;

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

  private:
    bool InstallHTTPResponseMaps();

    static Local<ObjectTemplate> MakeHTTPResponseMapTemplate(Isolate* isolate);

    static void HTTPResponseSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);

    Persistent<Context> context_;
    Isolate* isolate_;
};

#endif
