"""Delete the retained MQTT topics a Bluetti BT id left behind.

Changing the Bluetooth id changes the Home Assistant discovery node and the
bluetti/<id>/... topic prefix, so the old ones stay on the broker as retained
messages and Home Assistant keeps showing the previous device. Run this with the
OLD id to make Home Assistant drop it:

    python tools/purge_node.py AC200M2306000000000
    python tools/purge_node.py --all TMPPAIRTEST     # also show what is there

A retained message is deleted by publishing a zero-length payload to the same
topic. Topics are matched case-insensitively, because MQTT itself is case
sensitive and it is easy to guess the wrong case for a BLE name.
"""
import collections
import os
import sys
import threading
import time

import paho.mqtt.client as mqtt

# Broker from the environment so a public checkout carries no private network
# details:  export BLUETTI_BROKER=192.168.1.11:1883
_broker = os.environ.get("BLUETTI_BROKER", "")
if not _broker:
    raise SystemExit("set BLUETTI_BROKER, e.g. BLUETTI_BROKER=192.168.1.11:1883")
HOST, _, _port = _broker.partition(":")
PORT = int(_port) if _port else 1883

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(1)

apply_delete = "--apply" in sys.argv
node = [a for a in sys.argv[1:] if not a.startswith("--")][0]
needle = node.lower()

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except (AttributeError, TypeError):
    client = mqtt.Client()

seen = {}


def on_connect(c, userdata, flags, rc):
    c.subscribe("#", 0)


def on_message(c, userdata, msg):
    seen[msg.topic] = (msg.payload.decode("utf-8", "replace"), msg.retain)


client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT, 30)
threading.Thread(target=client.loop_forever, daemon=True).start()
time.sleep(10)

matches = sorted(t for t in seen if needle in t.lower())
print("topics matching '%s': %d" % (node, len(matches)))
for t in matches:
    payload, retain = seen[t]
    print("  %-60s retained=%-5s payload=%s" % (t, retain, payload[:50]))

if not matches:
    print("nothing to do")
    sys.exit(0)

if not apply_delete:
    print("\ndry run - re-run with --apply to delete these retained topics")
    sys.exit(0)

for t in matches:
    client.publish(t, None, 0, retain=True)   # zero-length payload deletes it
time.sleep(3)

print("\ndeleted %d topic(s) for '%s'" % (len(matches), node))
by_scheme = collections.Counter("/".join(t.split("/")[:2]) for t in matches)
for k, v in sorted(by_scheme.items()):
    print("  %-50s %d" % (k, v))
