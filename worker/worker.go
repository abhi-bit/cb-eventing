package worker

/*
#cgo LDFLAGS: -L/Users/asingh/.cbdepscache/lib/ -lv8_binding
#include <stdlib.h>
#include "binding/binding.h"
*/
import "C"
import "errors"

import (
	"runtime"
	"sync"
	"unsafe"
)

type workerTableIndex int

var workerTableLock sync.Mutex

// This table will store all pointers to all active workers. Because we can't safely
// pass pointers to Go objects to C, we instead pass a key to this table.
var workerTable = make(map[workerTableIndex]*worker)

// Keeps track of the last used table index. Incremeneted when a worker is created.
var workerTableNextAvailable workerTableIndex

// Don't init V8 more than once.
var initV8Once sync.Once

// Internal worker struct which is stored in the workerTable.
// Weak-ref pattern https://groups.google.com/forum/#!topic/golang-nuts/1ItNOOj8yW8/discussion
type worker struct {
	cWorker    *C.worker
	tableIndex workerTableIndex
}

// Worker - Golang wrapper around a single V8 Isolate.
type Worker struct {
	*worker
	disposed bool
	Quit     chan string
}

// Version - Returns the V8 version E.G. "4.3.59"
func Version() string {
	return C.GoString(C.worker_version())
}

func workerTableLookup(index workerTableIndex) *worker {
	workerTableLock.Lock()
	defer workerTableLock.Unlock()
	return workerTable[index]
}

// New creates a new worker, which corresponds to a V8 isolate. A single threaded
// standalone execution context.
func New(aName string) *Worker {
	workerTableLock.Lock()
	w := &worker{
		tableIndex: workerTableNextAvailable,
	}

	workerTableNextAvailable++
	workerTable[w.tableIndex] = w
	workerTableLock.Unlock()

	initV8Once.Do(func() {
		C.v8_init()
	})

	appName := C.CString(aName)
	defer C.free(unsafe.Pointer(appName))
	w.cWorker = C.worker_new(C.int(w.tableIndex), appName)

	externalWorker := &Worker{
		worker:   w,
		disposed: false,
	}

	runtime.SetFinalizer(externalWorker, func(final_worker *Worker) {
		final_worker.Dispose()
	})
	return externalWorker
}

// Dispose forcefully frees up memory associated with worker.
// GC will also free up worker memory so calling this isn't strictly necessary.
func (w *Worker) Dispose() {
	if w.disposed {
		panic("worker already disposed")
	}
	w.disposed = true
	workerTableLock.Lock()
	internalWorker := w.worker
	delete(workerTable, internalWorker.tableIndex)
	workerTableLock.Unlock()
	C.worker_dispose(internalWorker.cWorker)
}

// Load and executes a javascript file with the filename specified by
// scriptName and the contents of the file specified by the param code.
func (w *Worker) Load(sName string, codeString string) error {
	scriptName := C.CString(sName)
	code := C.CString(codeString)
	defer C.free(unsafe.Pointer(scriptName))
	defer C.free(unsafe.Pointer(code))

	r := C.worker_load(w.worker.cWorker, scriptName, code)
	if r != 0 {
		errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
		return errors.New(errStr)
	}
	return nil
}

// SendDelete sends DCP_DELETION mutation to v8
func (w *Worker) SendDelete(m string) error {
	msg := C.CString(m)
	defer C.free(unsafe.Pointer(msg))

	r := C.worker_send_delete(w.worker.cWorker, msg)
	if r != 0 {
		errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
		return errors.New(errStr)
	}

	return nil
}

// SendUpdate sends DCP_MUTATION to v8
func (w *Worker) SendUpdate(v string, m string, t string) error {
	value := C.CString(v)
	defer C.free(unsafe.Pointer(value))
	meta := C.CString(m)
	defer C.free(unsafe.Pointer(meta))
	docType := C.CString(t)
	defer C.free(unsafe.Pointer(docType))

	r := C.worker_send_update(w.worker.cWorker, value, meta, docType)
	if r != 0 {
		errStr := C.GoString(C.worker_last_exception(w.worker.cWorker))
		return errors.New(errStr)
	}
	return nil
}

// TerminateExecution terminates execution of javascript
func (w *Worker) TerminateExecution() {
	C.worker_terminate_execution(w.worker.cWorker)
}

// SendHTTPGet sends http GET request to JS world
func (w *Worker) SendHTTPGet(r string) string {
	req := C.CString(r)
	defer C.free(unsafe.Pointer(req))

	res := C.worker_send_http_get(w.worker.cWorker, req)
	return C.GoString(res)
}

// SendHTTPPost sends http POST request to JS world
func (w *Worker) SendHTTPPost(r string) string {
	req := C.CString(r)
	defer C.free(unsafe.Pointer(req))

	res := C.worker_send_http_post(w.worker.cWorker, req)
	return C.GoString(res)
}

// SendTimerCallback send list of keys against which timed callbacks need to be triggered
func (w *Worker) SendTimerCallback(k string) {
	keys := C.CString(k)
	defer C.free(unsafe.Pointer(keys))

	C.worker_send_timer_callback(w.worker.cWorker, keys)
	return
}

// SendContinueRequest function
func (w *Worker) SendContinueRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_continue_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendEvaluateRequest function
func (w *Worker) SendEvaluateRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_evaluate_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendLookupRequest function
func (w *Worker) SendLookupRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_lookup_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendBacktraceRequest function
func (w *Worker) SendBacktraceRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_backtrace_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendFrameRequest function
func (w *Worker) SendFrameRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_frame_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendSourceRequest function
func (w *Worker) SendSourceRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_source_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendSetBreakpointRequest function
func (w *Worker) SendSetBreakpointRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_setbreakpoint_request(w.worker.cWorker, request)
	return C.GoString(res)
}

// SendClearBreakpointRequest function
func (w *Worker) SendClearBreakpointRequest(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_clearbreakpoint_request(w.worker.cWorker, request)
	return C.GoString(res)
}

//SendListBreakpoints function
func (w *Worker) SendListBreakpoints(r string) string {
	request := C.CString(r)
	defer C.free(unsafe.Pointer(request))

	res := C.worker_send_listbreakpoints_request(w.worker.cWorker, request)
	return C.GoString(res)
}
