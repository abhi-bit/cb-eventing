#include <assert.h>
#include <iostream>
#include <libcouchbase/couchbase.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "v8.h"
#include "libplatform/libplatform.h"
#include "binding.h"

using namespace std;
using namespace v8;


class ArrayBufferAllocator : public ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

class Bucket;

struct worker_s {
  int x;
  int table_index;
  Isolate* isolate;
  Bucket* b;
  map<string, string> bucket;
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

class Bucket {
  public:
    Bucket(worker* w);
    //virtual ~Bucket();

    virtual bool Initialize(map<string, string>* bucket,
                           Local<String> script);
    int send_doc_update_bucket(const char *msg);
    int send_doc_delete_bucket(const char *msg);

  private:
    Global<ObjectTemplate> map_template_;
    bool ExecuteScript(Local<String> script);

    bool InstallMaps(map<string, string>* bucket);

    static Local<ObjectTemplate> MakeMapTemplate(Isolate* isolate);

    static void BucketGet(Local<Name> name,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketSet(Local<Name> name, Local<Value> value,
                          const PropertyCallbackInfo<Value>& info);
    static void BucketDelete(Local<Name> name,
                             const PropertyCallbackInfo<Boolean>& info);

    Local<Object> WrapMap(map<string, string> *bucket);
    static map<string, string>* UnwrapMap(Local<Object> obj);

    Isolate* GetIsolate() { return isolate_; }

    Isolate* isolate_;
    Global<Context> context_;
    Global<Function> on_update_;
    Global<Function> on_delete_;
};

Bucket::Bucket(worker* w) {
    isolate_ = w->isolate;
    Local<ObjectTemplate> global = ObjectTemplate::New(isolate_);
    Local<Context> context = Context::New(isolate_, NULL, global);
    context_.Reset(isolate_, context);
}

int Bucket::send_doc_update_bucket(const char *msg) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch;

  Local<Value> args[1];
  args[0] = String::NewFromUtf8(GetIsolate(), msg);

  assert(!try_catch.HasCaught());

  Handle<Value> val;
  if(!context->Global()->Get(context,
                          createUtf8String(GetIsolate(),
                                           "OnUpdate")).ToLocal(&val) ||
          !val->IsFunction()) {
    return 3;
  }

  Handle<Function> on_doc_update = Handle<Function>::Cast(val);
  on_doc_update->Call(context, context->Global(), 1, args);

  if (try_catch.HasCaught()) {
    //w->last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

int Bucket::send_doc_delete_bucket(const char *msg) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch;

  Local<Value> args[1];
  args[0] = String::NewFromUtf8(GetIsolate(), msg);

  assert(!try_catch.HasCaught());

  Handle<Value> val;
  if(!context->Global()->Get(context,
                          createUtf8String(GetIsolate(),
                                           "OnDelete")).ToLocal(&val) ||
          !val->IsFunction()) {
    return 3;
  }

  Handle<Function> on_doc_delete = Handle<Function>::Cast(val);
  on_doc_delete->Call(context, context->Global(), 1, args);

  if (try_catch.HasCaught()) {
    //w->last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

bool Bucket::Initialize(map<string, string>* bucket, Local<String> source) {

  HandleScope handle_scope(GetIsolate());

  Local<ObjectTemplate> global = ObjectTemplate::New(GetIsolate());
  global->Set(String::NewFromUtf8(GetIsolate(), "log"),
              FunctionTemplate::New(GetIsolate(), Print));

  Local<Context> context = Context::New(GetIsolate(), NULL, global);
  context_.Reset(GetIsolate(), context);

  Context::Scope context_scope(context);

  if (!InstallMaps(bucket))
      return false;

  if (!ExecuteScript(source))
      return false;

  Local<String> on_update =
      String::NewFromUtf8(GetIsolate(), "OnUpdate", NewStringType::kNormal)
        .ToLocalChecked();
  Local<String> on_delete =
      String::NewFromUtf8(GetIsolate(), "OnDelete", NewStringType::kNormal)
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

bool Bucket::InstallMaps(map<string, string>* bucket) {
  HandleScope handle_scope(GetIsolate());

  Local<Object> bucket_obj = WrapMap(bucket);

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);

  // Set the options object as a property on the global object.
  context->Global()
      ->Set(context,
            String::NewFromUtf8(GetIsolate(), "beer_sample", NewStringType::kNormal)
                .ToLocalChecked(),
            bucket_obj)
      .FromJust();

  return true;
}

bool Bucket::ExecuteScript(Local<String> script) {
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

Local<Object> Bucket::WrapMap(map<string, string>* obj) {
  EscapableHandleScope handle_scope(GetIsolate());

  if (map_template_.IsEmpty()) {
    Local<ObjectTemplate> raw_template = MakeMapTemplate(GetIsolate());
    map_template_.Reset(GetIsolate(), raw_template);
  }
  Local<ObjectTemplate> templ =
      Local<ObjectTemplate>::New(GetIsolate(), map_template_);

  Local<Object> result =
      templ->NewInstance(GetIsolate()->GetCurrentContext()).ToLocalChecked();

  Local<External> map_ptr = External::New(GetIsolate(), obj);

  result->SetInternalField(0, map_ptr);

  return handle_scope.Escape(result);

}

map<string, string>* Bucket::UnwrapMap(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(0));
  void* ptr = field->Value();
  return static_cast<map<string, string>*>(ptr);
}

void Bucket::BucketGet(Local<Name> name,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  map<string, string>* obj = UnwrapMap(info.Holder());

  string key = ObjectToString(Local<String>::Cast(name));

  map<string, string>::iterator iter = obj->find(key);

  if (iter == obj->end()) return;

  const string& value = (*iter).second;
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), value.c_str(),
                          NewStringType::kNormal,
                          static_cast<int>(value.length())).ToLocalChecked());
}

void Bucket::BucketSet(Local<Name> name, Local<Value> value_obj,
                       const PropertyCallbackInfo<Value>& info) {
  if (name->IsSymbol()) return;

  map<string, string>* obj = UnwrapMap(info.Holder());

  string key = ObjectToString(Local<String>::Cast(name));
  string value = ObjectToString(value_obj);
  printf("key: %s", key.c_str());
  printf(" value: %s\n", value.c_str());

  (*obj)[key] = value;

  info.GetReturnValue().Set(value_obj);
}

Local<ObjectTemplate> Bucket::MakeMapTemplate(
    Isolate* isolate) {
  EscapableHandleScope handle_scope(isolate);

  Local<ObjectTemplate> result = ObjectTemplate::New(isolate);
  result->SetInternalFieldCount(1);
  result->SetHandler(NamedPropertyHandlerConfiguration(BucketGet, BucketSet));

  return handle_scope.Escape(result);
}

extern "C" {
#include "_cgo_export.h"

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

  if (!w->b->Initialize(&w->bucket, source)) {
    cout << "Error initializing processor" << endl;
    exit(2);
  };
  return 0;
}

// Called from golang. Must route message to javascript lang.
// non-zero return value indicates error. check worker_last_exception().
int worker_send_doc_update(worker* w, const char* msg) {

  return w->b->send_doc_update_bucket(msg);
}

// Called from golang. Must route message to javascript lang.
// non-zero return value indicates error. check worker_last_exception().
int worker_send_doc_delete(worker* w, const char* msg) {

  return w->b->send_doc_delete_bucket(msg);
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

  w->b = new Bucket(w);

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
