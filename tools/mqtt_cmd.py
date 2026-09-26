"""Send a command to the Bluetti bridge and watch the resulting state.

    python tools/mqtt_cmd.py dc_output_on ON
    python tools/mqtt_cmd.py ac_output_on OFF 40

Publishes <payload> to bluetti/<device>/command/<field> and watches
bluetti/<device>/state/<field> so the effect is visible rather than assumed.

The command is published with retain=False on purpose: a retained command would
be redelivered by the broker on every reconnect and re-toggle the port.
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

if len(sys.argv) < 3:
    print(__doc__)
    sys.exit(1)

field = sys.argv[1]
payload = sys.argv[2]
watch = int(sys.argv[3]) if len(sys.argv) > 3 else 30

state_topic = "bluetti/%s/state/%s" % (DEVICE, field)
cmd_topic = "bluetti/%s/command/%s" % (DEVICE, field)

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except (AttributeError, TypeError):
    client = mqtt.Client()

latest = {"v": None}
start = time.time()


def stamp():
    return "t=%4.1fs" % (time.time() - start)


def on_connect(c, userdata, flags, rc):
    print("connected to broker (rc=%s)" % rc)
    c.subscribe(state_topic, 0)


def on_message(c, userdata, msg):
    v = msg.payload.decode("utf-8", "replace")
    if v != latest["v"]:
        latest["v"] = v
        print("%s  state/%s = %s" % (stamp(), field, v))


client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT, 30)
threading.Thread(target=client.loop_forever, daemon=True).start()

time.sleep(6)
before = latest["v"]
print("%s  BEFORE  (retained=false, so may be blank until the next poll)" % stamp())
print("%s  publishing %r to %s" % (stamp(), payload, cmd_topic))
client.publish(cmd_topic, payload, qos=0, retain=False)

deadline = time.time() + watch
while time.time() < deadline:
    time.sleep(1)

after = latest["v"]
print("\nresult: %s -> %s" % (before, after))

if payload.upper() == "ON":
    expect = "1"
elif payload.upper() == "OFF":
    expect = "0"
elif payload.lstrip("-").isdigit():
    expect = payload          # numeric writes mirror themselves
else:
    expect = None

if before is None and after is None:
    print("no state topic observed for '%s' - this field is command-only," % field)
    print("so its effect must be verified another way (BLE link / web UI / raw page).")
elif expect is None:
    print("(no expectation modelled for payload %r)" % payload)
elif after == expect:
    print("OK: state is %s as expected" % expect)
else:
    print("MISMATCH: expected %s, saw %s" % (expect, after))
