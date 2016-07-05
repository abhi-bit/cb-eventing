## Couchbase Eventing:

Effort to embed application server layer within Couchbase


#### Next set of items to finish:

* Detect if KV blob is json using meta flag and convert it to native js json.
  For non-json or binary blob users could leverage ArrayBuffers and they
  needn't to be converted to native js json
* code cleanup and making it more modular. Instead of using a single
  Cluster class, use Bucket, N1QL, Queue, View classes
* support for queue, which might require code injection
* support variable expansion in n1ql queries, right now project only
  support static n1ql queries. Might require dynamic code analysis
* Store cas value of mutations in-memory on each eventing node,
  in order to make callbacks idempotent
* support for timer events which would help in getting notification on
  document expiration or a recurring event
* callback support for httpget, sample callback might look like this:

```
 function OnHTTPGet(req, res) {
  if (req.uri === "/getbeers") {
    res.beer_count = 1000;
    res.brewery_location = "BLR"
    res.http_status_code = 200
  } else if (req.uri === "/getbreweries") {
    res.brewery_count = 10
    res.brewery_open_till = "23:00"
    res.http_status_code = 200
  } else {
    res.http_status_code = 404
  }
 }
```
note: we aren't return anything explicitly, v8 might need to inject some code
     here to make it work

* Support for multiple bucket/queue/n1ql/view definitions in deployment.cfg


#### Bugs:

* BucketSet isn't setting up document flags correctly i.e. even when it's setting a
  JSON blob in KV, doc flag is 0x0. Ideally it should be set to 0x2000006


