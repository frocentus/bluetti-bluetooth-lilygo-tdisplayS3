#ifndef BTOOTH_H
#define BTOOTH_H
#include "Arduino.h"
#include "NimBLEDevice.h"

/* Shared BLE state.
 * These used to be `static` in this header, which gave every translation unit
 * its own private copy: BTooth.cpp set its own `connected` while any other
 * reader saw a permanently stale one. Exactly one definition of each now lives
 * in BTooth.cpp.
 * (NimBLE-Arduino 1.4.x maps BLE* onto NimBLE* with macros in NimBLEDevice.h,
 * which is why the upstream BLE* spellings still compile.) */
extern boolean doConnect;
extern boolean connected;
extern BLERemoteCharacteristic* pRemoteWriteCharacteristic;
extern BLERemoteCharacteristic* pRemoteNotifyCharacteristic;
extern BLEAdvertisedDevice* bluettiDevice;

typedef struct __attribute__ ((packed)) {
  uint8_t prefix;            // 1 byte   
  uint8_t field_update_cmd;  // 1 byte 
  uint8_t page;              // 1 byte 
  uint8_t offset;            // 1 byte
  uint16_t len;              // 2 bytes  
  uint16_t check_sum;        // 2 bytes  
} bt_command_t;   


// The remote Bluetti service we wish to connect to.
extern BLEUUID serviceUUID;

// The characteristics of Bluetti Devices
extern BLEUUID    WRITE_UUID;
extern BLEUUID    NOTIFY_UUID;

void btResetStack();
extern void initBluetooth();
extern void handleBluetooth();
bool connectToServer();
extern void handleBTCommandQueue();
extern void sendBTCommand(bt_command_t command);
extern bool isBTconnected();
extern int getBTRssi();
extern unsigned long getLastBTMessageTime();

/* Names of the advertising devices heard by the most recent scan, formatted for
 * the /scanBT endpoint. Used to read a power station's Bluetooth name off a list
 * rather than typing it. */
String btScanNamesReport();

/* Diagnostic: the most recent raw BLE response, exactly as received.
 *
 * The page-0 poll returns ~254 bytes covering every register this firmware
 * knows about, and this device's field map is community-sourced and
 * demonstrably imperfect - so being able to read the page verbatim (including
 * registers the map does not name yet) beats guessing from published values.
 * Exposed by GET /rawPage in BWifi.cpp.
 *
 * Returns false if no frame for that page has arrived. The copy is guarded by
 * a critical section because the NimBLE task writes it and the async web task
 * reads it. */
#define RAW_FRAME_MAX 300
bool getRawPageFrame(uint8_t page, uint8_t *dst, size_t dstSize, size_t *len, uint8_t *offset);
#endif
