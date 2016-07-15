## Couchbase Eventing:
==================

Effort to embed application server layer within Couchbase


#### Next set of items to finish:
===========================

* Application reload when deployment.json is updated

* Support for Timer Callbacks, sample example:

```
function OnUpdate(doc, meta) {
  if (meta.id.Startswith(“pymc”)) {
    RegisterCallback(CallbackFunc1, meta.id);
  } else if (meta.id.Startswith(“defg”)) {
    RegisterCallback(CallbackFunc2, meta.id);
  } else {
    RegisterCallback(CallbackFunc3, meta.id);
  }
}

function CallbackFunc1(doc, meta) {
}
function CallbackFunc2(doc, meta) {
}
function CallbackFunc3(doc, meta) {
}
```

* Make sure bad javascript doesn’t crash the eventing process

* Add TryCatch in more places within v8 binding code to trace failures,
  along with some assert calls.

* Store cas value of mutations in-memory on each eventing node,
  in order to make callbacks idempotent

* Support for multiple bucket/queue/n1ql/view definitions in deployment.cfg

* Handle error messages gracefully:
  * From SendUpdate/SendDelete methods
