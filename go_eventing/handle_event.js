function OnUpdate(doc, meta) {
  log("doc meta id: ", meta.key);

  if (meta.type === "json" && doc.ssn) {
    log("doc.ssn field: ", doc.ssn);

    updated_doc = CalculateCreditScore(doc)
    credit_bucket[meta.key] = updated_doc;

    var value = credit_bucket[meta.key];

    //delete credit_bucket[meta.key];

    var d = new Date();
    var n = ISODateString(d);
    log("ISO 8601: ", n);

    registerCallback("CallbackFunc1", meta.key, n);
    enqueue(order_queue, meta.key);
  }
}

function CallbackFunc1(doc_id) {
    log("DocID recieved by callback: ", doc_id);
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

  if (req.path === "get_beer_count") {

    var n1qlResult = n1ql("select count(*) from ${bucket}");
    res["beer_sample_count"] = n1qlResult;

  } else if (req.path === "get_breweries_in_sf") {

    var city = "San Francisco";
    var n1qlResult = n1ql("select count(*) from ${bucket} where ${bucket}.city == '${city}'");
    res.breweries_sf_count = n1qlResult;

  } else if (req.path === "get_brewery_in_cali") {

      var state = "California";
      var limit = 1;
      var n1qlResult = n1ql("select * from ${bucket} where ${bucket}.state == '${state}' limit ${limit};");
      res.brewery_in_cali = n1qlResult;
      res.query_outpt_row_count = n1qlResult.length;

  } else {
      res.http_status_code = 404;
  }
}

function OnHTTPPost(req, res) {

  if (req.path === "book_tickets") {

    var user_id = req.params.user_id;
    var src_city = req.params.src;
    var dst_city = req.params.dst;

    var booking_id = "book_" + (Math.floor(Math.random() * 10000) + 10).toString();
    var booking_blob = {"booking_id": booking_id, "src_city": src_city,
                        "dst_city": dst_city, "user_id": user_id};

    credit_bucket[booking_id] = booking_blob;

    var user_blob = credit_bucket[user_id];
    user_blob.booking_ids.push(booking_id);

    credit_bucket[user_id] = user_blob;

    res.booking_id = booking_id;
    res.user_id = user_id;
  }

}

function OnTimerEvent() {

}

function CalculateCreditScore(doc) {
  var credit_score = 500;

  if (doc.credit_limit_used/doc.total_credit_limit < 0.3) {
      credit_score = credit_score + 20;
  } else {
      doc.credit_score = doc.credit_score -
                        Math.floor((doc.credit_limit_used/doc.total_credit_limit) * 20);
  }

  if (doc.missed_emi_payments !== 0) {
      credit_score = credit_score - doc.missed_emi_payments * 30;
  }

  if (credit_score < 300) {
      doc.credit_score = 300;
  } else {
      doc.credit_score = credit_score;
  }

  return doc;
}
