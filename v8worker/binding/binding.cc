#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include <libcouchbase/couchbase++.h>
#include <libcouchbase/couchbase++/views.h>
#include <libcouchbase/couchbase++/query.h>
#include <libcouchbase/couchbase++/endure.h>
#include <libcouchbase/couchbase++/logging.h>

#include <rapidjson/document.h>

#include <include/v8.h>
#include <include/libplatform/libplatform.h>
#include "binding.h"
#include "cluster.h"

using namespace std;
using namespace v8;

class Bucket;
static Couchbase::Client *conn_obj;

class ArrayBufferAllocator : public ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

struct worker_s {
  int x;
  int table_index;
  Isolate* isolate;
  Cluster* b;
  map<string, string> bucket;
  map<string, string> n1ql;
  ArrayBufferAllocator allocator;
  std::string last_exception;
  Persistent<Context> context;
};

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

// Exception details will be appended to the first argument.
std::string ExceptionString(Isolate* isolate, TryCatch* try_catch) {
  std::string out;
  size_t scratchSize = 20;
  char scratch[scratchSize]; // just some scratch space for sprintf

  HandleScope handle_scope(isolate);
  String::Utf8Value exception(try_catch->Exception());
  const char* exception_string = ToCString(exception);

  Handle<Message> message = try_catch->Message();

  if (message.IsEmpty()) {
    // V8 didn't provide any extra information about this error; just
    // print the exception.
    out.append(exception_string);
    out.append("\n");
  } else {
    // Print (filename):(line number)
    String::Utf8Value filename(message->GetScriptOrigin().ResourceName());
    const char* filename_string = ToCString(filename);
    int linenum = message->GetLineNumber();

    snprintf(scratch, scratchSize, "%i", linenum);
    out.append(filename_string);
    out.append(":");
    out.append(scratch);
    out.append("\n");

    // Print line of source code.
    String::Utf8Value sourceline(message->GetSourceLine());
    const char* sourceline_string = ToCString(sourceline);

    out.append(sourceline_string);
    out.append("\n");

    // Print wavy underline (GetUnderline is deprecated).
    int start = message->GetStartColumn();
    for (int i = 0; i < start; i++) {
      out.append(" ");
    }
    int end = message->GetEndColumn();
    for (int i = start; i < end; i++) {
      out.append("^");
    }
    out.append("\n");
    String::Utf8Value stack_trace(try_catch->StackTrace());
    if (stack_trace.length() > 0) {
      const char* stack_trace_string = ToCString(stack_trace);
      out.append(stack_trace_string);
      out.append("\n");
    } else {
      out.append(exception_string);
      out.append("\n");
    }
  }
  return out;
}

static Local<String> createUtf8String(Isolate *isolate, const char *str) {
  return String::NewFromUtf8(isolate, str,
          NewStringType::kNormal).ToLocalChecked();
}

void PrintMap(map<string, string>* m) {
  for (map<string, string>::iterator i = m->begin(); i != m->end(); i++) {
    pair<string, string> entry = *i;
    printf("%s: %s\n", entry.first.c_str(), entry.second.c_str());
  }
}

string ObjectToString(Local<Value> value) {
  String::Utf8Value utf8_value(value);
  return string(*utf8_value);
}

void Print(const FunctionCallbackInfo<Value>& args) {
  bool first = true;
  for (int i = 0; i < args.Length(); i++) {
    HandleScope handle_scope(args.GetIsolate());
    if (first) {
      first = false;
    } else {
      printf(" ");
    }
    String::Utf8Value str(args[i]);
    const char* cstr = ToCString(str);
    printf("%s", cstr);
  }
  printf("\n");
  fflush(stdout);
}

map<string, map<string, vector<string> > > ParseDeployment() {
  ifstream ifs("/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/deployment.json");
  string content((istreambuf_iterator<char>(ifs)),
                  (istreambuf_iterator<char>()));

  rapidjson::Document doc;
  if (doc.Parse(content.c_str()).HasParseError()) {
    std::cout << "Unable to parse deployment.json, exiting!" << std::endl;
    exit(1);
  }

  assert(doc.IsObject());
  map<string, map<string, vector<string> > > out;

  {
      rapidjson::Value& buckets = doc["buckets"];
      rapidjson::Value& queues = doc["queue"];
      rapidjson::Value& n1ql = doc["n1ql"];
      assert(buckets.IsArray());
      assert(queues.IsArray());
      assert(n1ql.IsArray());

      map<string, vector<string> > buckets_info;
      for(rapidjson::SizeType i = 0; i < buckets.Size(); i++) {
          vector<string> bucket_info;

          rapidjson::Value& bucket_name = buckets[i]["bucket_name"];
          rapidjson::Value& endpoint = buckets[i]["endpoint"];
          rapidjson::Value& name = buckets[i]["alias"];

          bucket_info.push_back(bucket_name.GetString());
          bucket_info.push_back(endpoint.GetString());
          bucket_info.push_back(name.GetString());

          buckets_info[bucket_name.GetString()] = bucket_info;
      }
      out["buckets"] = buckets_info;

      map<string, vector<string> > queues_info;
      for(rapidjson::SizeType i = 0; i < queues.Size(); i++) {
          vector<string> queue_info;

          rapidjson::Value& queue_name = queues[i]["queue_name"];
          rapidjson::Value& endpoint = queues[i]["endpoint"];
          rapidjson::Value& alias = buckets[i]["alias"];

          queue_info.push_back(queue_name.GetString());
          queue_info.push_back(endpoint.GetString());
          queue_info.push_back(alias.GetString());

          queues_info[queue_name.GetString()] = queue_info;
      }
      out["queues"] = queues_info;


      map<string, vector<string> > n1qls_info;
      for(rapidjson::SizeType i = 0; i < n1ql.Size(); i++) {
          vector<string> n1ql_info;

          rapidjson::Value& bucket_name = n1ql[i]["bucket_name"];
          rapidjson::Value& endpoint = n1ql[i]["endpoint"];
          rapidjson::Value& n1ql_alias = n1ql[i]["alias"];

          n1ql_info.push_back(bucket_name.GetString());
          n1ql_info.push_back(endpoint.GetString());
          n1ql_info.push_back(n1ql_alias.GetString());

          n1qls_info[n1ql_alias.GetString()] = n1ql_info;
      }
      out["n1ql"] = n1qls_info;
  }
  return out;
}

Cluster::Cluster(worker* w, const char* bname, const char* ep, const char* alias) {
    isolate_ = w->isolate;
    context_.Reset(isolate_, w->context);

    bucket_name.assign(bname);
    endpoint.assign(ep);
    bucket_alias.assign(alias);

    string connstr = "couchbase://" + GetEndPoint() + "/" + GetBucketName();
    conn_obj = new Couchbase::Client(connstr);
    Couchbase::Status rv = conn_obj->connect();
    if (!rv.success()) {
        cout << "Couldn't connect to '" << connstr << "'. Reason" << rv << endl;
        exit(1);
    }
}

Cluster::~Cluster() {
    on_update_.Reset();
    on_delete_.Reset();
    context_.Reset();
}

int Cluster::SendUpdate(const char *msg) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch(GetIsolate());

  Local<Value> args[1];
  args[0] = String::NewFromUtf8(GetIsolate(), msg);

  assert(!try_catch.HasCaught());

  Local<Function> on_doc_update = Local<Function>::New(GetIsolate(), on_update_);
  on_doc_update->Call(context, context->Global(), 1, args);

  if (try_catch.HasCaught()) {
    //w->last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

int Cluster::SendDelete(const char *msg) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch;

  Local<Value> args[1];
  args[0] = String::NewFromUtf8(GetIsolate(), msg);

  assert(!try_catch.HasCaught());

  Local<Function> on_doc_delete = Local<Function>::New(GetIsolate(), on_delete_);
  on_doc_delete->Call(context, context->Global(), 1, args);

  if (try_catch.HasCaught()) {
    //w->last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

bool Cluster::Initialize(map<string, string>* bucket,
                         map<string, string>* n1ql, Local<String> source) {

  HandleScope handle_scope(GetIsolate());

  Local<ObjectTemplate> global = ObjectTemplate::New(GetIsolate());
  global->Set(String::NewFromUtf8(GetIsolate(), "log"),
              FunctionTemplate::New(GetIsolate(), Print));

  Local<Context> context = Context::New(GetIsolate(), NULL, global);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  if (!InstallMaps(bucket, n1ql))
      return false;

  if (!ExecuteScript(source))
      return false;

  Local<String> on_update =
      String::NewFromUtf8(GetIsolate(), "OnUpdate", NewStringType::kNormal)
        .ToLocalChecked();
  Local<String> on_delete =
      String::NewFromUtf8(GetIsolate(), "OnDelete", NewStringType::kNormal)
        .ToLocalChecked();


  Local<String> on_http_get =
      String::NewFromUtf8(GetIsolate(), "OnHTTPGet", NewStringType::kNormal)
        .ToLocalChecked();
  Local<String> on_http_post =
      String::NewFromUtf8(GetIsolate(), "OnHTTPPost", NewStringType::kNormal)
        .ToLocalChecked();
  Local<String> on_timer_event =
      String::NewFromUtf8(GetIsolate(), "OnTimerEvent", NewStringType::kNormal)
        .ToLocalChecked();

  Local<Value> on_update_val;
  Local<Value> on_delete_val;

  if (!context->Global()->Get(context, on_update).ToLocal(&on_update_val) ||
      !context->Global()->Get(context, on_delete).ToLocal(&on_delete_val) ||
      !on_update_val->IsFunction() ||
      !on_delete_val->IsFunction()) {
      return false;
  }

  Local<Function> on_update_fun = Local<Function>::Cast(on_update_val);
  on_update_.Reset(GetIsolate(), on_update_fun);

  Local<Function> on_delete_fun = Local<Function>::Cast(on_delete_val);
  on_delete_.Reset(GetIsolate(), on_delete_fun);

  return true;
}

Local<Object> Cluster::WrapBucketMap(map<string, string>* obj) {
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

Local<Object> Cluster::WrapN1QLMap(map<string, string>* obj) {
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

bool Cluster::InstallMaps(map<string, string>* bucket, map<string, string> *n1ql) {
  HandleScope handle_scope(GetIsolate());

  Local<Object> bucket_obj = WrapBucketMap(bucket);
  Local<Object> n1ql_obj = WrapN1QLMap(n1ql);

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  // Set the options object as a property on the global object.
  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                bucket_alias.c_str(),
                                NewStringType::kNormal)
                .ToLocalChecked(),
            bucket_obj)
      .FromJust();

  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(),
                                "n1ql",
                                NewStringType::kNormal)
                .ToLocalChecked(),
            n1ql_obj)
      .FromJust();

  return true;
}

bool Cluster::ExecuteScript(Local<String> script) {
  HandleScope handle_scope(GetIsolate());

  TryCatch try_catch(GetIsolate());

  Local<Context> context(GetIsolate()->GetCurrentContext());

  Local<Script> compiled_script;
  if (!Script::Compile(context, script).ToLocal(&compiled_script)) {
    assert(try_catch.HasCaught());
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
    // The script failed to compile; bail out.
    return false;
  }

  Local<Value> result;
  if (!compiled_script->Run(context).ToLocal(&result)) {
    assert(try_catch.HasCaught());
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
    // Running the script failed; bail out.
    return false;
  }
  return true;
}

map<string, string>* Cluster::UnwrapMap(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(0));
  void* ptr = field->Value();
  return static_cast<map<string, string>*>(ptr);
}

void Cluster::BucketGet(Local<Name> name,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  auto res = conn_obj->get(key.c_str());

  const string& value = res.value();
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), value.c_str(),
                          NewStringType::kNormal,
                          static_cast<int>(value.length())).ToLocalChecked());
}

void Cluster::BucketSet(Local<Name> name, Local<Value> value_obj,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ObjectToString(value_obj);

  conn_obj->upsert(key.c_str(), value.c_str());
  info.GetReturnValue().Set(value_obj);
}

void Cluster::BucketDelete(Local<Name> name,
                       const PropertyCallbackInfo<Boolean>& info) {
  if (name->IsSymbol()) return;

  string key = ObjectToString(Local<String>::Cast(name));

  conn_obj->remove(key.c_str());

  info.GetReturnValue().Set(true);
}

Local<ObjectTemplate> Cluster::MakeBucketMapTemplate(
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

Local<ObjectTemplate> Cluster::MakeN1QLMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(1);
  result->SetHandler(NamedPropertyHandlerConfiguration(N1QLEnumGetCall));

  return handle_scope.Escape(result);
}

Local<String> v8_str(Isolate* isolate, const char* x) {
  return String::NewFromUtf8(isolate, x,
                             NewStringType::kNormal)
      .ToLocalChecked();
}

void Cluster::N1QLEnumGetCall(Local<Name> name,
                              const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  string query = ObjectToString(Local<String>::Cast(name));

  Couchbase::Status status;
  Couchbase::QueryCommand qcmd(query.c_str());
  Couchbase::Query q(*conn_obj, qcmd, status);

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

  Couchbase::Query q1(*conn_obj, qcmd, status);
  Handle<Array> result = Array::New(info.GetIsolate(), count);
  int index = 0;
  for (auto row : q1) {
      result->Set(Integer::New(info.GetIsolate(), index),
                  v8_str(info.GetIsolate(),
                         row.json().to_string().c_str()));
      index++;
  }

  info.GetReturnValue().Set(result);
}

extern "C" {

const char* worker_version() {
  return V8::GetVersion();
}

const char* worker_last_exception(worker* w) {
  return w->last_exception.c_str();
}

int worker_load(worker* w, char* name_s, char* source_s) {
  Locker locker(w->isolate);
  Isolate::Scope isolate_scope(w->isolate);
  HandleScope handle_scope(w->isolate);

  Local<Context> context = Local<Context>::New(w->isolate, w->context);
  Context::Scope context_scope(context);

  TryCatch try_catch;

  Local<String> name = String::NewFromUtf8(w->isolate, name_s);
  Local<String> source = String::NewFromUtf8(w->isolate, source_s);

  ScriptOrigin origin(name);

  if (!w->b->Initialize(&w->bucket, &w->n1ql, source)) {
    cout << "Error initializing processor" << endl;
    exit(2);
  };
  return 0;
}

// Called from golang. Must route message to javascript lang.
// non-zero return value indicates error. check worker_last_exception().
int worker_send_update(worker* w, const char* msg) {

  return w->b->SendUpdate(msg);
}

// Called from golang. Must route message to javascript lang.
// non-zero return value indicates error. check worker_last_exception().
int worker_send_delete(worker* w, const char* msg) {

  return w->b->SendDelete(msg);
}

static ArrayBufferAllocator array_buffer_allocator;

void v8_init() {
  V8::InitializeICU();
  Platform* platform = platform::CreateDefaultPlatform();
  V8::InitializePlatform(platform);
  V8::Initialize();
}

worker* worker_new(int table_index) {
  worker* w = new(worker);

  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &w->allocator;
  Isolate* isolate = Isolate::New(create_params);
  Locker locker(isolate);
  Isolate::Scope isolate_scope(isolate);
  HandleScope handle_scope(isolate);

  w->isolate = isolate;
  w->isolate->SetCaptureStackTraceForUncaughtExceptions(true);
  w->isolate->SetData(0, w);
  w->table_index = table_index;

  map<string, map<string, vector<string> > > result = ParseDeployment();
  map<string, map<string, vector<string> > >::iterator it = result.begin();

  for (; it != result.end(); it++) {
      if (it->first == "buckets") {
          map<string, vector<string> >::iterator bucket = result["buckets"].begin();
          for (; bucket != result["buckets"].end(); bucket++) {
            string bucket_name = bucket->first;
            string endpoint = result["buckets"][bucket_name][1];
            string alias = result["buckets"][bucket_name][2];

            cout << "bucket: " << bucket_name << " endpoint: " << endpoint
                 << " alias: " << alias << endl;

            w->b = new Cluster(w, bucket_name.c_str(),
                              endpoint.c_str(),
                              alias.c_str());
          }
      }
      /*if (it->first == "n1ql") {
          map<string, vector<string> >::iterator n1ql = result["n1ql"].begin();
          for (; n1ql != result["n1ql"].end(); n1ql++) {
            string bucket_name = n1ql->first;
            string endpoint = result["n1ql"][bucket_name][1];
            string alias = result["n1ql"][bucket_name][2];

            cout << "bucket: " << bucket_name << " endpoint: " << endpoint
                 << " alias: " << alias << endl;
            w->n = new N1QL(w, alias, endpoint);
          }
      }*/
  }
  //w->b = new Bucket(w, "beer-sample", "172.16.12.49");
  //w->b = new Bucket(w, "beer-sample", "choco");

  Local<ObjectTemplate> global = ObjectTemplate::New(w->isolate);

  Local<Context> context = Context::New(w->isolate, NULL, global);
  w->context.Reset(w->isolate, context);
  //context->Enter();

  return w;
}

void worker_dispose(worker* w) {
  w->isolate->Dispose();
  delete(w);
}

void worker_terminate_execution(worker* w) {
  V8::TerminateExecution(w->isolate);
}

}
