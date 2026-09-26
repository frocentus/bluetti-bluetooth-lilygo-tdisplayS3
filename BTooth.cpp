#include "BluettiConfig.h"
#include "BTooth.h"
#include "utils.h"
#include "PayloadParser.h"
#include "BWifi.h"
#include "display_tft.h"

/* Single definitions for the BLE state declared extern in BTooth.h */
boolean doConnect = false;
boolean connected = false;
BLERemoteCharacteristic* pRemoteWriteCharacteristic = nullptr;
BLERemoteCharacteristic* pRemoteNotifyCharacteristic = nullptr;
BLEAdvertisedDevice* bluettiDevice = nullptr;
static BLEClient* bleClient = nullptr;   // kept so getBTRssi() can query the link

BLEUUID serviceUUID("0000ff00-0000-1000-8000-00805f9b34fb");
BLEUUID WRITE_UUID("0000ff02-0000-1000-8000-00805f9b34fb");
BLEUUID NOTIFY_UUID("0000ff01-0000-1000-8000-00805f9b34fb");

/* most recent raw frame, for GET /rawPage - see BTooth.h */
static uint8_t rawFrame[RAW_FRAME_MAX];
static size_t  rawFrameLen    = 0;
static uint8_t rawFramePage   = 0xFF;
static uint8_t rawFrameOffset = 0;
static portMUX_TYPE rawFrameMux = portMUX_INITIALIZER_UNLOCKED;

bool getRawPageFrame(uint8_t page, uint8_t *dst, size_t dstSize, size_t *len, uint8_t *offset) {
  bool found = false;
  portENTER_CRITICAL(&rawFrameMux);
  if (rawFrameLen > 0 && rawFramePage == page && rawFrameLen <= dstSize) {
    memcpy(dst, rawFrame, rawFrameLen);
    *len = rawFrameLen;
    *offset = rawFrameOffset;
    found = true;
  }
  portEXIT_CRITICAL(&rawFrameMux);
  return found;
}


int pollTick = 0;

struct command_handle {
  uint8_t page;
  uint8_t offset;
  int length;
};

QueueHandle_t commandHandleQueue;
QueueHandle_t sendQueue;

unsigned long lastBTMessage = 0;
static unsigned long lastBleScan = 0;
/* How many advertising devices the last scan saw, and how many consecutive scans
 * have seen none at all - used to tell a wedged stack from an absent station. */
static volatile uint16_t scanDevicesSeen = 0;
static int emptyScans = 0;
/* Set once a connection has actually been established, so the wedged-stack reboot
 * cannot fire on a board that has never worked. */
static bool everConnected = false;

/* --- names heard by the last scan, served by GET /scanBT ------------------
 * For configuring a board on site: the Bluetooth id has to match the station's
 * advertisement exactly, and reading it from a list beats typing it from a phone.
 * Written by the scan callback (NimBLE host task), read by the web task, so both
 * directions copy under the lock. */
#define SCAN_NAME_LEN 24
#define SCAN_NAME_MAX 12

static char scanNames[SCAN_NAME_MAX][SCAN_NAME_LEN];
static uint8_t scanNameCount = 0;
static unsigned long scanNamesAt = 0;
static portMUX_TYPE scanNamesMux = portMUX_INITIALIZER_UNLOCKED;

/* loop task: start a fresh list before each scan */
static void scanNamesReset() {
  portENTER_CRITICAL(&scanNamesMux);
  scanNameCount = 0;
  scanNamesAt = millis();
  portEXIT_CRITICAL(&scanNamesMux);
}

/* NimBLE host task: record one advertised name */
static void scanNamesAdd(const char *name) {
  if (name == nullptr || name[0] == '\0') return;
  portENTER_CRITICAL(&scanNamesMux);
  bool dup = false;
  for (uint8_t i = 0; i < scanNameCount; i++) {
    if (strcmp(scanNames[i], name) == 0) { dup = true; break; }
  }
  if (!dup && scanNameCount < SCAN_NAME_MAX) {
    strlcpy(scanNames[scanNameCount], name, SCAN_NAME_LEN);
    scanNameCount++;
  }
  portEXIT_CRITICAL(&scanNamesMux);
}

String btScanNamesReport() {
  char copy[SCAN_NAME_MAX][SCAN_NAME_LEN];
  uint8_t count;
  unsigned long at;
  portENTER_CRITICAL(&scanNamesMux);
  memcpy(copy, scanNames, sizeof(copy));
  count = scanNameCount;
  at = scanNamesAt;
  portEXIT_CRITICAL(&scanNamesMux);

  String out = "Bluetooth devices heard by the last scan";
  if (at != 0) out += " (" + String((millis() - at) / 1000UL) + "s ago)";
  out += ":\n";
  if (count == 0) {
    out += "  (none)\n";
  } else {
    for (uint8_t i = 0; i < count; i++) out += "  " + String(copy[i]) + "\n";
  }
  out += "\nA scan runs every " + String(BLUETOOTH_SCAN_INTERVAL_IN_SECONDS) +
         "s while no station is connected, so reload to refresh.\n";
  out += "The Bluetooth ID must match one of these exactly:\n";
  out += "  /setBluettiID?value=<name>\n";
  return out;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    { SerialLock lock; Serial.println(F("BLE - onConnect")); }
    disp_setBlueTooth(true);
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    { SerialLock lock; Serial.println(F("BLE - onDisconnect")); }
    disp_setBlueTooth(false);
    #ifdef RELAISMODE
      #ifdef DEBUG
        Serial.println(F("deactivate relais contact"));
      #endif
      digitalWrite(RELAIS_PIN, RELAIS_LOW);
    #endif
  }
};

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class BluettiAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice *advertisedDevice) {
    scanDevicesSeen++;        // any device counts, for the wedged-stack check
    scanNamesAdd(advertisedDevice->getName().c_str());
#if DEBUG_BT_FRAMES
    {
      SerialLock lock;
      Serial.print(F("[BLE] Advertised Device found: "));
      Serial.println(advertisedDevice->toString().c_str());
    }
#endif

     ESPBluettiSettings settings = get_esp32_bluetti_settings();
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID) && (strcmp(advertisedDevice->getName().c_str(),settings.bluetti_device_id)==0) ) {
      BLEDevice::getScan()->stop();
      bluettiDevice = advertisedDevice;
      doConnect = true;
    }
  } 
};

void initBluetooth(){
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BluettiAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  scanNamesReset();
  pBLEScan->start(5, false);
  
  commandHandleQueue = xQueueCreate( 5, sizeof(bt_command_t ) );
  sendQueue = xQueueCreate( 5, sizeof(bt_command_t) );
}


static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

#if DEBUG_BT_FRAMES
    {
      SerialLock lock;      // keep the dump contiguous against the loop task's logging
      Serial.println("[BT] F01 - Write Response");
      for (int i = 1; i <= length; i++) {
        Serial.printf("%02x", pData[i-1]);
        if (i % 2 == 0) Serial.print(" ");
        if (i % 16 == 0) Serial.println();
      }
      Serial.println();
    }
#endif

    bt_command_t command_handle;
    // Zero timeout: this runs in the NimBLE host task, so blocking here stalls
    // the BLE stack. A response with no matching queued command is dropped.
    if(xQueueReceive(commandHandleQueue, &command_handle, 0)){
      // Keep a verbatim copy of the register page for GET /rawPage, before
      // parsing, so it shows what actually arrived rather than what the map
      // happens to name.
      if (length > 0 && length <= RAW_FRAME_MAX) {
        portENTER_CRITICAL(&rawFrameMux);
        memcpy(rawFrame, pData, length);
        rawFrameLen    = length;
        rawFramePage   = command_handle.page;
        rawFrameOffset = command_handle.offset;
        portEXIT_CRITICAL(&rawFrameMux);
      }
      parse_bluetooth_data(command_handle.page, command_handle.offset, pData, length);
    }
   
}

bool connectToServer() {
    Serial.print(F("[BT] Forming a connection to "));
    Serial.println(bluettiDevice->getAddress().toString().c_str());

    BLEDevice::setMTU(517); // set client to request maximum MTU from server (default is 23 otherwise)
    BLEClient*  pClient  = BLEDevice::createClient();
    bleClient = pClient;
    Serial.println(F("[BT] - Created client"));

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(bluettiDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(F("[BT] - Connected to server"));
    // pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print(F("[BT] Failed to find our service UUID: "));
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our service"));


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteWriteCharacteristic = pRemoteService->getCharacteristic(WRITE_UUID);
    if (pRemoteWriteCharacteristic == nullptr) {
      Serial.print(F("[BT] Failed to find our characteristic UUID: "));
      Serial.println(WRITE_UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our Write characteristic"));

        // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteNotifyCharacteristic = pRemoteService->getCharacteristic(NOTIFY_UUID);
    if (pRemoteNotifyCharacteristic == nullptr) {
      Serial.print(F("[BT] Failed to find our characteristic UUID: "));
      Serial.println(NOTIFY_UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(F("[BT] - Found our Write characteristic"));

    // Read the value of the characteristic.
    if(pRemoteWriteCharacteristic->canRead()) {
      std::string value = pRemoteWriteCharacteristic->readValue();
      Serial.print(F("[BT] The characteristic value was: "));
      Serial.println(value.c_str());
    }

    if(pRemoteNotifyCharacteristic->canNotify())
      pRemoteNotifyCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    everConnected = true;
     #ifdef RELAISMODE
      #ifdef DEBUG
        Serial.println(F("[BT] activate relais contact"));
      #endif
      digitalWrite(RELAIS_PIN, RELAIS_HIGH);
    #endif

    return true;
}


void handleBTCommandQueue(){

    bt_command_t command;
    if(xQueueReceive(sendQueue, &command, 0)) {
      
#if DEBUG_BT_FRAMES
    {
      SerialLock lock;
      Serial.print("[BT] Write Request FF02 - Value: ");
      for (int i = 0; i < 8; i++) {
        if (i % 2 == 0) Serial.print(" ");
        Serial.printf("%02x", ((uint8_t*)&command)[i]);
      }
      Serial.println("");
    }
#endif
      pRemoteWriteCharacteristic->writeValue((uint8_t*)&command, sizeof(command),true);
 
     };  
}

void sendBTCommand(bt_command_t command){
    bt_command_t cmd = command;
    xQueueSend(sendQueue, &cmd, 0);
}

void handleBluetooth(){

  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println(F("We are now connected to the Bluetti BLE Server."));
    } else {
      Serial.println(F("We have failed to connect to the server; there is nothing more we will do."));
    }
    doConnect = false;
  }

  if ((millis() - lastBTMessage) > (MAX_DISCONNECTED_TIME_UNTIL_REBOOT * 60000)){ 
#if defined(SLEEP_TIME_ON_BT_NOT_AVAIL) && (SLEEP_TIME_ON_BT_NOT_AVAIL > 0)
    Serial.println(F("[BT] disconnected over allowed limit, sleeping"));
    displayOff(); // otherwise the panel stays lit for the whole sleep
    esp_deep_sleep_start();
#elif defined(SLEEP_TIME_ON_BT_NOT_AVAIL)
    // 0 = never give up. Stay running and let the periodic rescan below find the
    // station when it comes back; this is what the config comment always claimed
    // 0 did, while the code actually slept. Sleeping here is what used to take the
    // bridge off the network for as long as the power station was switched off.
    static bool unreachableAnnounced = false;
    if (!unreachableAnnounced) {
      unreachableAnnounced = true;
      Serial.println(F("[BT] station unreachable - staying up and rescanning"));
    }
#else
    Serial.println(F("[BT] disconnected over allowed limit, reboot device"));
    ESP.restart();
#endif
  }

  if (connected) {

    // poll for device state
    if ( millis() - lastBTMessage > BLUETOOTH_QUERY_MESSAGE_DELAY){

       bt_command_t command;
       command.prefix = 0x01;
       command.field_update_cmd = 0x03;
       command.page = bluetti_polling_command[pollTick].f_page;
       command.offset = bluetti_polling_command[pollTick].f_offset;
       command.len = (uint16_t) bluetti_polling_command[pollTick].f_size << 8;
       command.check_sum = modbus_crc((uint8_t*)&command,6);

       xQueueSend(commandHandleQueue, &command, portMAX_DELAY);
       xQueueSend(sendQueue, &command, portMAX_DELAY);

       if (pollTick == sizeof(bluetti_polling_command)/sizeof(device_field_data_t)-1 ){
           pollTick = 0;
       } else {
           pollTick++;
       }
            
      lastBTMessage = millis();
    }

    handleBTCommandQueue();
    
  }else if(millis() - lastBleScan > (BLUETOOTH_SCAN_INTERVAL_IN_SECONDS * 1000UL)){
    // Periodic re-scan while disconnected. Without this the only attempt was the
    // 5 second window in initBluetooth(), so a missed scan meant waiting out the
    // 5 minute timeout for a reboot to try again - which is what left the bridge
    // sitting disconnected after the station was power cycled.
    lastBleScan = millis();
    Serial.println(F("[BT] disconnected, rescanning for the Bluetti"));
    scanDevicesSeen = 0;
    scanNamesReset();
    BLEDevice::getScan()->start(BLUETOOTH_SCAN_DURATION_IN_SECONDS);

    /* BLUETOOTH_MAX_RETRIES_BEFORE_REBOOT is a wedged-stack check, not an
     * absent-station one. A scan that saw no advertising device at all points at
     * a broken BLE stack, whereas a powered-off station still leaves the
     * neighbourhood's other devices visible - so only the empty case counts
     * towards the reboot.
     * It additionally requires that this bridge has connected at least once since
     * boot: a board in a quiet house, or one given a mistyped Bluetooth id, would
     * otherwise reboot itself every few minutes forever without ever being at
     * fault. */
    if (scanDevicesSeen == 0) {
      if (everConnected && ++emptyScans > BLUETOOTH_MAX_RETRIES_BEFORE_REBOOT) {
        Serial.println(F("[BT] BLE stack saw no devices at all, rebooting"));
        ESP.restart();
      }
    } else {
      emptyScans = 0;
    }
  }
}

void btResetStack()
{
  connected=false;

}

bool isBTconnected(){
  return connected;
}

/* BLE link RSSI in dBm, or 0 when there is no reading.
 * Call from the loop task only: getRssi() is a synchronous call into the NimBLE
 * host, and the host task is what executes the BLE callbacks - calling it from
 * one of those would deadlock. */
int getBTRssi(){
  if (!connected || bleClient == nullptr) return 0;
  return bleClient->getRssi();
}

unsigned long getLastBTMessageTime(){
    return lastBTMessage;
}


