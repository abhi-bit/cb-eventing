#ifndef __BINDING_H__
#define __BINDING_H__

#ifdef __cplusplus
extern "C" {
#endif

struct worker_s;
typedef struct worker_s worker;

const char* worker_version();

void v8_init();

worker* worker_new(int table_index, const char* app_name);

int worker_load(worker* w, char* name_s, char* source_s);

const char* worker_last_exception(worker* w);

int worker_send_update(worker* w, const char* value,
                       const char* meta, const char* type);
int worker_send_delete(worker* w, const char* msg);

const char* worker_send_http_get(worker* w, const char* http_req);

const char* worker_send_http_post(worker* w, const char* http_req);

void worker_send_timer_callback(worker* w, const char* keys);

const char* worker_send_continue_request(worker* w, const char* request);
const char* worker_send_evaluate_request(worker* w, const char* request);
const char* worker_send_lookup_request(worker* w, const char* request);
const char* worker_send_backtrace_request(worker* w, const char* request);
const char* worker_send_frame_request(worker* w, const char* request);
const char* worker_send_source_request(worker* w, const char* request);
const char* worker_send_setbreakpoint_request(worker* w, const char* request);
const char* worker_send_clearbreakpoint_request(worker* w, const char* request);
const char* worker_send_listbreakpoints_request(worker* w, const char* request);

void worker_dispose(worker* w);
void worker_terminate_execution(worker* w);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
