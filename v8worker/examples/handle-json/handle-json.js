function OnUpdate(msg) {
    var obj = JSON.parse(msg);
    obj.handled = true;
    log("Got from OnUpdate Golang", JSON.stringify(obj));
    beer_sample["test"] = "abc";
    //delete beer_sample["test"];
};

function OnDelete(msg) {
  var obj = JSON.parse(msg);
  beer_sample["abhi"] = "BLR";
  log("Got from OnDelete Golang world", JSON.stringify(obj));
};
