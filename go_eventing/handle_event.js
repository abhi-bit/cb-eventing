/* Triggered when opcode DCP_MUTATION is encountered
* Getting mutations from default bucket
* writing KV pairs to "beer_sample" bucket
*/
function OnUpdate(msg) {
  var obj = JSON.parse(msg);
  var val = JSON.stringify(obj);

  log("Got from onUpdate Golang world: ", val);

  // KV operation examples

  // Sets KV pair into "beer_sample" bucket in Couchbase
  beer_sample["test_key1"] = val;

  // Fetches Key from Couchbase
  var key = beer_sample["test_key1"];

  // Deletes a key from Couchbase
  delete beer_sample["test_key1"];


  // Queue operations:
  val.processed = true;
  val.send_email = true;

  // Enqueue to a queue
  kafka_queue["queue_test_key1"] = val;

  // Dequeues an entry from queue
  delete kafka_queue["queue_test_key1"];

  // n1ql operations:
  var n1qlResult = n1ql["select * from `beer-sample`;"];
  var n1qlResultLength = n1qlResult.length;
  for ( var i = 0; i < n1qlResultLength; i++) {
      log(n1qlResult[i]);
  }
}

/*
 * Triggered when opcode DCP_DELETION is encountered
 */
function OnDelete(msg) {
  var obj = JSON.parse(msg);
  log("Got from onDelete Golang world", JSON.stringify(obj));
}
