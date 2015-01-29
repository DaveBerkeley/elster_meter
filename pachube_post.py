#!/usr/bin/python

import time
import datetime
import os
import sys
import optparse

import httplib
import json

#
#   Post data to Cosm feed

class Cosm:

    host = 'api.cosm.com'
    agent = "python cosm 1.0"
 
    def __init__(self, feed, key, https=True):
        self.headers = {
            "Content-Type"  : "application/x-www-form-urlencoded",
            "X-ApiKey"      : key,
            "User-Agent"    : self.agent,
        }
        self.params = "/v2/feeds/" + str(feed)
        if https:
            self.Connection = httplib.HTTPSConnection
        else:
            self.Connection = httplib.HTTPConnection

    def put(self, info):
        items = []

        for key, value in info:
            items.append( { "id" : key, "current_value" : value } )

        data = {
          "version" : "1.0.0",
          "datastreams": items,
        }
        body = json.dumps(data)

        http = self.Connection(self.host)
        http.request("PUT", self.params, body, self.headers)
        response = http.getresponse()
        http.close()
        return response.status, response.reason

#
#

def make_path():
    pathfmt = "/usr/local/data/solar/%s/%02d/%02d.log"

    now = datetime.datetime.now()
    path = pathfmt % (now.year, now.month, now.day)
    return path

def parse_line(text):
    # read text in the form "HH:MM:SS wwwww"
    hms, w = text.strip().split()
    hms = hms.split(":")
    h, m, s = [ int(x,10) for x in hms ]
    secs  = (h * 3600) + (m * 60) + s
    watts  = float(int(w, 10))
    return secs, watts * 3600

#
#   Moving Average Filter

class Filter:

    def __init__(self, ntaps):
        self.ntaps = ntaps
        self.data = None

    def filter(self, data):
        if self.data is None:
            self.data = [ data, ] * self.ntaps
        self.data = self.data[1:] + [ data ]
        return int(sum(self.data) / float(self.ntaps))

#
#

class NoData(Exception):
    pass

def read_line(f, fn=None):
    fx = fn
    if fx is None:
        fx = f.read
    line = fx()
    if not line:
        raise NoData()

    print `line`

    t, w = parse_line(line)
    return t, w

#
#

def put_data(cosm, p, today_kwh, kwh):
        info = [ 
            ("0", p), 
            ("1", today_kwh), 
            ("2", kWh), 
        ]

        if opts.test:
            print info
            return

        try:
            cosm.put(info)
        except Exception, ex:
            print "Error", str(ex)

#
#

FEED = <your feed here>
API_KEY = '<your key here>'

p = optparse.OptionParser()
p.add_option("--test", "-t", dest="test", action="store_true")

opts, args = p.parse_args()

this_path = None
last_w = None
last_t = None
first_w = None

lpf = Filter(3)

cosm = Cosm(FEED, API_KEY)

while True:

    try:

        sys.stdout.flush()
        time.sleep(10)

        path = make_path()
        if path != this_path:

            if not os.path.exists(path):
                continue

            print "open path", path
            f = open(path, "r")

            # get the first line
            try:
                t, first_w = read_line(f, f.readline)
            except Exception, ex:
                print "Error", str(ex)
                continue            

            f.seek(0, 2) # seek to end
            this_path = path

        try:
            t, w = read_line(f)
        except NoData, ex:
            continue
        except Exception, ex:
            print "Error", str(ex)
            continue

        if w == last_w:
            continue

        if last_w is None:
            last_w = w
            last_t = t
            continue

        dt = t - last_t

        if not dt:
            continue

        dw =  w - last_w
        power = dw / dt
        last_t = t
        last_w = w

        p = lpf.filter(power)

        kWh = w / (3600 * 1000.0)
        today_kwh = (w  - first_w) / (3600 * 1000.0)
        print p, today_kwh, kWh

        put_data(cosm, p, today_kwh, kWh)

    except Exception, ex:
        # track down that error!
        print "Error", str(ex)

# FIN
