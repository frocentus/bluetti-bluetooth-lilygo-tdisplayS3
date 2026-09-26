#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include <Arduino.h>
#include <DeviceType.h>

// LilyGo T-Display S3 status screen (ST7789V 170x320, 8-bit parallel).
// Layout and the panel power rail are documented in display_tft.cpp, and the
// panel pinout lives in platformio.ini.

// Called once from setup()
void initDisplay();

// Called every loop() iteration; repaints only the regions that changed
void handleDisplay();

// Blank the panel and drop the backlight + power rail, for deep sleep
void displayOff();

/* Live values for the dashboard, fed from publishTopic() - the same single
 * funnel the Home Assistant discovery uses, so no extra polling is added.
 * Fields not handled here are ignored. */
void disp_setField(enum field_names field, const String &value);

// --- status / connectivity -------------------------------------------------
void wrDisp_IP(String strIP = "NoConf");
void wrDisp_Status(String strStatus = "boot..");
void wrDisp_wifisignal(int intMode = 0, int intSignal = -100);

void disp_setBlueTooth(bool boolBtConn = false);
void disp_setMqttStatus(bool blMqttconnected = false);
void disp_setWifiSignal(int extWifMode = 0, int extSignal = -100);
void disp_setWifiMode(byte wMode);
void disp_setStatus(String strStatus);

/* --- button-driven screen control (see buttons.cpp) ----------------------- */
void displayNextPage();
void displayPrevPage();
void displayToggleBacklight();
void displaySetBacklight(bool on);
bool displayBacklightOn();
void displayNotice(const String &text);

/* Draws the "join this network, open this URL" screen used while the config
 * portal is running. Called directly from the WiFiManager AP callback, because
 * loop() is not running yet at that point. */
void displaySetupScreen();

#endif
