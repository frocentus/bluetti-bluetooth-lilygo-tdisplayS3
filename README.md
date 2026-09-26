# Bluetti_ESP32

Reads a Bluetti power station over Bluetooth LE (Modbus-over-BLE) and republishes
every field it exposes to MQTT, a web UI, a Home Assistant MQTT-discovery entity
set, and the on-board display.

Runs on a **LilyGo T-Display S3** (ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 1.9"
ST7789V 170x320 on the 8-bit parallel bus).

Originally based on the community `Bluetti_ESP32_Bridge` project. This copy has
been reworked for the T-Display S3 and extended: a power dashboard, four display
pages, two working buttons, Home Assistant discovery, and a verified register map
for the AC200M.

---

## Build and flash

PlatformIO, pinned deliberately — the BLE code uses the `BLE*` class names that
NimBLE-Arduino 2.x removed, so **arduino-esp32 must stay on 2.0.x and
NimBLE-Arduino on 1.4.x**. See `platformio.ini`.

```bash
pio run                        # build
pio run -t upload              # build + flash
pio device monitor -b 115200   # serial
```

Upload speed is 460800 by default. If uploading fails with a serial exception
mid-write, the board's native USB is the cause, not the firmware — try another
cable/port first, and as a fallback flash with a current standalone esptool
(the command is in `platformio.ini`).

To wipe a board completely — configuration and all — before shipping it:

```bash
esptool --chip esp32s3 --port <PORT> erase-flash
esptool --chip esp32s3 --port <PORT> --baud 460800 write-flash \
  --compress --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0000 .pio/build/lilygo_t_display_s3/bootloader.bin \
  0x8000 .pio/build/lilygo_t_display_s3/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/lilygo_t_display_s3/firmware.bin
```

A plain reflash does **not** touch the configuration; only `erase-flash` does.

---

## First-time setup

An unconfigured board shows this on its panel:

```
SETUP WIFI
1  join network
Bluetti_ESP32
2  open
http://192.168.4.1
then /scanBT lists Bluetooth names
```

1. Join the WiFi network **`Bluetti_ESP32`** (open, no password).
2. Open **http://192.168.4.1**.
3. Fill in: WiFi, MQTT server/port/username/password, OTA credentials
   (recommended, see below), and the **Bluetti Bluetooth ID**.

The portal has no timeout in this state, so it waits until you save.

### Getting the Bluetooth ID right

This is the field people get wrong. It is matched with an exact `strcmp` against
the name the station advertises, so a typo means the board scans forever. The name
is the model plus the serial, e.g. `AC200M2306000000000` — it is on the station's
own screen and in the Bluetti phone app.

If you are not sure, configure the board first with a guess, then read the list
off the board itself once it is on the network:

```
http://<board-ip>/scanBT
```

It lists the Bluetooth names heard by the most recent scan. Copy the station's
name exactly and apply it — this does not disturb WiFi or MQTT:

```
http://<board-ip>/setBluettiID?value=<name>
```

The board reboots to apply it.

---

## Installing at another site

Three things to get right, in order of how often they break an install:

1. **The MQTT server must be reachable from that network.** A broker on your own
   LAN (`192.168.x.x`) cannot be reached from another house. Use a public broker,
   a port-forwarded host, or a VPN/tailnet address. Symptom if wrong: WiFi is up,
   the MQTT chip on the panel is dark, and the web UI shows `mqtt_connected 0`.
2. **The Bluetooth ID must match exactly** — see above. Symptom if wrong: MQTT and
   WiFi are fine, but the BT signal bars stay empty and no `state/` topics appear.
   Recoverable remotely via `/scanBT` and `/setBluettiID`.
3. **Set an OTA username/password.** It protects `/update`, and it is also what
   activates the auth guard on `/setBluettiID` and `/resetConfig`. At a remote
   site, an unauthenticated `/resetConfig` means the board parks itself in AP mode
   until somebody drives over with a laptop.

### Reading the panel on site

The footer alternates every 4 s between `IP <address> / up <uptime>` and the
status line, so the board's address is always obtainable without a console. The
buttons cycle through four pages:

| Button | Short press | Long press (1.2 s) |
|---|---|---|
| BUTTON_2 (GPIO14) | next page | backlight on/off |
| BUTTON_1 (GPIO0) | previous page | reboot (shows *release to reboot*) |

Pages: **1** power summary, **2** PORTS (volts/amps/watts per port), **3** CELLS
(16 cells, coloured by deviation from the mean), **4** SYSTEM (IP, WiFi/BLE dBm,
MQTT broker and state, publish errors, uptime, heap, firmware versions, serial).

`BUTTON_1` long press waits for you to *release* before restarting: GPIO0 is the
ESP32-S3 BOOT strapping pin, and resetting while it is held low drops the chip
into download mode, where the app never starts.

With the backlight off, the first press only lights the screen again — so a press
in the dark cannot page blindly or reboot the board.

---

## Web endpoints

| Endpoint | Purpose |
|---|---|
| `/` | status page (IP, RSSI, MQTT/BT state, message viewer) |
| `/scanBT` | Bluetooth names heard by the last scan |
| `/setBluettiID?value=<name>` | change the pairing without wiping other config |
| `/rawPage` | last raw page-0 BLE frame as hex — for register work |
| `/rebootDevice` | restart |
| `/resetConfig` | **wipe WiFi + MQTT + Bluetooth id**, reboot into the portal |
| `/switchLogging` | toggle the message viewer |
| `/events` | SSE feed used by the status page |
| `/update` | OTA firmware upload (ElegantOTA) |

All are unauthenticated **unless** an OTA username is set, in which case
`/setBluettiID`, `/resetConfig` and `/update` require those credentials.

---

## MQTT

Topics, all derived from the Bluetooth id:

```
bluetti/<id>/state/<field>       one topic per readable field
bluetti/<id>/state/device        {"IP","MAC","Uptime"}
bluetti/<id>/state/device_status {"MQTTconnected","BTconnected"}
bluetti/<id>/command/<field>     write a setting; ON/OFF or a number
bluetti/<id>/status              "online"/"offline" (retained, last will)
```

Home Assistant creates the entities from retained discovery configs on
`homeassistant/<component>/<node>/<object>/config` — sensors for readable fields,
switches for boolean settings, a number for the sleep timer, a button for power
off. Every entity points at the `state/` and `command/` topics above, so the raw
topics keep working for anything else that consumes them.

The MQTT client id is derived from the board's MAC, so two bridges can share a
broker without evicting each other.

---

## Development tools

`tools/` holds small diagnostics used while working on this. They take the broker,
the station's Bluetooth id and the bridge address from the environment, so no
private addresses or serial numbers live in the source:

```bash
export BLUETTI_BROKER=192.168.1.11:1883        # MQTT broker, host[:port]
export BLUETTI_DEVICE_ID=AC200M2306000000000   # the station's Bluetooth name
export BLUETTI_BRIDGE=http://192.168.1.50      # only decode_page.py needs this
```

| Script | Purpose |
|---|---|
| `mqtt_inspect.py` | show what the bridge is publishing, retained and live |
| `mqtt_cmd.py <field> <ON\|OFF\|number>` | send a command and watch the resulting state |
| `watch_availability.py` | follow the retained `status` topic |
| `decode_page.py fetch\|show\|diff <label>` | decode `/rawPage` register dumps, and diff two captures |
| `purge_node.py <node>` | delete retained topics left behind by a changed Bluetooth id |

## Troubleshooting

**Start with the panel.** The SYSTEM page shows broker, MQTT state, publish
errors, both RSSI values and the uptime — enough to tell a WiFi problem from a
broker problem from a BLE problem without any tools.

| Symptom | Likely cause |
|---|---|
| `bt_connected 0`, MQTT and WiFi fine | Station off, out of range, its Bluetooth already connected to the phone app (these units accept one central), or a wrong Bluetooth id — check `/scanBT` |
| `mqtt_connected 0` with errors climbing | Broker unreachable from that network |
| Everything dead but WiFi answers | `loop()` is blocked; see the serial log |
| Board went into AP mode by itself | `/resetConfig` was called |

The bridge re-scans for the station every 30 s while disconnected, so it recovers
from a station power-cycle on its own within about a minute — a reboot is not
normally needed.

It deliberately **does not** deep-sleep when the station is off
(`SLEEP_TIME_ON_BT_NOT_AVAIL 0`), so the web UI and MQTT stay reachable. Set that
define to a positive number if you would rather it slept.

### Serial output and the helper scripts

Serial is only trustworthy with `DEBUG_BT_FRAMES 0` (the default). Enabling it
dumps every BLE frame, and because the NimBLE host task and the loop task both
write to the same stream, long writes get spliced into each other and captures
become misleading. Use `/rawPage` when you need a register page instead.

---

## Known limitations

- **Only the AC200M register map is verified.** The AC300, EP500, EB3A, EP500P,
  AC500 and EP600 tables are upstream-derived; none has been checked against real
  hardware the way the AC200M's was. Expect wrong values before trusting them.
- The device type is a **compile-time** setting (`BLUETTI_TYPE` in `config.h`) —
  change it and rebuild for a different model.
- Two registers (`0x4B`, `0x35`) remain unidentified, and are deliberately left
  unnamed rather than guessed at.
- While the config portal is running, `loop()` is not, so the buttons do nothing
  and the panel is static. That is inherent to `autoConnect()` blocking.
- `parse_enum_field()` publishes a label for the enums the device headers
  document; a value outside those ranges falls back to the raw number rather than
  disappearing.
