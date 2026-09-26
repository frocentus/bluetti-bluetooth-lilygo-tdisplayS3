#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

/* The two T-Display S3 buttons.
 *   BUTTON_2 (GPIO14)  short press = next page, long press = backlight
 *   BUTTON_1 (GPIO0)   short press = previous page, long press = reboot
 * See config.h for why GPIO0 deserves care. */

void initButtons();
void handleButtons();

/* --- debounce / click / long-press decision ------------------------------
 * Deliberately separated from the GPIO read: it is a pure function of
 * (sample, timestamp), which is what makes buttonsSelfTest() possible without
 * hardware. Not using the OneButton library (which LilyGO's own examples use)
 * because the version in the sketchbook (2.0.4) does not match any version in
 * the PlatformIO registry - this is 20 lines and no version negotiation.
 * ------------------------------------------------------------------------ */
enum button_event {
  BTN_NONE = 0,
  BTN_CLICK,
  BTN_LONG
};

struct ButtonState {
  bool raw;                  /* most recent sample */
  bool stable;               /* debounced */
  unsigned long rawChangedAt;
  unsigned long pressedAt;
  bool longFired;
};

/* `pressed` is the logical state: true while the button is held down. */
button_event buttonFeed(ButtonState &st, bool pressed, unsigned long now);

/* Exercises buttonFeed() against synthetic samples and prints the result.
 * Returns true when every case behaved. */
bool buttonsSelfTest();

#endif
