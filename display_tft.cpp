#include <Arduino.h>
#include <TFT_eSPI.h>

#include "display_tft.h"
#include "config.h"
#include "BTooth.h"   // getBTRssi() for the BT signal indicator
#include "BWifi.h"    // get_esp32_bluetti_settings() for the system page
#include "MQTT.h"     // getPublishErrorCount() for the system page
#include <WiFi.h>     // softAPSSID()/softAPIP() for the setup screen

/* ---------------------------------------------------------------------------
 * LilyGo T-Display S3 power dashboard.
 *
 * Panel: 1.9" ST7789V, 170x320 native, on the ESP32-S3 8-bit parallel (I8080)
 * bus. Used landscape (rotation 3) => 320x170. Driver + pinout come from the
 * TFT_eSPI build flags in platformio.ini, NOT from TFT_eSPI's User_Setup.h.
 *
 * Two things beyond tft.init() are needed on this board, both lifted from
 * LilyGO's own examples/tft/tft.ino:
 *   GPIO15 (PIN_LCD_POWER_ON) powers the panel rail. TFT_eSPI knows nothing
 *   about it; leave it low and the screen stays black with no error.
 *   GPIO38 (PIN_LCD_BL) is the backlight. TFT_eSPI raises it itself from the
 *   TFT_BL/TFT_BACKLIGHT_ON flags, but we assert it too so the display does not
 *   depend on that library code path staying put.
 *
 * THREADING: disp_setField() is called from publishTopic(), which runs in the
 * NimBLE host task, while everything is rendered from the Arduino loop task.
 * The values are therefore handed over through a FreeRTOS queue rather than
 * shared directly: all the String state below is touched by the loop task
 * only. Sharing String objects across tasks would race on the heap and the
 * refcount. (The connectivity flags are plain bool/byte, which are atomic on
 * Xtensa, so those setters write directly.)
 *
 * Layout (320x170):
 *   y   0..22   title, RSSI, WIFI/BT/MQTT chips
 *   y  32..48   BATTERY / AC IN / AC OUT / DC IN labels
 *   y  50..76   the four values
 *   y  82..90   state-of-charge bar under the battery column
 *   y  98..116  PACK / CELLS min-max / dMAX
 *   y 126..162  IP + uptime, then the status line
 * Text is centred/right-aligned via textWidth() rather than fixed offsets, so
 * a longer IP or a 4-digit wattage cannot overlap its neighbour.
 * ------------------------------------------------------------------------- */

static TFT_eSPI tft;

/* ---- hand-over queue: BLE task -> loop task ----------------------------- */
struct DispField {
  uint8_t field;
  char value[16];
};
#define DISP_QUEUE_LEN 48
static QueueHandle_t dispQueue = nullptr;

/* ---- connectivity state (plain scalars, atomic on Xtensa) --------------- */
static bool btConnected = false;
static bool mqConnected = false;
static byte wifiMode    = 0;      /* 0 = down, 1 = connected, 2 = AP portal */
static int  wifiSignal  = -100;
static byte blinkPhase  = 0;

/* ---- render state: owned by the loop task ------------------------------ */
static String vModel   = "";
static String vBattery = "--";
static String vAcIn    = "--";
static String vAcOut   = "--";
static String vDcIn    = "--";
static String vDcOut   = "--";
static String vPack    = "--";
static String vIP      = "NoConf";
static String vStatus  = "boot..";
/* port detail for the PORTS screen */
static String vAcOutV  = "--", vAcOutA = "--", vAcOutHz = "--";
static String vAcInV   = "--", vAcInA = "--";
static String vDcOutV  = "--", vDcOutA = "--";
static String vDcInV   = "--", vDcInA  = "--";
/* identity for the SYSTEM screen */
static String vArm     = "--", vDsp = "--", vSerial = "--";
static float  cells[16];
static bool   cellValid[16] = {false};

/* ---- screens ------------------------------------------------------------ */
#define PAGE_COUNT 4            /* 0 power, 1 ports, 2 cells, 3 system */
static byte currentPage = 0;
static bool pageDirty = true;   /* full repaint: page switch */
static bool dataDirty = false;  /* a value the active page shows changed */
static bool backlightOn = true;

static bool headerDirty  = true;
static bool metricsDirty = true;
static bool detailDirty  = true;
static bool footerDirty  = true;

/* The footer alternates between the IP/uptime pair and the status, so the status
 * does not need a row of its own. */
static bool footerShowStatus = false;

/* ---- signal strength ---------------------------------------------------- */
static int  btRssi = 0;                     /* last BLE link RSSI we managed to read */
static byte lastWifiLevel = 255, lastBtLevel = 255;

/* ---- timers ------------------------------------------------------------- */
#define FLASH_MS    500
#define RUNTIME_MS  30000
#define FOOTER_MS   4000    /* how long each footer view stays up */
/* NimBLEClient::getRssi() is a synchronous call into the NimBLE host, so keep it
 * infrequent - a signal indicator does not need better than this anyway. */
#define BT_RSSI_MS  15000
static unsigned long prevFlash = 0, prevRuntime = 0, prevBtRssi = 0, prevFooter = 0;

/* ---- layout ------------------------------------------------------------- */
#define SCR_W    320
#define SCR_H    170

#define CHIP_W   34
#define CHIP_H   16
#define CHIP_Y   4
#define CHIP_X0  (SCR_W - 4 - CHIP_W)          /* the MQTT indicator */

/* Signal indicators: label + four bottom-aligned bars + the numeric reading.
 * Positions are computed when drawing so the groups pack against the MQTT chip. */
#define SIG_BARS    4
#define SIG_BAR_W   3
#define SIG_BAR_GAP 2
#define SIG_W       (SIG_BARS * SIG_BAR_W + (SIG_BARS - 1) * SIG_BAR_GAP)
#define SIG_BOTTOM  17                          /* tallest bar spans 5..17 */
#define SIG_LABEL_Y 8

#define COL_W    (SCR_W / 5)                    /* BATT AC-IN AC-OUT DC-IN DC-OUT */
#define Y_LABEL  34
#define Y_VALUE  54
#define Y_UNIT   84
#define Y_BAR    100
#define BAR_H    12
#define Y_DETAIL 122
#define Y_RULE   142
#define Y_FOOT   146

/* -------------------------------------------------------------------------- */

static uint16_t statusColour(const String &s) {
  if (s.indexOf("err") > -1 || s.indexOf("Err") > -1) return TFT_RED;
  if (s.startsWith("Setup")) return TFT_CYAN;
  if (s.startsWith("Init") || s.startsWith("boot")) return TFT_ORANGE;
  return TFT_GREEN;
}

static void clearRegion(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, TFT_BLACK);
}

static void drawCentered(const String &s, int cx, int y, uint8_t font, uint16_t colour) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawString(s, cx, y, font);
  tft.setTextDatum(TL_DATUM);
}

static String uptimeString() {
  unsigned long up = millis() / 1000UL;
  return String(up / 86400UL) + "d" + String((up % 86400UL) / 3600UL) + "h" +
         String((up % 3600UL) / 60UL) + "m";
}

/* ---- signal strength bars ----------------------------------------------- */

/* 0 = no reading, 1 (weak) .. 4 (strong) */
static byte signalLevel(int rssi) {
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi < 0)    return 1;      /* any reading at all is worth one bar */
  return 0;
}

static uint16_t signalColour(byte level) {
  if (level >= 3) return TFT_GREEN;
  if (level == 2) return TFT_YELLOW;
  if (level == 1) return TFT_RED;
  return TFT_DARKGREY;
}

/* One label, four bars and the numeric reading. Unfilled bars stay dark so the
 * scale is visible and a dead link differs from a weak one. */
static int signalWidth(const char *label, const String &reading) {
  return tft.textWidth(label, 1) + 3 + SIG_W + 3 + tft.textWidth(reading, 1);
}

static void drawSignal(const char *label, int x, byte level, bool live, const String &reading) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(live ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(label, x, SIG_LABEL_Y, 1);

  int barsX = x + tft.textWidth(label, 1) + 3;
  for (int i = 0; i < SIG_BARS; i++) {
    int h = 3 + i * 3;                       /* 3, 6, 9, 12 */
    tft.fillRect(barsX + i * (SIG_BAR_W + SIG_BAR_GAP), SIG_BOTTOM - h,
                 SIG_BAR_W, h, (level > i) ? signalColour(level) : TFT_DARKGREY);
  }

  tft.setTextColor(live ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(reading, barsX + SIG_W + 3, SIG_LABEL_Y, 1);
}

/* status: 1 = up (green), 2 = down (red), 3 = down, blink off (dim), 4 = AP */
static byte lastChipState = 255;

static void drawChip(int x, const char *label, byte state) {
  if (lastChipState == state) return;
  lastChipState = state;

  uint16_t bg = TFT_DARKGREY;
  if (state == 1)      bg = TFT_DARKGREEN;
  else if (state == 2) bg = TFT_MAROON;
  else if (state == 4) bg = TFT_NAVY;

  tft.fillRoundRect(x, CHIP_Y, CHIP_W, CHIP_H, 3, bg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(label, x + 4, CHIP_Y + 4, 1);
}

/* -------------------------------------------------------------------------- */

static void drawHeader() {
  // Everything except the MQTT chip, which drawChip() paints to the right of
  // CHIP_X0, so clearing the full width here would erase it.
  clearRegion(0, 0, CHIP_X0 - 4, 24);

  // Measure before drawing: the two signal groups are packed right-aligned
  // against the MQTT chip, so a "-105" reading cannot run into its neighbour.
  String wifiTxt = (wifiMode == 1) ? String(wifiSignal) : String("--");
  String btTxt   = btConnected ? String(btRssi) : String("--");
  int btX   = CHIP_X0 - 6 - signalWidth("BT", btTxt);
  int wifiX = btX - 6 - signalWidth("WIFI", wifiTxt);

  String title = "BLUETTI";
  if (vModel.length()) title += " " + vModel;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  // font 1 if the model name would run into the WiFi indicator
  tft.drawString(title, 6, 5, (tft.textWidth(title, 2) <= wifiX - 10) ? 2 : 1);

  drawSignal("WIFI", wifiX, lastWifiLevel, wifiMode == 1, wifiTxt);
  drawSignal("BT",   btX,   lastBtLevel,   btConnected,   btTxt);
  headerDirty = false;
}

static void drawSocBar() {
  int pct = vBattery.toInt();
  pct = constrain(pct, 0, 100);
  int x = 4, w = COL_W - 8, h = BAR_H;
  clearRegion(x, Y_BAR, w, h);
  tft.drawRect(x, Y_BAR, w, h, TFT_DARKGREY);
  int fill = ((w - 2) * pct) / 100;
  if (fill > 0) tft.fillRect(x + 1, Y_BAR + 1, fill, h - 2, TFT_ORANGE);
}

static void drawMetrics() {
  const char *labels[5] = {"BATT", "AC IN", "AC OUT", "DC IN", "DC OUT"};
  const String *vals[5] = {&vBattery, &vAcIn, &vAcOut, &vDcIn, &vDcOut};
  const char *units[5]  = {"%", "W", "W", "W", "W"};

  for (int i = 0; i < 5; i++) {
    int cx = i * COL_W + COL_W / 2;
    // One column block: label, value and unit. Stops above the SOC bar.
    clearRegion(i * COL_W, Y_LABEL, COL_W, Y_UNIT + 10 - Y_LABEL);

    drawCentered(labels[i], cx, Y_LABEL, 2, TFT_LIGHTGREY);

    const String &v = *vals[i];
    // Only fonts 1, 2 and 4 are compiled in (LOAD_FONT* in platformio.ini).
    // Stepping down one at a time would try font 3, which does not exist.
    uint8_t font = 1;
    if (tft.textWidth(v, 4) <= COL_W - 4)      font = 4;
    else if (tft.textWidth(v, 2) <= COL_W - 4) font = 2;
    drawCentered(v, cx, Y_VALUE, font, (i == 0) ? TFT_ORANGE : TFT_WHITE);

    drawCentered(units[i], cx, Y_UNIT, 1, TFT_LIGHTGREY);
  }

  drawSocBar();
  metricsDirty = false;
}

static void drawDetail() {
  clearRegion(0, Y_DETAIL, SCR_W, 18);

  float mn = 0, mx = 0;
  bool any = false;
  for (int i = 0; i < 16; i++) {
    if (!cellValid[i] || cells[i] <= 0.0f) continue;
    if (!any) { mn = mx = cells[i]; any = true; }
    else {
      if (cells[i] < mn) mn = cells[i];
      if (cells[i] > mx) mx = cells[i];
    }
  }

  String s = "PACK " + vPack + "V   CELLS ";
  s += any ? (String(mn, 2) + "-" + String(mx, 2) + "V") : String("--");
  s += "   dMAX ";
  s += any ? (String(mx - mn, 2) + "V") : String("--");

  drawCentered(s, SCR_W / 2, Y_DETAIL, 2, TFT_WHITE);
  detailDirty = false;
}

/* One row that alternates between the network/uptime pair and the status.
 * Both views occupy the same line, which is what frees the second footer row. */
static void drawFooter() {
  clearRegion(0, Y_RULE, SCR_W, SCR_H - Y_RULE);
  tft.drawFastHLine(0, Y_RULE, SCR_W, TFT_DARKGREY);

  if (footerShowStatus) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(statusColour(vStatus), TFT_BLACK);
    tft.drawString(vStatus, SCR_W / 2, Y_FOOT, 2);
    tft.setTextDatum(TL_DATUM);
  } else {
    String uptime = "up " + uptimeString();

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("IP " + vIP, 6, Y_FOOT, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(uptime, SCR_W - 6, Y_FOOT, 2);
    tft.setTextDatum(TL_DATUM);
  }

  footerDirty = false;
}

/* -------------------------------------------------------------------------- */

/* ---- screens ------------------------------------------------------------- */

/* PORTS: the V/A/W triplet for each of the four ports. This is the data the
 * register investigation identified, and none of it fits on the power page. */
static void drawPortRow(int y, const char *label, const String &v, const String &a, const String &w) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(label, 6, y, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(v, 180, y, 2);
  tft.drawString(a, 252, y, 2);
  tft.drawString(w, SCR_W - 6, y, 2);
  tft.setTextDatum(TL_DATUM);
}

static void drawPagePorts() {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("PORTS", 6, 4, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("volts", 180, 4, 2);
  tft.drawString("amps", 252, 4, 2);
  tft.drawString("watts", SCR_W - 6, 4, 2);
  tft.setTextDatum(TL_DATUM);

  drawPortRow(34,  "AC OUT", vAcOutV, vAcOutA, vAcOut);
  drawPortRow(60,  "AC IN",  vAcInV,  vAcInA,  vAcIn);
  drawPortRow(86,  "DC OUT", vDcOutV, vDcOutA, vDcOut);
  drawPortRow(112, "DC IN",  vDcInV,  vDcInA,  vDcIn);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("AC OUT " + vAcOutHz + " Hz    PACK " + vPack + " V", 6, 138, 2);
}

/* CELLS: 4x4 grid, each cell coloured by how far it sits from the mean. */
static void drawPageCells() {
  float mn = 0, mx = 0, sum = 0;
  int n = 0;
  for (int i = 0; i < 16; i++) {
    if (!cellValid[i] || cells[i] <= 0.0f) continue;
    if (n == 0) { mn = mx = cells[i]; }
    if (cells[i] < mn) mn = cells[i];
    if (cells[i] > mx) mx = cells[i];
    sum += cells[i];
    n++;
  }
  float mean = n ? (sum / n) : 0.0f;

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("CELLS", 6, 4, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(n ? (String(mn, 2) + "-" + String(mx, 2) + "V  dMAX " + String(mx - mn, 2) + "V")
                   : String("no data"), SCR_W - 6, 4, 2);
  tft.setTextDatum(TL_DATUM);

  for (int i = 0; i < 16; i++) {
    int cellW = SCR_W / 4;
    int x = (i % 4) * cellW;
    int y = 30 + (i / 4) * 34;

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(String(i + 1), x + 4, y, 1);

    uint16_t colour = TFT_DARKGREY;
    String val = "--";
    if (cellValid[i] && cells[i] > 0.0f) {
      val = String(cells[i], 2);
      float d = fabsf(cells[i] - mean);
      colour = (d <= 0.015f) ? TFT_GREEN : (d <= 0.040f ? TFT_YELLOW : TFT_RED);
    }
    drawCentered(val, x + cellW / 2, y + 8, 2, colour);
  }
}

static void drawPageSystem() {
  ESPBluettiSettings s = get_esp32_bluetti_settings();

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("SYSTEM", 6, 4, 2);

  struct sys_line_t { String text; uint16_t colour; };
  const sys_line_t lines[] = {
    { "IP   " + vIP, TFT_WHITE },
    { "WiFi " + (wifiMode == 1 ? String(wifiSignal) : String("--")) +
      " dBm  BLE " + (btConnected ? String(btRssi) : String("--")) + " dBm", TFT_LIGHTGREY },
    { "MQTT " + String(s.mqtt_server) + ":" + String(s.mqtt_port), TFT_WHITE },
    { String("MQTT ") + (mqConnected ? "up" : "DOWN") +
      "   errors " + String(getPublishErrorCount()), mqConnected ? TFT_GREEN : TFT_RED },
    { "up " + uptimeString() + "  heap " + String(ESP.getFreeHeap()), TFT_LIGHTGREY },
    { "ARM " + vArm + "  DSP " + vDsp, TFT_LIGHTGREY },
    { "SN " + vSerial, TFT_LIGHTGREY },
  };

  int y = 28;
  for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
    tft.setTextColor(lines[i].colour, TFT_BLACK);
    tft.drawString(lines[i].text, 6, y, 2);
    y += 20;
  }
}

static void drawActivePage() {
  switch (currentPage) {
    case 1:  drawPagePorts();  break;
    case 2:  drawPageCells();  break;
    case 3:  drawPageSystem(); break;
    default: break;
  }
}

/* ---- button-driven screen control ---------------------------------------- */

void displayNextPage() {
  currentPage = (currentPage + 1) % PAGE_COUNT;
  pageDirty = true;
}

void displayPrevPage() {
  currentPage = (currentPage + PAGE_COUNT - 1) % PAGE_COUNT;
  pageDirty = true;
}

bool displayBacklightOn() { return backlightOn; }

void displaySetBacklight(bool on) {
  backlightOn = on;
  digitalWrite(PIN_LCD_BL, on ? HIGH : LOW);
}

void displayToggleBacklight() { displaySetBacklight(!backlightOn); }

/* Full-screen message, used to make a button action visible before it happens. */
void displayNotice(const String &text) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(text, SCR_W / 2, SCR_H / 2, 4);
  tft.setTextDatum(TL_DATUM);
}

/* Shown while the config portal is up. This has to draw immediately rather than
 * set a dirty flag: autoConnect() blocks inside initBWifi(), so setup() never
 * completes and loop() - and therefore handleDisplay() - never runs. That is also
 * why the panel used to sit on "IP: NoConf" forever until it was configured. */
void displaySetupScreen() {
  String ssid = WiFi.softAPSSID();
  if (ssid.length() == 0) ssid = "Bluetti_ESP32";   // portal not up yet
  String url = "http://" + WiFi.softAPIP().toString();

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("SETUP WIFI", 6, 6, 2);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("1  join network", 6, 40, 2);

  // The name is what has to be typed, so give it the large font when it fits.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(ssid, 6, 60, (tft.textWidth(ssid, 4) <= SCR_W - 12) ? 4 : 2);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("2  open", 6, 108, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(url, 6, 128, 2);

  // The Bluetooth id is the usual stumbling block, so point at the endpoint that
  // lists the names - it only works once the board is on the network.
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("then /scanBT lists Bluetooth names", 6, 152, 1);

  Serial.printf("[DISP] setup screen: join '%s' then open %s\n", ssid.c_str(), url.c_str());
}

/* -------------------------------------------------------------------------- */

/* Fields the dashboard actually shows. Anything else is not worth queueing. */
static bool isShown(enum field_names f) {
  if (f >= INTERNAL_CELL01_VOLTAGE && f <= INTERNAL_CELL16_VOLTAGE) return true;
  switch (f) {
    case DEVICE_TYPE:
    case TOTAL_BATTERY_PERCENT:
    case AC_INPUT_POWER:
    case AC_OUTPUT_POWER:
    case DC_INPUT_POWER:
    case DC_OUTPUT_POWER:
    case INTERNAL_PACK_VOLTAGE:
    /* PORTS screen */
    case INTERNAL_AC_VOLTAGE:
    case INTERNAL_CURRENT_ONE:
    case INTERNAL_AC_FREQUENCY:
    case AC_INPUT_VOLTAGE:
    case DC_OUTPUT_VOLTAGE:
    case DC_OUTPUT_CURRENT:
    case INTERNAL_DC_INPUT_VOLTAGE:
    case INTERNAL_DC_INPUT_CURRENT:
    /* SYSTEM screen */
    case ARM_VERSION:
    case DSP_VERSION:
    case SERIAL_NUMBER:
      return true;
    default:
      return false;
  }
}

/* Assign and report whether anything actually changed, so the screens only
 * repaint on a real change. */
static bool changed(String &dst, const char *value) {
  if (strcmp(dst.c_str(), value) == 0) return false;
  dst = value;
  return true;
}

void disp_setField(enum field_names field, const String &value) {
  if (dispQueue == nullptr || !isShown(field)) return;

  DispField msg;
  msg.field = (uint8_t)field;
  strlcpy(msg.value, value.c_str(), sizeof(msg.value));

  // Non-blocking: this runs in the NimBLE host task, so it must never wait.
  // A dropped sample is harmless, the next poll cycle repeats every field.
  xQueueSend(dispQueue, &msg, 0);
}

/* Runs in the loop task only. strcmp/atof instead of String comparison so the
 * steady state allocates nothing. */
static void applyField(uint8_t field, const char *value) {
  switch ((enum field_names)field) {
    case DEVICE_TYPE:
      if (changed(vModel, value))   { headerDirty = true;  dataDirty = true; }
      break;
    case TOTAL_BATTERY_PERCENT:
      if (changed(vBattery, value)) { metricsDirty = true; dataDirty = true; }
      break;
    case AC_INPUT_POWER:
      if (changed(vAcIn, value))    { metricsDirty = true; dataDirty = true; }
      break;
    case AC_OUTPUT_POWER:
      if (changed(vAcOut, value))   { metricsDirty = true; dataDirty = true; }
      break;
    case DC_INPUT_POWER:
      if (changed(vDcIn, value))    { metricsDirty = true; dataDirty = true; }
      break;
    case DC_OUTPUT_POWER:
      if (changed(vDcOut, value))   { metricsDirty = true; dataDirty = true; }
      break;
    case INTERNAL_PACK_VOLTAGE:
      if (changed(vPack, value))    { detailDirty = true;  dataDirty = true; }
      break;
    /* PORTS screen */
    case INTERNAL_AC_VOLTAGE:
      if (changed(vAcOutV, value))  { dataDirty = true; }
      break;
    case INTERNAL_CURRENT_ONE:
      if (changed(vAcOutA, value))  { dataDirty = true; }
      break;
    case INTERNAL_AC_FREQUENCY:
      if (changed(vAcOutHz, value)) { dataDirty = true; }
      break;
    case AC_INPUT_VOLTAGE:
      if (changed(vAcInV, value))   { dataDirty = true; }
      break;
    case AC_INPUT_CURRENT:
      if (changed(vAcInA, value))   { dataDirty = true; }
      break;
    case DC_OUTPUT_VOLTAGE:
      if (changed(vDcOutV, value))  { dataDirty = true; }
      break;
    case DC_OUTPUT_CURRENT:
      if (changed(vDcOutA, value))  { dataDirty = true; }
      break;
    case INTERNAL_DC_INPUT_VOLTAGE:
      if (changed(vDcInV, value))   { dataDirty = true; }
      break;
    case INTERNAL_DC_INPUT_CURRENT:
      if (changed(vDcInA, value))   { dataDirty = true; }
      break;
    /* SYSTEM screen */
    case ARM_VERSION:
      if (changed(vArm, value))     { dataDirty = true; }
      break;
    case DSP_VERSION:
      if (changed(vDsp, value))     { dataDirty = true; }
      break;
    case SERIAL_NUMBER:
      if (changed(vSerial, value))  { dataDirty = true; }
      break;
    default:
      if (field >= INTERNAL_CELL01_VOLTAGE && field <= INTERNAL_CELL16_VOLTAGE) {
        int idx = field - INTERNAL_CELL01_VOLTAGE;
        cells[idx] = atof(value);
        cellValid[idx] = true;
        detailDirty = true;
        dataDirty = true;
      }
      break;
  }
}

void initDisplay() {
  pinMode(PIN_LCD_POWER_ON, OUTPUT);
  digitalWrite(PIN_LCD_POWER_ON, HIGH);   // panel rail - required
  delay(10);

  tft.init();
  tft.setRotation(3);                     // native 170x320 -> landscape 320x170
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);         // backlight

  if (dispQueue == nullptr) {
    dispQueue = xQueueCreate(DISP_QUEUE_LEN, sizeof(DispField));
  }

  headerDirty = metricsDirty = detailDirty = footerDirty = true;
  handleDisplay();
}

void handleDisplay() {
  unsigned long now = millis();

  if (now - prevFlash >= FLASH_MS) {
    prevFlash = now;
    blinkPhase ^= 1;
  }

  // --- drain values handed over from the BLE task -------------------------
  DispField msg;
  while (dispQueue != nullptr && xQueueReceive(dispQueue, &msg, 0) == pdTRUE) {
    applyField(msg.field, msg.value);
  }

  // --- signal levels ------------------------------------------------------
  byte wifiLevel = (wifiMode == 1) ? signalLevel(wifiSignal) : 0;
  byte btLevel   = btConnected ? signalLevel(btRssi) : 0;
  if (wifiLevel != lastWifiLevel || btLevel != lastBtLevel) {
    lastWifiLevel = wifiLevel;
    lastBtLevel   = btLevel;
    headerDirty   = true;
    dataDirty     = true;            // the SYSTEM page shows the dBm values
  }

  // Sample the BLE link RSSI from the loop task. It must not be read from a
  // NimBLE callback: getRssi() is a synchronous host call and the callbacks are
  // executed by the host task itself, so it would deadlock.
  if (now - prevBtRssi >= BT_RSSI_MS) {
    prevBtRssi = now;
    int r = getBTRssi();
    if (r != btRssi) {
      btRssi = r;
      headerDirty = true;
      dataDirty = true;
    }
  }

  // --- repaint ------------------------------------------------------------
  if (pageDirty) {                   // page switch: everything
    tft.fillScreen(TFT_BLACK);
    headerDirty = metricsDirty = detailDirty = footerDirty = true;
    lastChipState = 255;             // drawChip() caches its state
    pageDirty = false;
    dataDirty = true;
  }

  if (currentPage == 0) {
    // Page 0 repaints by region so live values do not flicker the whole screen.
    drawChip(CHIP_X0, "MQTT", mqConnected ? 1 : (blinkPhase ? 3 : 2));
    if (headerDirty)  drawHeader();
    if (metricsDirty) drawMetrics();
    if (detailDirty)  drawDetail();
    if (footerDirty)  drawFooter();
  } else if (dataDirty) {
    tft.fillScreen(TFT_BLACK);
    drawActivePage();
  }
  dataDirty = false;

  if (now - prevRuntime >= RUNTIME_MS) {
    prevRuntime = now;
    footerDirty = true;                   // uptime moves on
  }

  // Flip the footer between the IP/uptime pair and the status.
  if (now - prevFooter >= FOOTER_MS) {
    prevFooter = now;
    footerShowStatus = !footerShowStatus;
    footerDirty = true;
  }
}

void displayOff() {
  tft.fillScreen(TFT_BLACK);
  digitalWrite(PIN_LCD_BL, LOW);
  digitalWrite(PIN_LCD_POWER_ON, LOW);
}

/* --- setters called from the loop task ---------------------------------- */

void wrDisp_IP(String strIP) {
  if (strIP == vIP) return;
  vIP = strIP;
  footerDirty = true;
}

void wrDisp_Status(String strStatus) {
  if (strStatus == vStatus) return;
  vStatus = strStatus;
  footerDirty = true;
}

void disp_setStatus(String strStatus) { wrDisp_Status(strStatus); }

void wrDisp_wifisignal(int intMode, int intSignal) {
  wifiMode = (byte)intMode;
  wifiSignal = intSignal;
  headerDirty = true;
}

void disp_setWifiSignal(int extWifMode, int extSignal) { wrDisp_wifisignal(extWifMode, extSignal); }

void disp_setWifiMode(byte wMode) {
  if (wMode == wifiMode) return;
  wifiMode = wMode;
  headerDirty = true;
}

/* --- setters also reachable from the BLE task: plain scalars only -------- */

void disp_setBlueTooth(bool boolBtConn)       { btConnected = boolBtConn; }
void disp_setMqttStatus(bool blMqttconnected) { mqConnected = blMqttconnected; }
