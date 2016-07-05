/*function n1ql(strings, ...values) {
    var stringsLength = strings.length;
    var query = "";
    for (i = 0; i < stringsLength  - 1; i++) {
        query = query.concat(strings[i]);
        query = query.concat(values[i]);
    }

    return query;
    //return n1ql_blr_cluster[query];
}*/

/* Triggered when opcode DCP_MUTATION is encountered
* Getting mutations from default bucket
* writing KV pairs to "beer_sample" bucket
*/
function OnUpdate(msg) {
  var obj = JSON.parse(msg);
  var val = JSON.stringify(obj);

  //log(val);
  //KV ops

  credit_bucket[obj.name] = val;

  var value = credit_bucket[obj.name];

  delete credit_bucket[obj.name];

  /*
  // Queue operations:
  obj.processed = true;
  obj.send_email = true;

  // Enqueue to a queue
  var id = beanstalk_queue[JSON.stringify(obj)];

  // Dequeues an entry from queue
  delete beanstalk_queue[id];
  */

  var bucket = "beer-sample";
  var limit = 10;
  var type = "brewery";

  var n1qlResult = n1ql`select ${bucket}.name from ${bucket} where ${bucket}.type == ${type} limit ${limit}`;
  log(n1qlResult);
  //var n1qlResult = n1ql("select `beer-sample`.name from `beer-sample` where `beer-sample`.type == 'brewery' limit 1;");
  //log(n1qlResult);
  // n1ql operations:
  /*var n1qlResult = n1ql_blr_cluster["select `beer-sample`.name from `beer-sample` where `beer-sample`.type == 'brewery' limit 1;"]
  var n1qlResultLength = n1qlResult.length;
  for (i = 0; i < n1qlResultLength; i++) {
      log(JSON.stringify(JSON.parse(n1qlResult[i])));
  }*/
}

/*
 * Triggered when opcode DCP_DELETION is encountered
 */
function OnDelete(msg) {
  var obj = JSON.parse(msg);
  log("Got from onDelete Golang world", JSON.stringify(obj));
}

function OnHTTPGet(req) {

}

function OnHTTPPost(req) {

}

function OnTimerEvent() {

}
