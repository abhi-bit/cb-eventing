var couchbase = require('couchbase');

var cluster = new couchbase.Cluster('couchbase://donut');
var bucket = cluster.openBucket('default', function(err) {
    if (err) {
        throw err;
    }
    bucket.replace('doc_name', {some:'value'}, function(err, res) {
        if (err) {
            throw err;
        }
    });

    bucket.get('doc_name', function(err, res) {
        if (err) {
            throw err;
        }
        console.log("value: ", res.value);
        process.exit(0);
    });
});
