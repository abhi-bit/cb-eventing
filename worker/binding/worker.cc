#include <cstdlib>
#include <cstring>
#include <ctime>
#include <curl/curl.h>
#include <regex>
#include <sstream>
#include <typeinfo>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include "bucket.h"
#include "http_response.h"
#include "n1ql.h"
#include "parse_deployment.h"
#include "queue.h"

using namespace v8;

enum RETURN_CODE
{
    SUCCESS = 0,
    FAILED_TO_COMPILE_JS,
    NO_HANDLERS_DEFINED,
    FAILED_INIT_BUCKET_HANDLE,
    FAILED_INIT_N1QL_HANDLE,
    FAILED_INIT_QUEUE_HANDLE,
    RAPIDJSON_FAILED_PARSE,
    ON_UPDATE_CALL_FAIL,
    ON_DELETE_CALL_FAIL
};

string cb_cluster_endpoint;
string cb_cluster_bucket;

string continue_result;
string evaluate_result;
string set_breakpoint_result;
string clear_breakpoint_result;
string list_breakpoint_result;

// Copies a C string to a 16-bit string.  Does not check for buffer overflow.
// Does not use the V8 engine to convert strings, so it can be used
// in any thread.  Returns the length of the string.
int AsciiToUtf16(const char* input_buffer, uint16_t* output_buffer) {
  int i;
  for (i = 0; input_buffer[i] != '\0'; ++i) {
    // ASCII does not use chars > 127, but be careful anyway.
    output_buffer[i] = static_cast<unsigned char>(input_buffer[i]);
  }
  output_buffer[i] = 0;
  return i;
}

bool GetEvaluateResult(char* message, string &buffer) {
  if (strstr(message, "\"command\":\"evaluate\"") == NULL) {
    return false;
  }
  if (strstr(message, "\"text\":\"") == NULL) {
    return false;
  }
  cout << __FUNCTION__ << " Message dump: " << message << endl;

  string msg(message);
  rapidjson::Document doc;
  if (doc.Parse(msg.c_str()).HasParseError()) {
      cerr << "Failed to parse v8 debug JSON response" << endl;
  }

  assert(doc.IsObject());
  string result;
  {
      rapidjson::Value& value = doc["body"]["text"];
      result.assign(value.GetString());
  }
  buffer.assign(result);
  return true;
}

static void DebugEvaluateHandler(const v8::Debug::Message& message) {
  v8::Local<v8::String> json = message.GetJSON();
  v8::String::Utf8Value utf8(json);

  GetEvaluateResult(*utf8, evaluate_result);
}

bool SetBreakpointResult(char* message) {
  if (strstr(message, "\"command\":\"setbreakpoint\"") == NULL) {
    return false;
  }
  if (strstr(message, "\"type\":\"") == NULL) {
    return false;
  }
  cout << __FUNCTION__ << " Message dump: " << message << endl;

  string msg(message);
  rapidjson::Document doc;
  if (doc.Parse(msg.c_str()).HasParseError()) {
      cerr << "Failed to parse v8 debug JSON response" << endl;
  }

  assert(doc.IsObject());
  {
      rapidjson::Value& line = doc["body"]["line"];
      rapidjson::Value& column = doc["body"]["column"];
      char buf[10];
      // TODO: more error checking or using a safe wrapper on top of
      // standard sprintf
      sprintf(buf, "%d:%d", line.GetInt(), column.GetInt());
      set_breakpoint_result.assign(buf);
  }
  return true;
}

static void DebugSetBreakpointHandler(const v8::Debug::Message& message) {
  v8::Local<v8::String> json = message.GetJSON();
  v8::String::Utf8Value utf8(json);

  SetBreakpointResult(*utf8);
}

void ContinueResult(char* message) {
  if (strstr(message, "\"command\":\"continue\"") == NULL) {
    return;
  }
  cout << __FUNCTION__ << " Message dump: " << message << endl;

  string msg(message);
  rapidjson::Document doc;
  if (doc.Parse(msg.c_str()).HasParseError()) {
      cerr << "Failed to parse v8 debug JSON response" << endl;
  }

  assert(doc.IsObject());
  {
      rapidjson::Value& request_seq = doc["request_seq"];
      char buf[20];
      // TODO: more error checking or using a safe wrapper on top of
      // standard sprintf
      sprintf(buf, "request_seq: %d", request_seq.GetInt());
      continue_result.assign(buf);
  }
  cout << __FUNCTION__ << __LINE__ << " " << continue_result << endl;
}

static void DebugContinueHandler(const v8::Debug::Message& message) {
  v8::Local<v8::String> json = message.GetJSON();
  v8::String::Utf8Value utf8(json);

  ContinueResult(*utf8);
}

void ClearBreakpointResult(char* message) {
  if (strstr(message, "\"command\":\"clearbreakpoint\"") == NULL) {
    return;
  }
  cout << __FUNCTION__ << " Message dump: " << message << endl;
  if (strstr(message, "\"message\":\"Error\"") == NULL) {
    // Sample error message
    /* {"seq":1,
     * "request_seq":239,
     * "type":"response",
     * "command":"clearbreakpoint",
     * "success":false,
     * "message":"Error: Debugger: Invalid breakpoint",
     * "running":true}*/
    return;
  }

  string msg(message);
  rapidjson::Document doc;
  if (doc.Parse(msg.c_str()).HasParseError()) {
      cerr << "Failed to parse v8 debug JSON response" << endl;
  }

  assert(doc.IsObject());
  {
      rapidjson::Value& type = doc["body"]["type"];
      rapidjson::Value& breakpoints_cleared = doc["body"]["breakpoint"];
      char buf[40];
      // TODO: more error checking or using a safe wrapper on top of
      // standard sprintf
      sprintf(buf, "type:%s breakpoints_cleared: %d",
              type.GetString(), breakpoints_cleared.GetInt());
      clear_breakpoint_result.assign(buf);
  }
  return;
}

static void DebugClearBreakpointHandler(const v8::Debug::Message& message) {
  v8::Local<v8::String> json = message.GetJSON();
  v8::String::Utf8Value utf8(json);

  ClearBreakpointResult(*utf8);
}

void ListBreakpointResult(char* message) {
  if (strstr(message, "\"command\":\"listbreakpoints\"") == NULL) {
    return;
  }
  cout << __FUNCTION__ << " Message dump: " << message << endl;
  if (strstr(message, "\"message\":\"Error\"") == NULL) {
    // Sample error message
    /* {"seq":1,
     * "request_seq":239,
     * "type":"response",
     * "command":"clearbreakpoint",
     * "success":false,
     * "message":"Error: Debugger: Invalid breakpoint",
     * "running":true}*/
    return;
  }

  string msg(message);
  rapidjson::Document doc;
  if (doc.Parse(msg.c_str()).HasParseError()) {
      cerr << "Failed to parse v8 debug JSON response" << endl;
  }

  assert(doc.IsObject());

  list_breakpoint_result.assign(message);

  return;
}

static void DebugListBreakpointHandler(const v8::Debug::Message& message) {
  v8::Local<v8::String> json = message.GetJSON();
  v8::String::Utf8Value utf8(json);

  ListBreakpointResult(*utf8);
}

static void op_get_callback(lcb_t instance, int cbtype, const lcb_RESPBASE *rb) {
    const lcb_RESPGET *resp = reinterpret_cast<const lcb_RESPGET*>(rb);
    Result *result = reinterpret_cast<Result*>(rb->cookie);

    result->status = resp->rc;
    result->cas = resp->cas;
    result->itmflags = resp->itmflags;
    result->value.clear();

    if (resp->rc == LCB_SUCCESS) {
        result->value.assign(
                reinterpret_cast<const char*>(resp->value),
                resp->nvalue);
    } else {
        cerr << "lcb get failed with error " << lcb_strerror(instance, resp->rc) << endl;
    }
}

static void op_set_callback(lcb_t instance, int cbtype, const lcb_RESPBASE *rb) {
    // cerr << "lcb set response code: " << lcb_strerror(instance, rb->rc) << endl;
}


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

lcb_t* UnwrapWorkerLcbInstance(Local<Object> obj) {
    Local<External> field = Local<External>::Cast(obj->GetInternalField(2));
    void *ptr = field->Value();
    return static_cast<lcb_t*>(ptr);
}

Worker* UnwrapWorkerInstance(Local<Object> obj) {
    Local<External> field = Local<External>::Cast(obj->GetInternalField(1));
    void* ptr = field->Value();
    return static_cast<Worker*>(ptr);
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

string ConvertToISO8601(string timestamp) {
  char buf[sizeof "2016-08-09T10:11:12"];
  string buf_s;
  time_t now;

  int timerValue = atoi(timestamp.c_str());

  // Expiry timers more than 30 days will mention epoch
  // otherwise it will mention seconds from when key
  // was set
  if (timerValue > 25920000) {
    now = timerValue;
    strftime(buf, sizeof buf, "%FT%T", gmtime(&now));
    buf_s.assign(buf);
  } else {
    time(&now);
    now += timerValue;
    strftime(buf, sizeof buf, "%FT%T", gmtime(&now));
    buf_s.assign(buf);
  }
  return buf_s;
}

void RegisterCallback(const FunctionCallbackInfo<Value>& args) {
  HandleScope handle_scope(args.GetIsolate());
  String::Utf8Value callbackFuncName(args[0]);
  String::Utf8Value documentID(args[1]);
  String::Utf8Value startTimestamp(args[2]);

  // Store a blob in KV store, blob structure:
  // {
  //    "callback_func": CallbackFunc1,
  //    "document_id": docid,
  //    "start_timestamp": timestamp
  // }
  string callback_func, doc_id, startTs, timestamp, value;
  callback_func.assign(string(*callbackFuncName));
  doc_id.assign(string(*documentID));
  startTs.assign(string(*startTimestamp));

  // If the doc not supposed to expire, skip
  // setting up timer callback for it
  if (atoi(startTs.c_str()) == 0) {
      fprintf(stdout,
              "Skipping timer callback setup for doc_id: %s doc won't expire\n",
              doc_id.c_str());
      return;
  }

  timestamp = ConvertToISO8601(startTs);

  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);

  writer.StartObject();

  writer.Key("callback_func");
  writer.String(callback_func.c_str(), callback_func.length());
  writer.Key("doc_id");
  writer.String(doc_id.c_str(), doc_id.length());
  writer.Key("start_timestamp");
  writer.String(timestamp.c_str(), timestamp.length());

  writer.EndObject();

  value.assign(s.GetString());

  lcb_t* bucket_cb_handle = reinterpret_cast<lcb_t*>(args.GetIsolate()->GetData(1));

  lcb_CMDSTORE scmd = { 0 };
  LCB_CMD_SET_KEY(&scmd, doc_id.c_str(), doc_id.length());
  LCB_CMD_SET_VALUE(&scmd, value.c_str(), value.length());
  scmd.operation = LCB_SET;
  scmd.flags = 0x2000000;

  lcb_sched_enter(*bucket_cb_handle);
  lcb_store3(*bucket_cb_handle, NULL, &scmd);
  lcb_sched_leave(*bucket_cb_handle);
  lcb_wait(*bucket_cb_handle);

  // Append doc_id to key that keeps tracks of doc_ids for which
  // callbacks need to be triggered at any given point in time

  Result result;
  lcb_CMDGET gcmd = { 0 };
  LCB_CMD_SET_KEY(&gcmd, timestamp.c_str(), timestamp.length());
  lcb_sched_enter(*bucket_cb_handle);
  lcb_get3(*bucket_cb_handle, &result, &gcmd);
  lcb_sched_leave(*bucket_cb_handle);
  lcb_wait(*bucket_cb_handle);

  if (result.status != LCB_SUCCESS) {
    // LCB_ADD to KV
    // TODO: error catching by setting up store_callback
    string timestamp_marker("");
    lcb_CMDSTORE acmd = { 0 };
    LCB_CMD_SET_KEY(&acmd, timestamp.c_str(), timestamp.length());
    LCB_CMD_SET_VALUE(&acmd, timestamp_marker.c_str(), timestamp_marker.length());
    acmd.operation = LCB_ADD;

    lcb_sched_enter(*bucket_cb_handle);
    lcb_store3(*bucket_cb_handle, NULL, &acmd);
    lcb_sched_leave(*bucket_cb_handle);
    lcb_wait(*bucket_cb_handle);
  }

  // appending delimiter ";"
  doc_id.append(";");
  lcb_CMDSTORE cmd = { 0 };
  lcb_IOV iov[2];
  cmd.operation = LCB_APPEND;
  iov[0].iov_base = (void *)doc_id.c_str();
  iov[0].iov_len = doc_id.length();

  LCB_CMD_SET_VALUEIOV(&cmd, iov, 1);
  LCB_CMD_SET_KEY(&cmd, timestamp.c_str(), timestamp.length());
  lcb_sched_enter(*bucket_cb_handle);
  lcb_store3(*bucket_cb_handle, NULL, &cmd);
  lcb_sched_leave(*bucket_cb_handle);
  lcb_wait(*bucket_cb_handle);
}

void PostMail(char* app_name, char* to, char* subject, char* body) {
  CURL* curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:6063/sendmail/");
    char buf[1000];
    sprintf(buf, "app_name=%s&to=%s&subject=%s&body=%s",
            app_name, to, subject, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buf);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));

    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
}

void SendMail(const FunctionCallbackInfo<Value>& args) {
  string app_name, mail_to, mail_subject, mail_body;
  Worker* w = NULL;

  {
    Isolate* isolate = args.GetIsolate();
    w = static_cast<Worker*>(isolate->GetData(0));
    assert(w->GetIsolate() == isolate);

    Locker locker(w->GetIsolate());
    HandleScope handle_Scope(isolate);

    Local<Context> context = Local<Context>::New(w->GetIsolate(), w->context_);
    Context::Scope context_scope(context);

    Local<Value> m_to = args[0];
    assert(m_to->IsString());
    String::Utf8Value s_m_to(m_to);
    mail_to = ToCString(s_m_to);

    Local<Value> m_subject = args[1];
    assert(m_subject->IsString());
    String::Utf8Value s_m_subject(m_subject);
    mail_subject = ToCString(s_m_subject);

    Local<Value> m_body = args[2];
    assert(m_body->IsString());
    String::Utf8Value s_m_body(m_body);
    mail_body = ToCString(s_m_body);

    app_name.assign(w->app_name_);
  }
  PostMail((char*)app_name.c_str(),
           (char*)mail_to.c_str(),
           (char*)mail_subject.c_str(),
           (char*)mail_body.c_str());
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

vector<string> &split(const string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

vector<string> split(const string &s, char delim) {
    vector<string> elems;
    split(s, delim, elems);
    return elems;
}

Worker::Worker(int tindex, const char* app_name) {
  Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;
  isolate_ = Isolate::New(create_params);
  Locker locker(isolate_);
  Isolate::Scope isolate_scope(isolate_);
  HandleScope handle_scope(isolate_);

  isolate_->SetCaptureStackTraceForUncaughtExceptions(true);
  isolate_->SetData(0, this);
  table_index = tindex;
  Local<ObjectTemplate> global = ObjectTemplate::New(GetIsolate());

  TryCatch try_catch;

  global->Set(String::NewFromUtf8(GetIsolate(), "log"),
              FunctionTemplate::New(GetIsolate(), Print));
  global->Set(String::NewFromUtf8(GetIsolate(), "registerCallback"),
              FunctionTemplate::New(GetIsolate(), RegisterCallback));
  global->Set(String::NewFromUtf8(GetIsolate(), "sendmail"),
              FunctionTemplate::New(GetIsolate(), SendMail));
  if(try_catch.HasCaught()) {
    last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("ERROR Print exception: %s\n", last_exception.c_str());
  }

  string v8_debug_flag("--expose-debug-as=debug");

  V8::SetFlagsFromString(v8_debug_flag.c_str(), v8_debug_flag.length());

  Local<Context> context = Context::New(GetIsolate(), NULL, global);
  context_.Reset(GetIsolate(), context);

  app_name_ = app_name;
  deployment_config* result = ParseDeployment(app_name);

  cb_cluster_endpoint.assign(result->source_endpoint);
  cb_cluster_bucket.assign(result->source_bucket);

 //context->Enter();

  map<string, map<string, vector<string> > >::iterator it = result->component_configs.begin();

  for (; it != result->component_configs.end(); it++) {

      if (it->first == "buckets") {
          map<string, vector<string> >::iterator bucket = result->component_configs["buckets"].begin();
          for (; bucket != result->component_configs["buckets"].end(); bucket++) {
            string bucket_alias = bucket->first;
            string bucket_name = result->component_configs["buckets"][bucket_alias][0];
            string endpoint(cb_cluster_endpoint);

            b = new Bucket(this,
                           bucket_name.c_str(),
                           endpoint.c_str(),
                           bucket_alias.c_str());
          }
      }

      n = new N1QL(this,
                   cb_cluster_bucket.c_str(),
                   cb_cluster_endpoint.c_str(),
                   "_n1ql");

      if (it->first == "queue") {
          map<string, vector<string> >::iterator queue = result->component_configs["queue"].begin();
          for (; queue != result->component_configs["queue"].end(); queue++) {
            string provider = queue->first;
            string endpoint = result->component_configs["queue"][provider][1];
            string queue_alias = result->component_configs["queue"][provider][2];
            string queue_name = result->component_configs["queue"][provider][3];

            q = new Queue(this,
                          provider.c_str(),
                          endpoint.c_str(),
                          queue_alias.c_str(),
                          queue_name.c_str());
          }

      }
  }
  r = new HTTPResponse(this);

  // Register a lcb_t handle for storing timer based callbacks in CB
  // TODO: Fix the hardcoding i.e. allow customer to create
  // bucket with any name and it should be picked from config file

  string connstr = "couchbase://" + cb_cluster_endpoint + "/" + result->metadata_bucket.c_str();

  delete result;
  // lcb related setup
  lcb_create_st crst;
  memset(&crst, 0, sizeof crst);

  crst.version = 3;
  crst.v.v3.connstr = connstr.c_str();

  lcb_create(&cb_instance, &crst);
  lcb_connect(cb_instance);
  lcb_wait(cb_instance);

  lcb_install_callback3(cb_instance, LCB_CALLBACK_GET, op_get_callback);
  lcb_install_callback3(cb_instance, LCB_CALLBACK_STORE, op_set_callback);

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

  string temp, script_to_execute;
  string content, builtin_functions;
  LoadBuiltins(&builtin_functions);

  content.assign(source_s);

  // Preprocessor for allowing queue operations
  std::regex enqueue("(enqueue\\((.*)\\, (.*)\\)))");
  std::smatch queue_m;

  while (std::regex_search(content, queue_m, enqueue)) {
      temp += queue_m.prefix();
      temp += queue_m[2].str();
      temp += "[";
      temp += queue_m[3].str();
      temp += "]";
      content = queue_m.suffix();
  }
  temp += content;

  // TODO: Figure out if there is a cleaner way to do preprocessing for n1ql
  // Converting n1ql("<query>") to tagged template literal i.e. n1ql`<query>`
  std::regex n1ql_ttl("(n1ql\\(\")(.*)(\"\\))");
  std::smatch n1ql_m;

  while (std::regex_search(temp, n1ql_m, n1ql_ttl)) {
      script_to_execute += n1ql_m.prefix();
      std::regex re_prefix("n1ql\\(\"");
      std::regex re_suffix("\"\\)");
      script_to_execute += std::regex_replace(n1ql_m[1].str(),
                                              re_prefix, "n1ql`");
      script_to_execute += n1ql_m[2].str();
      script_to_execute += std::regex_replace(n1ql_m[3].str(),
                                              re_suffix, "`");
      temp = n1ql_m.suffix();
  }
  script_to_execute += temp;
  script_to_execute.append(builtin_functions);

  Local<String> name = String::NewFromUtf8(GetIsolate(), name_s);
  Local<String> source = String::NewFromUtf8(GetIsolate(),
                                             script_to_execute.c_str());

  script_to_execute_ = script_to_execute;
  // cout << "script to execute: " << script_to_execute << endl;

  if (!ExecuteScript(source))
      return FAILED_TO_COMPILE_JS;

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

  Local<Value> on_update_val;
  Local<Value> on_delete_val;
  Local<Value> on_http_get_val;
  Local<Value> on_http_post_val;

  if (!context->Global()->Get(context, on_update).ToLocal(&on_update_val) ||
      !context->Global()->Get(context, on_delete).ToLocal(&on_delete_val) ||
      !context->Global()->Get(context, on_http_get).ToLocal(&on_http_get_val) ||
      !context->Global()->Get(context, on_http_post).ToLocal(&on_http_post_val) ||
      !on_update_val->IsFunction() ||
      !on_delete_val->IsFunction() ||
      !on_http_get_val->IsFunction() ||
      !on_http_post_val->IsFunction()) {
      return NO_HANDLERS_DEFINED;
  }

  Local<Function> on_update_fun = Local<Function>::Cast(on_update_val);
  on_update_.Reset(GetIsolate(), on_update_fun);

  Local<Function> on_delete_fun = Local<Function>::Cast(on_delete_val);
  on_delete_.Reset(GetIsolate(), on_delete_fun);

  Local<Function> on_http_get_fun = Local<Function>::Cast(on_http_get_val);
  on_http_get_.Reset(GetIsolate(), on_http_get_fun);

  Local<Function> on_http_post_fun = Local<Function>::Cast(on_http_post_val);
  on_http_post_.Reset(GetIsolate(), on_http_post_fun);

  //TODO: return proper exit codes
  if (!b->Initialize(this, &bucket)) {
    cerr << "Error initializing bucket handler" << endl;
    return FAILED_INIT_BUCKET_HANDLE;
  }
  if (!n->Initialize(this, &n1ql)) {
    cerr << "Error initializing n1ql handler" << endl;
    return FAILED_INIT_N1QL_HANDLE;
  }
  if (!q->Initialize(this, &queue)) {
    cerr << "Error initializing queue handler" << endl;
    return FAILED_INIT_QUEUE_HANDLE;
  }

  // Wrap around the lcb handle into v8 isolate
  this->GetIsolate()->SetData(1, (void *)(&cb_instance));
  return SUCCESS;
}

bool Worker::ExecuteScript(Local<String> script) {
  HandleScope handle_scope(GetIsolate());

  TryCatch try_catch(GetIsolate());

  Local<Context> context(GetIsolate()->GetCurrentContext());

  Local<Script> compiled_script;
  if (!Script::Compile(context, script).ToLocal(&compiled_script)) {
    assert(try_catch.HasCaught());
    last_exception = ExceptionString(GetIsolate(), &try_catch);
    // printf("Logged: %s\n", last_exception.c_str());
    // The script failed to compile; bail out.
    return false;
  }

  Local<Value> result;
  if (!compiled_script->Run(context).ToLocal(&result)) {
    assert(try_catch.HasCaught());
    last_exception = ExceptionString(GetIsolate(), &try_catch);
    // printf("Logged: %s\n", last_exception.c_str());
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
    last_exception = ExceptionString(GetIsolate(), &try_catch);
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
    last_exception = ExceptionString(GetIsolate(), &try_catch);
    printf("Logged: %s\n", last_exception.c_str());
  }

  Local<Function> on_http_post = Local<Function>::New(GetIsolate(), on_http_post_);

  on_http_post->Call(context->Global(), 2, args);

  return this->r->ConvertMapToJson();
}

void Worker::SendTimerCallback(const char* k) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  vector<string> keys = split(k, ';');

  for (auto key : keys) {
    Result result;
    lcb_CMDGET gcmd = { 0 };
    LCB_CMD_SET_KEY(&gcmd, key.c_str(), key.length());
    lcb_sched_enter(cb_instance);
    lcb_get3(cb_instance, &result, &gcmd);
    lcb_sched_leave(cb_instance);
    lcb_wait(cb_instance);

    rapidjson::Document doc;
    if (doc.Parse(result.value.c_str()).HasParseError()) {
        return;
    }

    string callback_func, doc_id, start_timestamp;
    assert(doc.IsObject());
    {
        rapidjson::Value& cf = doc["callback_func"];
        rapidjson::Value& id = doc["doc_id"];
        rapidjson::Value& sts = doc["start_timestamp"];

        callback_func.assign(cf.GetString());
        doc_id.assign(id.GetString());
        start_timestamp.assign(sts.GetString());
    }

    // TODO: check for anonymous JS functions. Disallow them completely
    Handle<Value> val = context->Global()->Get(
            createUtf8String(GetIsolate(), callback_func.c_str()));
    Handle<Function> cb_func = Handle<Function>::Cast(val);

    Handle<Value> arg[1];
    arg[0] = String::NewFromUtf8(GetIsolate(), doc_id.c_str());

    cb_func->Call(context->Global(), 1, arg);
  }
}

const char* Worker::SendContinueRequest(const char* command) {
  const int kBufferSize = 1000;
  uint16_t buffer[kBufferSize];

  Debug::SetMessageHandler(GetIsolate(), DebugContinueHandler);
  Debug::SendCommand(GetIsolate(), buffer, AsciiToUtf16(command, buffer));

  return continue_result.c_str();
}

const char* Worker::SendEvaluateRequest(const char* command) {
  const int kBufferSize = 1000;
  uint16_t buffer[kBufferSize];

  Debug::SetMessageHandler(GetIsolate(), DebugEvaluateHandler);
  Debug::SendCommand(GetIsolate(), buffer, AsciiToUtf16(command, buffer));

  return evaluate_result.c_str();
}

const char* Worker::SendLookupRequest(const char* request) {
    return "";
}

const char* Worker::SendBacktraceRequest(const char* request) {
    return "";
}

const char* Worker::SendFrameRequest(const char* request) {
    return "";
}

const char* Worker::SendSourceRequest(const char* request) {
    return "";
}

const char* Worker::SendSetBreakpointRequest(const char* command) {
  const int kBufferSize = 1000;
  uint16_t buffer[kBufferSize];

  Debug::SetMessageHandler(GetIsolate(), DebugSetBreakpointHandler);
  Debug::SendCommand(GetIsolate(), buffer, AsciiToUtf16(command, buffer));

  return set_breakpoint_result.c_str();
}

const char* Worker::SendClearBreakpointRequest(const char* command) {
  const int kBufferSize = 1000;
  uint16_t buffer[kBufferSize];

  Debug::SetMessageHandler(GetIsolate(), DebugClearBreakpointHandler);
  Debug::SendCommand(GetIsolate(), buffer, AsciiToUtf16(command, buffer));

  return clear_breakpoint_result.c_str();
}

const char* Worker::SendListBreakpointsRequest(const char* command) {
  const int kBufferSize = 1000;
  uint16_t buffer[kBufferSize];

  Debug::SetMessageHandler(GetIsolate(), DebugListBreakpointHandler);
  Debug::SendCommand(GetIsolate(), buffer, AsciiToUtf16(command, buffer));

  return list_breakpoint_result.c_str();
}

int Worker::SendUpdate(const char* value, const char* meta, const char* type ) {
  Locker locker(GetIsolate());
  Isolate::Scope isolate_scope(GetIsolate());
  HandleScope handle_scope(GetIsolate());

  Local<Context> context = Local<Context>::New(GetIsolate(), context_);
  Context::Scope context_scope(context);

  // cout << "value: " << value << " meta: " << meta << " type: " << type << endl;
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
    last_exception = ExceptionString(GetIsolate(), &try_catch);
    fprintf(stderr, "Logged: %s\n", last_exception.c_str());
    fflush(stderr);
  }

  Local<Function> on_doc_update = Local<Function>::New(GetIsolate(), on_update_);
  on_doc_update->Call(context->Global(), 2, args);
  Debug::ProcessDebugMessages(GetIsolate());

  if (try_catch.HasCaught()) {
    cout << "Exception message: "
         <<  ExceptionString(GetIsolate(), &try_catch) << endl;
    return ON_UPDATE_CALL_FAIL;
  }

  return SUCCESS;
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
  Debug::ProcessDebugMessages(GetIsolate());

  if (try_catch.HasCaught()) {
    //last_exception = ExceptionString(GetIsolate(), &try_catch);
    return ON_DELETE_CALL_FAIL;
  }

  return SUCCESS;
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

worker* worker_new(int table_index, const char* app_name) {
  worker* wrkr = (worker*)malloc(sizeof(worker));
  wrkr->w = new Worker(table_index, app_name);
  return wrkr;
}

const char* worker_last_exception(worker* w) {
    return w->w->WorkerLastException();
}

int worker_load(worker* w, char* name_s, char* source_s) {
    return w->w->WorkerLoad(name_s, source_s);
}

void worker_send_timer_callback(worker* w, const char* keys) {
    w->w->SendTimerCallback(keys);
}

const char* worker_send_continue_request(worker* w, const char* request) {
    return w->w->SendContinueRequest(request);
}

const char* worker_send_evaluate_request(worker* w, const char* request) {
    return w->w->SendEvaluateRequest(request);
}

const char* worker_send_lookup_request(worker* w, const char* request) {
    return w->w->SendLookupRequest(request);
}

const char* worker_send_backtrace_request(worker* w, const char* request) {
    return w->w->SendBacktraceRequest(request);
}

const char* worker_send_frame_request(worker* w, const char* request) {
    return w->w->SendFrameRequest(request);
}

const char* worker_send_source_request(worker* w, const char* request) {
    return w->w->SendSourceRequest(request);
}

const char* worker_send_setbreakpoint_request(worker* w, const char* request) {
    return w->w->SendSetBreakpointRequest(request);
}

const char* worker_send_clearbreakpoint_request(worker* w, const char* request) {
    return w->w->SendClearBreakpointRequest(request);
}

const char* worker_send_listbreakpoints_request(worker* w, const char* request) {
    return w->w->SendListBreakpointsRequest(request);
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
