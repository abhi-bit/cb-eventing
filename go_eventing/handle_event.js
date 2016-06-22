function OnUpdate(msg) {
  var obj = JSON.parse(msg);
  log("Got from onUpdate Golang world: ", JSON.stringify(obj));
  //beer_sample["test"] = "abc";
}

function OnDelete(msg) {
  var obj = JSON.parse(msg);
  log("Got from onDelete Golang world", JSON.stringify(obj));
}
