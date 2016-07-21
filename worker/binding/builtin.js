function n1ql(strings, ...values) {
    var stringsLength = strings.length;
    var query = "";

    query = strings[0];

    for (i = 0; i < stringsLength - 1; i++) {
        if (typeof values[i] === "string" && values[i].indexOf("-") !== -1) {
            query = query.concat('`');
            query = query.concat(values[i]);
            query = query.concat('`');
        } else {
          query = query.concat(values[i]);
        }
        query = query.concat(strings[i + 1]);
    }
    return _n1ql[query];
}

function ISODateString(d) {
    function pad(n) {return n<10 ? '0'+n : n}
    return d.getUTCFullYear()+'-'
         + pad(d.getUTCMonth()+1)+'-'
         + pad(d.getUTCDate())+'T'
         + pad(d.getUTCHours())+':'
         + pad(d.getUTCMinutes())+':'
         + pad(d.getUTCSeconds())
}
