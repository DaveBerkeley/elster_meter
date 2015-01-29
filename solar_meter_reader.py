#!/usr/bin/python

import sys
import os
import datetime
import time
import serial

base = "/usr/local/data/solar"

ser = serial.Serial('/dev/ttyUSB0', 9600)

last_wh = None
last_m = None
last_path = None
f =  None

while True:
    line = ser.readline()

    try:
        wh = int(line, 10)
    except ValueError:
        time.sleep(0.5)
        continue
    if wh == last_wh:
        continue

    now = datetime.datetime.now()

    if now.minute == last_m:
        continue

    dirname = os.path.join(base, str(now.year), "%02d" % now.month)
    if not os.path.exists(dirname):
        os.makedirs(dirname)

    path = os.path.join(dirname, "%02d.log" % now.day)

    if path != last_path:
        if not f is None:
            f.close()
        f = open(path, "a+")
        last_path = path

    fmt = "%H:%M:%S"
    print >> f, now.strftime(fmt), wh
    f.flush()

    last_wh = wh
    last_m = now.minute

# FIN
