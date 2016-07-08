/* Triggered when opcode DCP_MUTATION is encountered
* Getting mutations from default bucket
* writing KV pairs to "beer_sample" bucket
*/
function OnUpdate(doc, meta) {
  log("doc id: ", meta.key);

  if (meta.type === "json") {
    log("doc.uuid field: ", doc.uuid);

    //KV ops
    credit_bucket[doc.uuid] = JSON.stringify(doc);
    var value = credit_bucket[doc.uuid];
    log("value received from bucket get call: ", value);
    delete credit_bucket[doc.uuid];

    // Queue operations:
    doc.processed = true;
    doc.send_email = true;

    // Enqueue to a queue
    beanstalk_enqueue(JSON.stringify(doc));
  }
}

/*
 * Triggered when opcode DCP_DELETION is encountered
 */
function OnDelete(msg) {
  log("Got from onDelete Golang world", msg);

  // N1QL operations
  var bucket = "beer-sample";
  var limit = 10;
  var type = "brewery";

  // Support for both TTL based syntax and typical parenthesis enclosed query
  // n1ql`<query>` or n1ql("<query>")

  //var n1qlResult = n1ql`select ${bucket}.name from ${bucket} where ${bucket}.type == '${type}' limit ${limit}`;
  var n1qlResult = n1ql("select ${bucket}.name from ${bucket} where ${bucket}.type == '${type}' limit ${limit}");
  var n1qlResultLength = n1qlResult.length;
  for (i = 0; i < n1qlResultLength; i++) {
      log("OnDelete: n1ql query response row: ", n1qlResult[i]);
  }
}

function OnHTTPGet(req, res) {
  var bucket = "beer-sample";

  if (req.path === "/get_beer_count") {
    var n1qlResult = n1ql("select count(*) from ${bucket}");
    res["beer_sample_count"] = n1qlResult;
    res["http_status_code"] = 200;
  } else if (req.path === "/get_breweries_in_sf") {
    var city = "San Francisco";
    var n1qlResult = n1ql("select count(*) from ${bucket} where ${bucket}.city == '${city}'");
    res.breweries_sf_count = n1qlResult;
    res.http_status_code = 200;
  } else {
      res.http_status_code = 404;
  }
}

function OnHTTPPost(req, res) {

}

function OnTimerEvent() {

}
