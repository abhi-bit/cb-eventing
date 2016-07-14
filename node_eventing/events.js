var cluster = require('cluster');
var eventEmitter = require('events').EventEmitter;
var fs = require('fs');
var net = require('net');
var request = require('request');
var numCPUs = require('os').cpus().length;

// var cb = require('./bucket');

var emitter = new eventEmitter;

if (cluster.isMaster) {
    for (var i = 0; i < numCPUs; i++) {
        cluster.fork();
    }
    cluster.on('exit', function(worker, code, signal) {
        console.log('worker %d died (%s). restarting...',
                   worker.process.pid, signal, code);
        cluster.fork();
    });
} else {
    net.createServer(function(socket) {
        socket.on('data', function(data) {

            var obj = JSON.parse(data);
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
                var interval = setInterval(function(doc) {
                    emitter.emit(obj.id, doc);
                }, obj.ts, obj);
                setTimeout(function() {
                    emitter.removeListener(obj.id, console.log);
                    clearInterval(interval);
                }, obj.ts);
            }
        })
    }).listen(3001, '127.0.0.1');
}
