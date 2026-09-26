#include "buttons.h"
#include "config.h"
#include "display_tft.h"

/* A contact has to hold its new state for this long before we believe it. */
#define BTN_DEBOUNCE_MS 40
/* Holding this long counts as a long press and cancels the click. */
#define BTN_LONG_MS     1200

static ButtonState stBtn1 = {};
static ButtonState stBtn2 = {};
static bool rebootArmed = false;

/* -------------------------------------------------------------------------- */

button_event buttonFeed(ButtonState &st, bool pressed, unsigned long now) {
  if (pressed != st.raw) {
    st.raw = pressed;
    st.rawChangedAt = now;
  }

  if (st.raw != st.stable && (now - st.rawChangedAt) >= BTN_DEBOUNCE_MS) {
    st.stable = st.raw;
    if (st.stable) {
      st.pressedAt = now;
      st.longFired = false;
    } else if (!st.longFired) {
      return BTN_CLICK;             /* released without ever going long */
    }
  }

  if (st.stable && !st.longFired && (now - st.pressedAt) >= BTN_LONG_MS) {
    st.longFired = true;
    return BTN_LONG;
  }

  return BTN_NONE;
}

/* -------------------------------------------------------------------------- */

static int selftestFailures = 0;

static void check(const char *name, bool ok) {
  Serial.printf("[BTN] selftest %-38s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) selftestFailures++;
}

/* Feed one raw level across a time window, counting the events it produces. */
static void driveCount(ButtonState &st, bool pressed, unsigned long from,
                       unsigned long to, int &clicks, int &longs) {
  for (unsigned long t = from; t <= to; t += 5) {
    button_event e = buttonFeed(st, pressed, t);
    if (e == BTN_CLICK)      clicks++;
    else if (e == BTN_LONG)  longs++;
  }
}

bool buttonsSelfTest() {
  selftestFailures = 0;

  { /* clean click: one click, no long */
    ButtonState st = {}; int c = 0, l = 0;
    driveCount(st, false, 0,   100, c, l);     /* idle        */
    driveCount(st, true,  105, 305, c, l);     /* held briefly */
    driveCount(st, false, 310, 410, c, l);     /* released    */
    check("clean click -> 1 click, 0 long", c == 1 && l == 0);
  }

  { /* long press fires once and suppresses the click on release */
    ButtonState st = {}; int c = 0, l = 0;
    driveCount(st, true,  0,    3000, c, l);
    driveCount(st, false, 3005, 3200, c, l);
    check("long press -> 1 long, 0 clicks", c == 0 && l == 1);
  }

  { /* contact bounce shorter than the debounce window must be ignored */
    ButtonState st = {}; int c = 0, l = 0;
    for (unsigned long b = 0; b < 30; b += 5) {
      driveCount(st, (b % 10) == 0, b, b + 4, c, l);
    }
    driveCount(st, true,  35,  235, c, l);
    driveCount(st, false, 240, 340, c, l);
    check("bounce then press -> 1 click", c == 1 && l == 0);
  }

  { /* just under the long threshold is still a click */
    ButtonState st = {}; int c = 0, l = 0;
    driveCount(st, true,  0,    1100, c, l);
    driveCount(st, false, 1105, 1200, c, l);
    check("press < long threshold -> 1 click", c == 1 && l == 0);
  }

  Serial.printf("[BTN] selftest %s (%d failure(s))\n",
                selftestFailures ? "FAILED" : "all passed", selftestFailures);
  return selftestFailures == 0;
}

/* -------------------------------------------------------------------------- */

void initButtons() {
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  stBtn1 = ButtonState{};
  stBtn2 = ButtonState{};
  #ifdef DEBUG
    buttonsSelfTest();
  #endif
}

void handleButtons() {
  unsigned long now = millis();
  button_event e1 = buttonFeed(stBtn1, digitalRead(PIN_BUTTON_1) == LOW, now);
  button_event e2 = buttonFeed(stBtn2, digitalRead(PIN_BUTTON_2) == LOW, now);

  /* With the backlight off, the first press only lights the screen again and is
   * consumed. Otherwise a press in the dark would page blindly - or, worse, land
   * on BUTTON_1's long press and reboot the bridge. */
  if (!displayBacklightOn()) {
    if (e1 != BTN_NONE || e2 != BTN_NONE) {
      displaySetBacklight(true);
    }
    return;
  }

  if (e1 == BTN_CLICK) displayPrevPage();
  if (e2 == BTN_CLICK) displayNextPage();
  if (e2 == BTN_LONG)  displayToggleBacklight();

  if (e1 == BTN_LONG) {
    /* BUTTON_1 is GPIO0, which is the ESP32-S3 BOOT strapping pin. The long press
     * fires while the button is still held, so restarting here would reset with
     * GPIO0 low - the ROM would sample that and drop into download mode, and the
     * app would never start. Arm it instead and wait for the release. */
    displayNotice("Release to reboot");
    rebootArmed = true;
  }

  if (rebootArmed && digitalRead(PIN_BUTTON_1) == HIGH) {
    displayNotice("Rebooting...");
    delay(300);                       /* readable before the screen goes dark */
    ESP.restart();
  }
}
