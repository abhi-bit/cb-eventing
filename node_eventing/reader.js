var cluster = require('cluster');
var eventEmitter = require('events').EventEmitter;
var fs = require('fs');
var readline = require('readline');
var request = require('request');
var numCPUs = require('os').cpus().length;

var emitter = new eventEmitter;

var lineReader = readline.createInterface({
  input: fs.createReadStream('events.in'),
  output: process.stdout,
  terminal: false
});

lineReader.on('line', function(line) {
  var obj = JSON.parse(line);
  emitter.on(obj.id, console.log);

  if (obj.type == "DELETE") {
    var interval = setInterval(function(doc) {
      request({
        uri: "http://localhost:3000/",
        method: "POST",
        form: {
            type: "delete_event",
            id: obj.id,
            ts: obj.ts
        }
      }, function(error, response, body) {
          emitter.emit(obj.id, body);
      })
    }, obj.ts, obj);

    setTimeout(function() {
      emitter.removeListener(obj.id, console.log);
      clearInterval(interval);
    }, obj.ts);
  }

  if (obj.type == "SET") {
      var interval = setInterval((doc) => {
          emitter.emit(obj.id, doc);
      }, obj.ts, obj);
      setTimeout(() => {
          emitter.removeListener(obj.id, console.log);
          clearInterval(interval);
      }, obj.ts);
  }
})
