## Couchbase Eventing:
==================

Effort to embed application server layer within Couchbase


#### Next set of items to finish:
===========================

* NamedPropertyHandlerConfiguration interceptor functions need to be static, this puts a lot of
  limitation in current design. Could use Local<Value> to wrap pointer with External::New()
  and unwrap in callbacks

* Make sure bad javascript doesn’t crash the eventing process

* Make couchbase bucket connection object non-static

* Add TryCatch in more places within v8 binding code to trace failures,
  along with some assert calls.

* Convert unnecessary public variables in different classes to private and
  set up getter methods

* Move back to libcouchbase C SDK

* Store cas value of mutations in-memory on each eventing node,
  in order to make callbacks idempotent

* support for queue, which might require code injection

* Design in-memory structure in Golang that would store the cas value

* support for timer events which would help in getting notification on
  document expiration or a recurring event

* Support for multiple bucket/queue/n1ql/view definitions in deployment.cfg

* Use clang-tidy to standardize code formatting all over the place

* Handle error messages gracefully:
  * From SendUpdate/SendDelete methods
