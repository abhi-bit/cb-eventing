var http = require('http');
var cluster = require('cluster');
var numCPUs = require('os').cpus().length;
var querystring = require('querystring');

if (cluster.isMaster) {
  for(var i = 0; i < numCPUs; i++) {
    cluster.fork();
  }

  console.log('Server running at http://localhost:3000/');
  cluster.on('exit', function(worker, code, signal) {
    if (signal) {
      console.log('worker was killed by signal: ${signal}');
    } else if (code !== 0) {
      console.log('worker exited with error code: ${code}');
    } else {
      console.log('worker started: ${worker.process.pid}');
    }});
} else {
    console.log('worker process pid:', process.pid);
    http.createServer(function(req, res) {
        // console.log('serviced by worker pid: ', process.pid);
        if (req.method == 'POST') {
            var body = '';

            req.on('data', function(data) {
                body += data;
                if (body.length > 1e3)
                    req.connection.destroy();
            });

            req.on('end', function() {
                var post = querystring.parse(body);
                console.log(post);
            });
        }
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('Hello World\n');
    }).listen(3000);
}

