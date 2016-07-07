#include <cstdlib>
#include <cstring>
#include <regex>

#include "parse_deployment.h"
#include "cluster.h"

using namespace v8;

extern "C" {

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

const char* ToJson(Isolate* isolate, Handle<Value> object) {
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
  String::Utf8Value str(result->ToString());
  return ToCString(str);
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
    const char* cstr = ToJson(args.GetIsolate(), args[i]);
    printf("%s", cstr);
  }
  printf("\n");
  fflush(stdout);
}

// Exception details will be appended to the first argument.
string ExceptionString(Isolate* isolate, TryCatch* try_catch) {
  string out;
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

Worker::Worker(int tindex) {
  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;
  isolate_ = Isolate::New(create_params);
  Locker locker(isolate_);
  Isolate::Scope isolate_scope(isolate_);
  HandleScope handle_scope(isolate_);

  isolate_->SetCaptureStackTraceForUncaughtExceptions(true);
  //isolate->SetData(0, w);
  table_index = tindex;
  Local<ObjectTemplate> global = ObjectTemplate::New(GetIsolate());

  TryCatch try_catch;

  global->Set(String::NewFromUtf8(GetIsolate(), "log"),
              FunctionTemplate::New(GetIsolate(), Print));
  if(try_catch.HasCaught()) {
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("ERROR Print exception: %s\n", last_exception.c_str());
  }


  Local<Context> context = Context::New(GetIsolate(), NULL, global);
  context_.Reset(GetIsolate(), context);

 //context->Enter();

  map<string, map<string, vector<string> > > result = ParseDeployment();
  map<string, map<string, vector<string> > >::iterator it = result.begin();

  for (; it != result.end(); it++) {
      if (it->first == "buckets") {
          map<string, vector<string> >::iterator bucket = result["buckets"].begin();
          for (; bucket != result["buckets"].end(); bucket++) {
            string bucket_alias = bucket->first;
            string bucket_name = result["buckets"][bucket_alias][0];
            string endpoint = result["buckets"][bucket_alias][1];

            b = new Bucket(this,
                           bucket_name.c_str(),
                           endpoint.c_str(),
                           bucket_alias.c_str());
          }
      }
      if (it->first == "n1ql") {
          map<string, vector<string> >::iterator n1ql = result["n1ql"].begin();
          for (; n1ql != result["n1ql"].end(); n1ql++) {
            string n1ql_alias = n1ql->first;
            string bucket_name = result["n1ql"][n1ql_alias][0];
            string endpoint = result["n1ql"][n1ql_alias][1];

            cout << "bucket: " << bucket_name << " endpoint: " << endpoint
                 << " n1ql_alias: " << n1ql_alias << endl;
            n = new N1QL(this,
                         bucket_name.c_str(),
                         endpoint.c_str(),
                         n1ql_alias.c_str());
          }
      }
  }
}

Worker::~Worker() {
  context_.Reset();
  on_delete_.Reset();
  on_update_.Reset();
}

void LoadBuiltins(string* out) {
    ifstream ifs("../worker/binding/builtin.js");
    string content((istreambuf_iterator<char> (ifs)),
                   (istreambuf_iterator<char>()));
    out->assign(content);
    return;
}

int Worker::WorkerLoad(char* name_s, char* source_s) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch;

  string script_to_execute;
  string content;
  LoadBuiltins(&content);

  content.append(source_s);

  // TODO: Figure out if there is a cleaner way to do preprocessing for n1ql
  // Converting n1ql("<query>") to tagged template literal i.e. n1ql`<query>`
  std::regex n1ql_ttl("(n1ql\\(\")(.*)(\"\\))");
  std::smatch m;

  while (std::regex_search(content, m, n1ql_ttl)) {
      script_to_execute += m.prefix();
      std::regex re_prefix("n1ql\\(\"");
      std::regex re_suffix("\"\\)");
      script_to_execute += std::regex_replace(m[1].str(), re_prefix, "n1ql`");
      script_to_execute += m[2].str();
      script_to_execute += std::regex_replace(m[3].str(), re_suffix, "`");
      content = m.suffix();
  }
  script_to_execute += content;

  Local<String> name = String::NewFromUtf8(GetIsolate(), name_s);
  Local<String> source = String::NewFromUtf8(GetIsolate(),
                                             script_to_execute.c_str());

  //cout << "script to execute: " << script_to_execute << endl;
  ScriptOrigin origin(name);

  if (!ExecuteScript(source))
      return 2;

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

  //TODO: return proper errorcode
  if (!b->Initialize(this, &bucket, source)) {
    cerr << "Error initializing bucket handler" << endl;
    exit(2);
  }
  if (!n->Initialize(this, &n1ql, source)) {
    cerr << "Error initializing n1ql handler" << endl;
    exit(2);
  }

  return 0;
}

bool Worker::ExecuteScript(Local<String> script) {
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


const char* Worker::WorkerLastException() {
  return last_exception.c_str();
}

const char* worker_version() {
    return V8::GetVersion();
}

const char* Worker::WorkerVersion() {
  return V8::GetVersion();
}

int Worker::SendUpdate(const char* value, const char* meta, const char* type ) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  //cout << "value: " << value << " meta: " << meta << " type: " << type << endl;
  TryCatch try_catch(GetIsolate());

  Handle<Value> args[2];
  string doc_type(type);
  if (doc_type.compare("json") == 0) {
      args[0] = v8::JSON::Parse(String::NewFromUtf8(GetIsolate(), value));
  }
  else {
      args[0] = String::NewFromUtf8(GetIsolate(), value);
  }

  args[1] = v8::JSON::Parse(String::NewFromUtf8(GetIsolate(), meta));

  if(try_catch.HasCaught()) {
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
  }

  Local<Function> on_doc_update = Local<Function>::New(GetIsolate(), on_update_);
  on_doc_update->Call(context->Global(), 2, args);

  if (try_catch.HasCaught()) {
    //last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

int Worker::SendDelete(const char *msg) {
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
    //last_exception = ExceptionString(GetIsolate(), &try_catch);
    return 2;
  }

  return 0;
}

int worker_send_update(worker* w, const char* value,
                       const char* meta, const char* type) {

  // TODO: return proper errorcode
  return w->w->SendUpdate(value, meta, type);
}

int worker_send_delete(worker* w, const char* msg) {

  // TODO: return proper errorcode
  return w->w->SendDelete(msg);
}

static ArrayBufferAllocator array_buffer_allocator;

void v8_init() {
  V8::InitializeICU();
  Platform* platform = platform::CreateDefaultPlatform();
  V8::InitializePlatform(platform);
  V8::Initialize();
}

worker* worker_new(int table_index) {
  worker* wrkr = (worker*)malloc(sizeof(worker));
  wrkr->w = new Worker(table_index);
  return wrkr;
}

const char* worker_last_exception(worker* w) {
    return w->w->WorkerLastException();
}

int worker_load(worker* w, char* name_s, char* source_s) {
    return w->w->WorkerLoad(name_s, source_s);
}

void worker_dispose(worker* w) {
    w->w->WorkerDispose();
}

void Worker::WorkerDispose() {
  isolate_->Dispose();
  //delete(w);
  // TODO:: Cleanup resources neatly
}

void worker_terminate_execution(worker* w) {
    w->w->WorkerTerminateExecution();
}

void Worker::WorkerTerminateExecution() {
  V8::TerminateExecution(GetIsolate());
}

}
