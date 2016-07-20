
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>

#include "queue.h"

using namespace std;
using namespace v8;

Queue::Queue(Worker* w,
             const char* p, const char* ep,
             const char* alias, const char* qname) {
  isolate_ = w->GetIsolate();
  context_.Reset(isolate_, w->context_);

  provider.assign(p);
  endpoint.assign(ep);
  queue_alias.assign(alias);
  queue_name.assign(qname);

  std::string delimiter = ":";
  std::string hostname = endpoint.substr(0, endpoint.find(delimiter));
  endpoint.erase(0, endpoint.find(delimiter) + delimiter.length());
  std::string port = endpoint.substr(0, endpoint.find(delimiter));

  std::cout << "QUEUE:: provider: " << provider
            << " queue_name: " << queue_name
            << " hostname: " << hostname
            << " port: " << port
            << " queue_alias: " << queue_alias << std::endl;

  // Setting up redis connection handle
  struct timeval timeout = { 1, 500000 };
  c = redisConnectWithTimeout(hostname.c_str(), std::stoi(port), timeout);
  if (c == NULL || c->err) {
      if (c) {
          cout << "Connection error: " << c->errstr << endl;
          redisFree(c);
      } else {
          cout << "Connection error: can't allocate redis context" << endl;
      }
      exit(1);
  }

  // Delete pre-existing list
  redisReply* reply;
  string del_command = "DEL ";
  del_command.append(queue_name.c_str());
  reply = (redisReply*) redisCommand(c, del_command.c_str());
  freeReplyObject(reply);
}

Queue::~Queue() {
    redisFree(c);
}

bool Queue::Initialize(Worker* w, map<string, string>* queue) {
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), w->context_);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  if (!InstallQueueMaps(queue))
      return false;

  return true;
}

Local<ObjectTemplate> Queue::MakeQueueMapTemplate(Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(3);
  result->SetHandler(NamedPropertyHandlerConfiguration(QueueGetCall));

  return handle_scope.Escape(result);
}

Local<Object> Queue::WrapQueueMap(map<string, string>* obj) {
  EscapableHandleScope handle_scope(GetIsolate());

  if (queue_map_template_.IsEmpty()) {
    Local<ObjectTemplate> raw_template = MakeQueueMapTemplate(GetIsolate());
    queue_map_template_.Reset(GetIsolate(), raw_template);
  }
  Local<ObjectTemplate> templ =
      Local<ObjectTemplate>::New(GetIsolate(), queue_map_template_);

  Local<Object> result =
      templ->NewInstance(GetIsolate()->GetCurrentContext()).ToLocalChecked();

  Local<External> map_ptr = External::New(GetIsolate(), obj);
  Local<External> redis_conn_obj = External::New(GetIsolate(), c);
  Local<External> qname = External::New(GetIsolate(), &queue_name);

  result->SetInternalField(0, map_ptr);
  result->SetInternalField(1, redis_conn_obj);
  result->SetInternalField(2, qname);

  return handle_scope.Escape(result);
}

bool Queue::InstallQueueMaps(map<string, string> *queue) {
  HandleScope handle_scope(GetIsolate());

  Local<Object> queue_obj = WrapQueueMap(queue);

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  cout << "Registering handler for queue_alias: "
       << queue_alias.c_str() << endl;
  // Set the options object as a property on the global object.

  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                queue_alias.c_str(),
                                NewStringType::kNormal)
                .ToLocalChecked(),
            queue_obj)
      .FromJust();

  return true;
}

redisContext* UnwrapRedisContext(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(1));
  void* ptr = field->Value();
  return static_cast<redisContext*>(ptr);
}

const char* UnwrapQueueName(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(2));
  void* ptr = field->Value();
  return static_cast<const char*>(ptr);
}

void Queue::QueueGetCall(Local<Name> name,
                         const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string doc = ObjectToString(Local<String>::Cast(name));

  redisContext* redis_context = UnwrapRedisContext(info.Holder());
  string qname(UnwrapQueueName(info.Holder()));

  cerr << "ABHI: RedisPush " << doc.c_str() << endl;

  // TODO: Why is `16` showing up in buffer when const char* is fetched
  // from isolate heap
  qname.erase(0, 1);
  redisReply* reply = (redisReply*)redisCommand(redis_context,
                                   "LPUSH %s %s", qname.c_str(), doc.c_str());
  freeReplyObject(reply);
}
