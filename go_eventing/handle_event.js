function OnUpdate(doc, meta) {
  log("doc meta id: ", meta.key);

  if (meta.type === "json") {
    log("doc.uuid field: ", doc.uuid);

    credit_bucket[doc.uuid] = doc;

    var value = credit_bucket[doc.uuid];

    delete credit_bucket[doc.uuid];

    var d = new Date();
    var n = ISODateString(d);
    log("ISO 8601: ", n);

    registerCallback("OnTimerEvent", meta.key, n);
    enqueue(order_queue, doc.uuid);
  }
}

function OnDelete(msg) {
  var bucket = "beer-sample";
  var limit = 5;
  var type = "brewery";

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

  } else if (req.path === "/get_breweries_in_sf") {

    var city = "San Francisco";
    var n1qlResult = n1ql("select count(*) from ${bucket} where ${bucket}.city == '${city}'");
    res.breweries_sf_count = n1qlResult;

  } else if (req.path === "/get_brewery_in_cali") {

      var state = "California";
      var limit = 10;
      var n1qlResult = n1ql("select * from ${bucket} where ${bucket}.state == '${state}' limit ${limit};");
      res.brewery_in_cali = n1qlResult;
      res.query_outpt_row_count = n1qlResult.length;

  } else {
      res.http_status_code = 404;
  }
}

function OnHTTPPost(req, res) {
  var bucket = "beer-sample";

  if (req.path === "/get_breweries_by_city") {

    var city = req.params.city;
    var n1qlResult = n1ql("select count(*) from ${bucket} where ${bucket}.city == '${city}'");
    res.brewery_count = n1qlResult;
    res.city = city

  } else if (req.path === "/get_breweries_by_state") {

      var state = req.params.state;
      var n1qlResult = n1ql("select count(*) from ${bucket} where ${bucket}.state == '${state}'");
      res.brewery_count = n1qlResult;
      res.state = state;

  } else {
      res.http_status_code = 404;
  }

}

function OnTimerEvent() {

}
