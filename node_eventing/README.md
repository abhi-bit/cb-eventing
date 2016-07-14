# Eventing
===========

To run the prototype:

* python time_series.py 10000 # 10K time-series records to be created, within “events.in” file
* node events.js # starts up the eventing server
* node app.js # starts up server to which eventing server sometimes sends notifications
* chmod u+x rr.sh; ./rr.sh events.in # this starts pumping in data to eventing service

