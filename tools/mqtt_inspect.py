"""Subscribe to the broker and report what the bridge is actually publishing.

Used to verify the Home Assistant discovery work independently of the ESP32's
serial console, which interleaves BLE hex dumps with MQTT log lines.
"""
import os
import sys
import threading
import time

import paho.mqtt.client as mqtt

# Broker and station id come from the environment so a public checkout carries no
# private network details:
#   export BLUETTI_BROKER=192.168.1.11:1883
#   export BLUETTI_DEVICE_ID=AC200M2306000000000
_broker = os.environ.get("BLUETTI_BROKER", "")
DEVICE = os.environ.get("BLUETTI_DEVICE_ID", "")
if not _broker or not DEVICE:
    raise SystemExit("set BLUETTI_BROKER (host[:port]) and BLUETTI_DEVICE_ID")
HOST, _, _port = _broker.partition(":")
PORT = int(_port) if _port else 1883
NODE = DEVICE.lower()          # Home Assistant's node id is the slugified name

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except (AttributeError, TypeError):
    client = mqtt.Client()

discovery = {}
state = {}
other = {}


def on_connect(c, userdata, flags, rc):
    print("connect rc=%s" % rc)
    c.subscribe("homeassistant/#", 0)
    c.subscribe("bluetti/#", 0)


def on_message(c, userdata, msg):
    payload = msg.payload.decode("utf-8", "replace")
    if msg.topic.startswith("homeassistant/"):
        discovery[msg.topic] = (payload, msg.retain)
    elif msg.topic.startswith("bluetti/"):
        if "/state/" in msg.topic or msg.topic.endswith("/status"):
            state[msg.topic] = payload
        else:
            other[msg.topic] = payload


client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(HOST, PORT, 30)
except Exception as exc:
    print("TCP/connect failed: %r" % exc)
    sys.exit(1)

thread = threading.Thread(target=client.loop_forever, daemon=True)
thread.start()
time.sleep(12)
client.disconnect()

print("\n=== retained discovery configs: %d" % len(discovery))
for topic in sorted(discovery):
    print("  " + topic)

print("\n=== live state/status topics: %d" % len(state))
for topic in sorted(state):
    print("  %s = %s" % (topic, state[topic][:60]))

print("\n=== other bluetti topics: %d" % len(other))
for topic in sorted(other):
    print("  %s" % topic)

if discovery:
    # Show our own entities; the broker also carries other integrations.
    interesting = ["homeassistant/switch/%s/ac_output_on/config" % NODE,
                   "homeassistant/sensor/%s/total_battery_percent/config" % NODE,
                   "homeassistant/number/%s/auto_sleep_mode/config" % NODE]
    for topic in interesting:
        if topic in discovery:
            print("\n=== %s ===" % topic)
            print(discovery[topic][0])
