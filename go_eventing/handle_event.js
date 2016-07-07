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

    /*
    // Queue operations:
    obj.processed = true;
    obj.send_email = true;

    // Enqueue to a queue
    var id = beanstalk_queue[JSON.stringify(obj)];

    // Dequeues an entry from queue
    delete beanstalk_queue[id];
    */


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
        log("n1ql query response row: ", n1qlResult[i]);
    }
  }
}

/*
 * Triggered when opcode DCP_DELETION is encountered
 */
function OnDelete(msg) {
  var obj = JSON.parse(msg);
  log("Got from onDelete Golang world", msg);
}

function OnHTTPGet(req) {

}

function OnHTTPPost(req) {

}

function OnTimerEvent() {

}
