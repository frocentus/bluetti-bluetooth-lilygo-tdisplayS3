#include "BWifi.h"
#include "BTooth.h"
#include "MQTT.h"
#include "config.h"
#include "display_tft.h"
#include "buttons.h"
#include "utils.h"


void setup() {
  Serial.begin(115200);
  serialLockInit();      // before anything logs from more than one task
  #ifdef RELAISMODE
    pinMode(RELAIS_PIN, OUTPUT);
    #ifdef DEBUG
      Serial.println(F("deactivate relais contact"));
    #endif
    digitalWrite(RELAIS_PIN, RELAIS_LOW);
  #endif
  #if defined(SLEEP_TIME_ON_BT_NOT_AVAIL) && (SLEEP_TIME_ON_BT_NOT_AVAIL > 0)
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_ON_BT_NOT_AVAIL * 60 * 1000000ULL);
  #endif
  // Display first so the panel is up while WiFi/BT are still connecting.
  initDisplay();
  initButtons();
  initBWifi(false);
  initBluetooth();
  initMQTT();
  wrDisp_Status("Running!");
}

void loop() {
  handleButtons();
  handleDisplay();
  handleBluetooth();
  handleMQTT();
  handleWebserver();
}
