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

/* --- register sweep ------------------------------------------------------
 * Queue one MODBUS read of SWEEP_QTY registers and answer it through
 * GET /readRegs. tools/sweep_regs.py drives this to walk the whole
 * 0x0000-0x1FFF address space: the field map only names registers somebody has
 * already identified, so reading everything is how the rest get found.
 *
 * The bridge cannot sweep by itself - the 3 s poll cadence would make 800 blocks
 * take the better part of an hour - so the reads are driven from a PC.
 *
 * One request at a time, and the normal poll pauses while one is outstanding so
 * the two cannot interleave. A sweep answer is matched by its expected frame
 * length rather than through the poll's command queue, because an address the
 * station refuses to serve would otherwise shift that queue and publish every
 * later value against the wrong field.
 *
 * requestReadRegs returns false if a read is already outstanding, BLE is down,
 * or the block would run past the end of its page. takeSweepResult returns false
 * if the answer has not arrived (or was already taken). */
#define SWEEP_QTY      10
#define SWEEP_WAIT_MS  3500
bool requestReadRegs(uint8_t page, uint8_t offset);
bool takeSweepResult(uint8_t *dst, size_t dstSize, size_t *len);
#endif
