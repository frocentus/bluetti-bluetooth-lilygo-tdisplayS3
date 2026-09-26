#ifndef CONFIG_H
#define CONFIG_H
#include "Arduino.h"

#define DEBUG                 1

/* Raw BLE frame dumps on the serial console.
 * OFF by default, deliberately: the NimBLE host task and the loop task both write
 * Serial, and the Arduino stream chunks long writes, so a frame dump gets spliced
 * mid-line by MQTT logging and the resulting capture cannot be trusted. With this
 * off the loop task is effectively the only writer. Use GET /rawPage when you
 * need a register page - it returns one frame, complete and un-interleaved.
 * Turning this on is what makes serial captures lossy. */
#define DEBUG_BT_FRAMES       0

/* ---- Display: LilyGo T-Display S3 ---------------------------------------
 * 1.9" ST7789V, 170x320 native, 8-bit parallel (I8080) on the ESP32-S3
 * LCD_CAM peripheral. Driver and panel pinout are configured through TFT_eSPI
 * build flags in platformio.ini, not through TFT_eSPI's own User_Setup.h.
 *
 * GPIO15 powers the LCD rail and GPIO38 is the backlight. Both must be driven
 * HIGH or the screen stays blank with no error; see display_tft.cpp.
 *
 * The old SSD1306/OLED path was removed: on this board GPIO4 is the battery
 * ADC and GPIO5 is the LCD reset, so the previous I2C pins (4 and 5) could
 * never have worked here. Real I2C on this board is SDA=18, SCL=17.
 * ------------------------------------------------------------------------ */
#define PIN_LCD_POWER_ON 15
#define PIN_LCD_BL       38

/* T-Display S3 buttons (LilyGO examples/tft/pin_config.h). Both are active low
 * and need the internal pull-up, which is what every LilyGO example does.
 * NOTE: BUTTON_1 is GPIO0, which is also the ESP32-S3 BOOT strapping pin - held
 * low through a reset the chip enters download mode and the app never starts, so
 * it must not be given an action that invites holding it at power-up. */
#define PIN_BUTTON_1 0
#define PIN_BUTTON_2 14

#define EEPROM_SALT 13374

#define DEVICE_NAME "BLUETTI-MQTT"
#define BLUETTI_TYPE AC200M

#define BLUETOOTH_QUERY_MESSAGE_DELAY 3000
#define BLUETOOTH_MAX_RETRIES_BEFORE_REBOOT 10
/* BLE re-scan while disconnected. Both were defined but referenced nowhere,
 * which is why the only scan used to be the single 5 second window at the end of
 * initBluetooth(): if that window missed the station (typical right after it
 * powers on) nothing ever rescanned, and the bridge stayed disconnected until
 * the 5 minute timeout rebooted it.
 * The scan blocks the loop for its duration, so keep the duty cycle low: 5s of
 * scanning every 30s leaves MQTT (15s keepalive) and the display responsive. */
#define BLUETOOTH_SCAN_DURATION_IN_SECONDS 5
#define BLUETOOTH_SCAN_INTERVAL_IN_SECONDS 30

/* No relay is wired on this board.
 * WARNING: the upstream default was GPIO22, which does not exist on the
 * ESP32-S3 (it has 0-21 and 26-48), so enabling RELAISMODE requires picking a
 * free pin first. Claimed on the T-Display S3: 4,5,6,7,8,9,15,16,17,18,19,20,
 * 21,38 and 39-48; GPIO1/2/3/10/11/12/13/43/44 are the usable header pins. */
//#define RELAISMODE 1
#define RELAIS_PIN 1
#define RELAIS_LOW LOW
#define RELAIS_HIGH HIGH

#define MAX_DISCONNECTED_TIME_UNTIL_REBOOT 5 //device will reboot when wlan/BT/MQTT is not connectet within x Minutes
/* What to do when the power station has not been reachable over BLE for
 * MAX_DISCONNECTED_TIME_UNTIL_REBOOT minutes:
 *   > 0  deep sleep for that many minutes, then reboot and try again
 *   = 0  stay up and keep rescanning (see BLUETOOTH_SCAN_INTERVAL_IN_SECONDS)
 * The previous code rebooted on BOTH paths despite documenting 0 as "disabled",
 * which made the bridge unreachable for as long as the station was switched off.
 * With the periodic rescan in place there is no need to reboot to recover, so 0
 * is the sensible default for a station that is not always on. */
#define SLEEP_TIME_ON_BT_NOT_AVAIL 0
#define DEVICE_STATE_UPDATE  5
#define MSG_VIEWER_DETAILS 0 //enable detailed BT/MQTT messages via WebUI by default, can be changed in WebUI
#define DEVICE_STATE_STATUS_UPDATE  2.5 //Was 0.5 in original branc which is half the DEVICE_STATE_UPDATE value, kept the ratio
#define MSG_VIEWER_ENTRY_COUNT 20 //number of lines for web message viewer
#define MSG_VIEWER_REFRESH_CYCLE 5 //refresh time for website data in seconds

/* Home Assistant MQTT discovery (see HADiscovery.cpp).
 * MQTT_DISCOVERY_PREFIX is HA's discovery root topic - change it only if the
 * MQTT integration was set up with a custom discovery prefix.
 * MQTT_BUFFER_SIZE must fit the largest discovery payload (~600 bytes with a
 * long Bluetti id + serial). PubSubClient's default is 256 and it drops the
 * connection instead of reporting an error when a publish exceeds the buffer. */
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define MQTT_BUFFER_SIZE 1024


#ifndef BLUETTI_TYPE
  #define BLUETTI_TYPE AC300
#endif



#endif
