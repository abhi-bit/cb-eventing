#include <cstdlib>
#include <cstring>
#include <regex>

#include "bucket.h"
#include "http_response.h"
#include "n1ql.h"
#include "parse_deployment.h"
#include "queue.h"

using namespace v8;

//extern "C" {

Local<String> createUtf8String(Isolate *isolate, const char *str) {
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
  return ObjectToString(result);
}

lcb_t* UnwrapLcbInstance(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(1));
  void* ptr = field->Value();
  return static_cast<lcb_t*>(ptr);
}

map<string, string>* UnwrapMap(Local<Object> obj) {
  Local<External> field = Local<External>::Cast(obj->GetInternalField(0));
  void* ptr = field->Value();
  return static_cast<map<string, string>*>(ptr);
}

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
      cout << "it->first " << it->first << endl;

      cout << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << endl;
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
      if (it->first == "queue") {
          map<string, vector<string> >::iterator queue = result["queue"].begin();
          for (; queue != result["queue"].end(); queue++) {
            string queue_name = queue->first;
            string endpoint = result["queue"][queue_name][1];
            string queue_alias = result["queue"][queue_name][2];

            cout << "queue_name: " << queue_name << " endpoint: " << endpoint
                 << " queue_alias: " << queue_alias << endl;
            q = new Queue(this,
                          queue_name.c_str(),
                          endpoint.c_str(),
                          queue_alias.c_str());
          }

      }
  }
  r = new HTTPResponse(this);
}

Worker::~Worker() {
  context_.Reset();
  on_delete_.Reset();
  on_update_.Reset();
}

void LoadBuiltins(string* out) {
    //ifstream ifs("../worker/binding/builtin.js");
    ifstream ifs("/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/worker/binding/builtin.js");
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

  string temp, script_to_execute;
  string content;
  LoadBuiltins(&content);

  content.append(source_s);

  // TODO: Figure out if there is a cleaner way to do preprocessing for n1ql
  // Converting n1ql("<query>") to tagged template literal i.e. n1ql`<query>`
  std::regex n1ql_ttl("(n1ql\\(\")(.*)(\"\\))");
  std::smatch n1ql_m;

  while (std::regex_search(content, n1ql_m, n1ql_ttl)) {
      temp += n1ql_m.prefix();
      std::regex re_prefix("n1ql\\(\"");
      std::regex re_suffix("\"\\)");
      temp += std::regex_replace(n1ql_m[1].str(),
                                              re_prefix, "n1ql`");
      temp += n1ql_m[2].str();
      temp += std::regex_replace(n1ql_m[3].str(),
                                              re_suffix, "`");
      content = n1ql_m.suffix();
  }
  temp += content;

  // Preprocessor for allowing queue operations
  std::regex enqueue("(enqueue\\((.*)\\, (.*)\\)))");
  std::smatch queue_m;

  while (std::regex_search(temp, queue_m, enqueue)) {
      script_to_execute += queue_m.prefix();
      script_to_execute += queue_m[2].str();
      script_to_execute += "[";
      script_to_execute += queue_m[3].str();
      script_to_execute += "]";
      temp = queue_m.suffix();
  }
  script_to_execute += temp;

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
  Local<Value> on_http_get_val;
  Local<Value> on_http_post_val;
  Local<Value> on_timer_event_val;

  if (!context->Global()->Get(context, on_update).ToLocal(&on_update_val) ||
      !context->Global()->Get(context, on_delete).ToLocal(&on_delete_val) ||
      !context->Global()->Get(context, on_http_get).ToLocal(&on_http_get_val) ||
      !context->Global()->Get(context, on_http_post).ToLocal(&on_http_post_val) ||
      !context->Global()->Get(context, on_timer_event).ToLocal(&on_timer_event_val) ||
      !on_update_val->IsFunction() ||
      !on_delete_val->IsFunction() ||
      !on_http_get_val->IsFunction() ||
      !on_http_post_val->IsFunction() ||
      !on_timer_event_val->IsFunction()) {
      return false;
  }

  Local<Function> on_update_fun = Local<Function>::Cast(on_update_val);
  on_update_.Reset(GetIsolate(), on_update_fun);

  Local<Function> on_delete_fun = Local<Function>::Cast(on_delete_val);
  on_delete_.Reset(GetIsolate(), on_delete_fun);

  Local<Function> on_http_get_fun = Local<Function>::Cast(on_http_get_val);
  on_http_get_.Reset(GetIsolate(), on_http_get_fun);

  Local<Function> on_http_post_fun = Local<Function>::Cast(on_http_post_val);
  on_http_post_.Reset(GetIsolate(), on_http_post_fun);

  Local<Function> on_timer_event_fun = Local<Function>::Cast(on_timer_event_val);
  on_timer_event_.Reset(GetIsolate(), on_timer_event_fun);

  //TODO: return proper exit codes
  if (!b->Initialize(this, &bucket)) {
    cerr << "Error initializing bucket handler" << endl;
    exit(2);
  }
  if (!n->Initialize(this, &n1ql)) {
    cerr << "Error initializing n1ql handler" << endl;
    exit(2);
  }
  if (!q->Initialize(this, &queue)) {
    cerr << "Error initializing queue handler" << endl;
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

void PrintMap(map<string, string> obj) {
  for(auto elem : obj)
    cout << elem.first << " " << elem.second << endl;
}

const char* Worker::SendHTTPGet(const char* http_req) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch(GetIsolate());

  Handle<Value> args[2];
  args[0] = v8::JSON::Parse(String::NewFromUtf8(GetIsolate(), http_req));
  args[1] = this->r->WrapHTTPResponseMap();

  if(try_catch.HasCaught()) {
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
  }

  Local<Function> on_http_get = Local<Function>::New(GetIsolate(), on_http_get_);

  on_http_get->Call(context->Global(), 2, args);

  return this->r->ConvertMapToJson();
}

const char* Worker::SendHTTPPost(const char* http_req) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  TryCatch try_catch(GetIsolate());

  Handle<Value> args[2];
  args[0] = v8::JSON::Parse(String::NewFromUtf8(GetIsolate(), http_req));
  args[1] = this->r->WrapHTTPResponseMap();

  if(try_catch.HasCaught()) {
    string last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
  }

  Local<Function> on_http_post = Local<Function>::New(GetIsolate(), on_http_post_);

  on_http_post->Call(context->Global(), 2, args);

  return this->r->ConvertMapToJson();
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
  on_doc_delete->Call(context->Global(), 1, args);

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

const char* worker_send_http_get(worker* w, const char* uri_path) {
  return w->w->SendHTTPGet(uri_path);
}

const char* worker_send_http_post(worker* w, const char* uri_path) {
  return w->w->SendHTTPPost(uri_path);
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

//}
