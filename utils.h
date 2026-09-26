#ifndef UTILS_H
#define UTILS_H
#include "Arduino.h"

extern uint16_t swap_bytes(uint16_t number);
extern uint16_t modbus_crc(uint8_t buf[], int len);

/* Serial is written from more than one task - the NimBLE host task logs BLE
 * frames when DEBUG_BT_FRAMES is on, and the loop task logs MQTT activity. The
 * Arduino stream chunks long writes, so two tasks logging at once splice lines
 * into each other and a capture stops being trustworthy. Hold this around any
 * sequence of prints that must stay contiguous. */
void serialLockInit();
class SerialLock {
 public:
  SerialLock();
  ~SerialLock();
};

#endif
