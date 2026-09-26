"""Watch the retained availability topic until it settles on 'online'.

Proves that the periodic re-assert in MQTT.cpp recovers from a stale
'offline' last-will left by an unclean reboot.
"""
import os
import sys
import threading
import time

import paho.mqtt.client as mqtt

# Broker and station id from the environment so a public checkout carries no
# private network details:
#   export BLUETTI_BROKER=192.168.1.11:1883
#   export BLUETTI_DEVICE_ID=AC200M2306000000000
_broker = os.environ.get("BLUETTI_BROKER", "")
DEVICE = os.environ.get("BLUETTI_DEVICE_ID", "")
if not _broker or not DEVICE:
    raise SystemExit("set BLUETTI_BROKER (host[:port]) and BLUETTI_DEVICE_ID")
HOST, _, _port = _broker.partition(":")
PORT = int(_port) if _port else 1883

TOPIC = "bluetti/%s/status" % DEVICE
value = {"v": None, "at": None}

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except (AttributeError, TypeError):
    client = mqtt.Client()


def on_connect(c, userdata, flags, rc):
    c.subscribe(TOPIC, 0)


def on_message(c, userdata, msg):
    value["v"] = msg.payload.decode("utf-8", "replace")
    value["at"] = time.time()


client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT, 30)
threading.Thread(target=client.loop_forever, daemon=True).start()

start = time.time()
deadline = start + float(sys.argv[1] if len(sys.argv) > 1 else 200)
last = None
while time.time() < deadline:
    time.sleep(10)
    elapsed = int(time.time() - start)
    if value["v"] != last:
        print("t=%3ds  status = %s" % (elapsed, value["v"]))
        last = value["v"]
    if last == "online" and elapsed > 20:
        print("settled on 'online' after %ds" % elapsed)
        break
else:
    print("gave up, final status = %s" % last)
