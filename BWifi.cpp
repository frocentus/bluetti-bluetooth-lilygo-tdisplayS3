#include "BluettiConfig.h"
#include "BWifi.h"
#include "BTooth.h"
#include "MQTT.h"
#include "index.h"  //Web page header file
#include <EEPROM.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer/archive/master.zip
#include <AsyncTCP.h> // https://github.com/me-no-dev/AsyncTCP/archive/master.zip
#include <ESPmDNS.h>
#include <ElegantOTA.h> // https://github.com/ayushsharma82/ElegantOTA
#include "display_tft.h"

AsyncWebServer server(80);
AsyncEventSource events("/events");

unsigned long lastTimeWebUpdate = 0;  

String lastMsg = ""; 

bool msgViewerDetails = false;
bool shouldSaveConfig = false;
int wifiReconnectCounter = 0;

/* --- message viewer queue -------------------------------------------------
 * AddtoMsgView() is called from whichever task happens to be logging:
 * publishTopic() runs in the NimBLE host task and calls it on every field
 * (~35 times per 3s poll cycle), the /setBluettiID handler runs in the async web
 * task, and handleWebserver() in the loop task. lastMsg is a String, so it gets a
 * single owner - the loop task - and messages cross as fixed-size queue entries.
 * Mutating the String from several tasks at once raced on the heap and the
 * refcount, which is a latent crash rather than a visible bug.
 *
 * Declared up here because initBWifi() and handleWebserver() both need it.
 * ------------------------------------------------------------------------ */
#define MSG_ENTRY_LEN 112
#define MSG_QUEUE_LEN 32
struct MsgEntry { char text[MSG_ENTRY_LEN]; };
static QueueHandle_t msgQueue = nullptr;

/* loop task only - owns lastMsg */
static void appendMsgView(const char *data){
  int firstPos = lastMsg.indexOf("</p>");
  int nextPos = firstPos;
  int numEntry = 0;
  while(nextPos > 0){
    nextPos = lastMsg.indexOf("</p>",nextPos+4);
    if (nextPos > 0){
      numEntry++;
    }
  }

  if (numEntry > MSG_VIEWER_ENTRY_COUNT-2){
    lastMsg = lastMsg.substring(firstPos+4) + "<p>" + data + "</p>";
  }
  else{
    lastMsg = lastMsg + "<p>" + data + "</p>";
  }
}

/* loop task only - moves whatever the other tasks queued into lastMsg */
static void drainMsgView(){
  MsgEntry entry;
  while (msgQueue != nullptr && xQueueReceive(msgQueue, &entry, 0) == pdTRUE){
    appendMsgView(entry.text);
  }
}

char mqtt_server[40] = "127.0.0.1";
char mqtt_port[6]  = "1883";
char bluetti_device_id[40] = "e.g. ACXXXYYYYYYYY";

void saveConfigCallback () {
  shouldSaveConfig = true;
}


ESPBluettiSettings wifiConfig;

ESPBluettiSettings get_esp32_bluetti_settings(){
    return wifiConfig;
}

void eeprom_read(){
  Serial.println(F("Loading Values from EEPROM"));
  EEPROM.begin(512);
  EEPROM.get(0, wifiConfig);
  EEPROM.end();
}

void eeprom_saveconfig(){
  Serial.println(F("Saving Values to EEPROM"));
  EEPROM.begin(512);
  EEPROM.put(0, wifiConfig);
  EEPROM.commit();
  EEPROM.end();
}

/* --- configuration endpoints --------------------------------------------
 * /setBluettiID and /resetConfig change persistent configuration, so they are
 * guarded by the OTA credentials. If no OTA username is configured they stay
 * open, which is how ElegantOTA itself behaves - refusing outright would lock
 * a device with no credentials out of its own recovery path.
 *
 * Note this is plain HTTP, so Basic auth only keeps casual LAN traffic out;
 * treat ota_password as a throwaway rather than a password you care about.
 * ---------------------------------------------------------------------- */
static bool requireAuth(AsyncWebServerRequest *request) {
  if (strlen(wifiConfig.ota_username) == 0) return true;
  if (request->authenticate(wifiConfig.ota_username, wifiConfig.ota_password)) return true;
  request->requestAuthentication(AsyncAuthType::AUTH_BASIC, DEVICE_NAME);
  return false;
}

/* The id is strcmp'd against the BLE advertised name and embedded in MQTT
 * topics, so reject anything that would corrupt the topic tree. */
static bool validBluettiID(const String &v) {
  if (v.length() == 0 || v.length() >= (int)sizeof(wifiConfig.bluetti_device_id)) return false;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '/' || c == '+' || c == '#' || (unsigned char)c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

void initBWifi(bool resetWifi){

  eeprom_read();

  // Owned by the loop task; AddtoMsgView() only enqueues to it. Must exist before
  // anything can log, so create it here rather than lazily.
  if (msgQueue == nullptr) {
    msgQueue = xQueueCreate(MSG_QUEUE_LEN, sizeof(MsgEntry));
  }

  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server Address", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Server Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "MQTT Username", "", 40);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", "", 40, "type=password");
  WiFiManagerParameter custom_ota_username("ota_username", "OTA Username", "", 40);
  WiFiManagerParameter custom_ota_password("ota_password", "OTA Password", "", 40, "type=password");
  WiFiManagerParameter custom_bluetti_device("bluetti", "Bluetti Bluetooth ID", bluetti_device_id, 40);

  WiFiManager wifiManager;

  if (resetWifi){
    wifiManager.resetSettings();
    ESPBluettiSettings defaults;
    wifiConfig = defaults;
    eeprom_saveconfig();
  } else if (wifiConfig.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    ESPBluettiSettings defaults;
    wifiConfig = defaults;
  } else {
    wifiManager.setConfigPortalTimeout(300);
  }

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_ota_username);
  wifiManager.addParameter(&custom_ota_password);
  wifiManager.addParameter(&custom_bluetti_device);
  
  wifiManager.setAPCallback([&](WiFiManager* wifiManager) {
		Serial.printf("Entered config mode:ip=%s, ssid='%s'\n", 
                        WiFi.softAPIP().toString().c_str(), 
                        wifiManager->getConfigPortalSSID().c_str());
                        wrDisp_wifisignal(2); //AP mode
                        wrDisp_IP(WiFi.softAPIP().toString());
                        wrDisp_Status("Setup Wifi");
                        // Draw it now: loop() is not running while autoConnect()
                        // blocks here, so the dirty flags above would never be
                        // flushed and the panel would sit on "IP: NoConf".
                        displaySetupScreen();
	});
  
  if (!wifiManager.autoConnect("Bluetti_ESP32")) {
    ESP.restart();
  }

  if (shouldSaveConfig) {
     strlcpy(wifiConfig.mqtt_server, custom_mqtt_server.getValue(), 40);
     strlcpy(wifiConfig.mqtt_port, custom_mqtt_port.getValue(), 6);
     strlcpy(wifiConfig.mqtt_username, custom_mqtt_username.getValue(), 40);
     strlcpy(wifiConfig.mqtt_password, custom_mqtt_password.getValue(), 40);
     strlcpy(wifiConfig.ota_username, custom_ota_username.getValue(), 40);
     strlcpy(wifiConfig.ota_password, custom_ota_password.getValue(), 40);
     strlcpy(wifiConfig.bluetti_device_id, custom_bluetti_device.getValue(), 40);
     eeprom_saveconfig();
  }

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    // The wifi indicator blinks while the connection is down; handleDisplay()
    // drives the blink phase, so just keep calling it.
    handleDisplay();
    delay(200);
    Serial.print(".");
  }
  
  WiFi.setAutoReconnect(true);
  // WiFi power save is left at the SDK default (WIFI_PS_MIN_MODEM). An explicit
  // setWiFiPowerSavingMode() used to sit here unreferenced; worth knowing that
  // WIFI_PS_NONE caused a kernel panic on the upstream author's hardware, so it
  // is not a knob to reach for when the link looks poor.

  Serial.println(F(""));
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
  wrDisp_IP(WiFi.localIP().toString());
  disp_setWifiSignal(1, WiFi.RSSI());
  if (MDNS.begin(DEVICE_NAME)) {
    Serial.println(F("MDNS responder started"));
  }

  //setup web server handling
  #if MSG_VIEWER_DETAILS
      msgViewerDetails = true;
      Serial.println(F("webserver BT/MQTT variable logging enabled..."));
    #else
      msgViewerDetails = false;
      Serial.println(F("webserver BT/MQTT variable logging disabled..."));
  #endif

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/html", index_html, processorWebsiteUpdates);
  });
  server.on("/switchLogging", HTTP_GET, [](AsyncWebServerRequest *request){
      msgViewerDetails = !msgViewerDetails;
      if(msgViewerDetails){
        Serial.println(F("webserver BT/MQTT variable logging enabled..."));
      }
      else{
        Serial.println(F("webserver BT/MQTT variable logging disabled..."));
      }
      request->send_P(200, "text/html", index_html, processorWebsiteUpdates);
  });
  server.on("/rebootDevice", [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "reboot in 2sec");
      delay(2000);
      ESP.restart();
  });
  server.on("/resetConfig", [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
      request->send(200, "text/plain", "reset Wifi and reboot in 2sec");
      delay(2000);
      initBWifi(true);
  });
  // Change the BLE pairing without wiping the WiFi/MQTT configuration.  // Previously the only way to change this was /resetConfig, which also erased
  // the WiFi credentials and every MQTT setting - painful, because a wrong id
  // otherwise leaves the device rebooting every MAX_DISCONNECTED_TIME_UNTIL_REBOOT
  // minutes with no way back in.
  server.on("/setBluettiID", HTTP_GET, [](AsyncWebServerRequest *request){
      if (!requireAuth(request)) return;
      if (!request->hasParam("value")) {
        request->send(400, "text/plain",
          "usage: /setBluettiID?value=<bluetooth name>\ncurrent: " +
          String(wifiConfig.bluetti_device_id) + "\n");
        return;
      }
      String value = request->getParam("value")->value();
      value.trim();
      if (!validBluettiID(value)) {
        request->send(400, "text/plain",
          "invalid id: 1-39 characters, no '/', '+', '#' or control characters\n");
        return;
      }
      if (value == String(wifiConfig.bluetti_device_id)) {
        request->send(200, "text/plain", "unchanged: " + value + "\n");
        return;
      }
      String previous = String(wifiConfig.bluetti_device_id);
      strlcpy(wifiConfig.bluetti_device_id, value.c_str(), sizeof(wifiConfig.bluetti_device_id));
      eeprom_saveconfig();
      Serial.println("[WIFI] Bluetti BT id changed: " + previous + " -> " + value);
      AddtoMsgView(String(millis()) + ": Bluetti BT id " + previous + " -> " + value + ", rebooting");
      request->send(200, "text/plain",
        "Bluetti BT id: " + previous + " -> " + value +
        "\nrebooting - the BLE scan filter is set up once in setup(), and Home"
        " Assistant will create a new device for the new id.\n");
      delay(2000);   // give the response a chance to leave the socket
      ESP.restart();
  });
  // Read-only diagnostic: the last raw page-0 BLE response as hex, so the register
  // map can be inspected without fighting interleaved serial output. Exposes the
  // same telemetry already published to MQTT, so it is left unauthenticated.
  server.on("/rawPage", HTTP_GET, [](AsyncWebServerRequest *request){
      uint8_t buf[RAW_FRAME_MAX];
      size_t len = 0;
      uint8_t offset = 0;
      if (!getRawPageFrame(0x00, buf, sizeof(buf), &len, &offset)) {
        request->send(503, "text/plain", "no page 0x00 frame captured yet\n");
        return;
      }
      String hex;
      hex.reserve(len * 5 + 64);
      for (size_t i = 0; i < len; i++) {
        if (i && i % 16 == 0) hex += '\n';
        char b[4];
        snprintf(b, sizeof(b), "%02x", buf[i]);
        hex += b;
        if (i % 2 == 1) hex += ' ';
      }
      request->send(200, "text/plain",
        "page=0x00 offset=0x" + String(offset, HEX) + " bytes=" + String(len) +
        "\nregister N sits at data byte 2*(0x" + String(offset, HEX) + "-offset);\n" + hex + "\n");
  });

  /* Lists the Bluetooth names heard by the most recent scan. Aimed at setting a
   * board up on site: the Bluetooth ID has to match the station's advertised name
   * exactly, and reading it off a list beats typing an 18-character string from a
   * phone. Read-only, so it needs no credentials. */
  server.on("/scanBT", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", btScanNamesReport());
  });

  //setup web server events
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello my friend, I'm just your data feed!", NULL, millis(), 10000);
  });
  server.addHandler(&events);

  // ota_username is a char[], so "if (!wifiConfig.ota_username)" was always
  // false: it took the credential branch even when both fields were empty.
  // ElegantOTA's async mode needs -DELEGANTOTA_USE_ASYNC_WEBSERVER=1, which is
  // set globally in platformio.ini.
  if (strlen(wifiConfig.ota_username) == 0) {
    ElegantOTA.begin(&server);
  } else {
    ElegantOTA.begin(&server, wifiConfig.ota_username, wifiConfig.ota_password);
  }

  server.begin();
  Serial.println(F("HTTP server started"));

}

void handleWebserver() {
  
  //Serial.println(F("DEBUG handleWebserver"));
  drainMsgView();
  if ((millis() - lastTimeWebUpdate) > MSG_VIEWER_REFRESH_CYCLE*1000) {
    
    // check wifi status every MSG_VIEWER_REFRESH_CYCLE and set display 
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi is disconnected, try to reconnect..."));
      disp_setWifiMode(0);
      disp_setStatus("Wifi err..");
      WiFi.disconnect();
      WiFi.reconnect();
      AddtoMsgView(String(millis()) + ": WLAN ERROR! try to reconnect");
      wifiReconnectCounter++;
      //delay(1000); no delay as we only check every 5 seconds. Removing 1 second blocking of the program in the loop.
    } else {
      disp_setWifiSignal(1,WiFi.RSSI());
      if (wifiReconnectCounter > 0)
      {
        //only update display ones after wifi is recovered.
        disp_setStatus("Running!");
        wifiReconnectCounter = 0;
      }
    }

    // update display
    disp_setBlueTooth(isBTconnected());
    disp_setMqttStatus(isMQTTconnected());

    // Send Events to the Web Server with current data
    events.send("ping",NULL,millis());
    events.send(String(millis()).c_str(),"runtime",millis());
    events.send(String(WiFi.RSSI()).c_str(),"rssi",millis());
    events.send(String(isMQTTconnected()).c_str(),"mqtt_connected",millis());
    events.send(String(getLastMQTTMessageTime()).c_str(),"mqtt_last_msg_time",millis());
    events.send(String(isBTconnected()).c_str(),"bt_connected",millis());
    events.send(String(getLastBTMessageTime()).c_str(),"bt_last_msg_time",millis());
    if(msgViewerDetails){
      events.send(lastMsg.c_str(),"last_msg",millis());
    } 
    
    lastTimeWebUpdate = millis();
  }
}


String processorWebsiteUpdates(const String& var){
  
  if(var == "IP"){
    return String(WiFi.localIP().toString());
  }
  else if(var == "RSSI"){
    return String(WiFi.RSSI());
  }
  else if(var == "SSID"){
    return String(WiFi.SSID());
  }
  else if(var == "MAC"){
    return String(WiFi.macAddress());
  }
  else if(var == "RUNTIME"){
    return String(millis());
  }
  else if(var == "MQTT_IP"){
    char msg[40];
    if (strlen(wifiConfig.mqtt_server) == 0){
      strlcpy(msg, "No MQTT server configured", 40);
    }else{
      strlcpy(msg, wifiConfig.mqtt_server, 40);
    }
    
    return msg;
  }
  else if(var == "MQTT_PORT"){
    char msg[6];
    strlcpy(msg, wifiConfig.mqtt_port, 6);
    return msg;
  }
  else if(var == "MQTT_CONNECTED"){
    return String(isMQTTconnected());
  }
  else if(var == "LAST_MQTT_MSG_TIME"){
    return String(getLastMQTTMessageTime());
  }
  else if(var == "DEVICE_ID"){
    char msg[40];
    strlcpy(msg, wifiConfig.bluetti_device_id, 40);
    return msg;
  }
  else if(var == "BT_CONNECTED"){
    return String(isBTconnected());
  }
  else if(var == "LAST_BT_MSG_TIME"){
    return String(getLastBTMessageTime());
  }
  else if(var == "BT_ERROR"){
    return String(getPublishErrorCount());
  }
  else if(var == "LAST_MSG"){
    if (msgViewerDetails){
      return String("...waiting for data...");
    }
    else{
      return String("...disabled...");
    }
  }
  else //return something, else this if then else will crash in case calles without VAR set....
  {
    return String("");
  }
}

void AddtoMsgView(String data){
  if (msgQueue == nullptr) return;          // created in initBWifi()

  MsgEntry entry;
  strlcpy(entry.text, data.c_str(), sizeof(entry.text));

  // Non-blocking: the BLE task must never wait, and a dropped viewer line is
  // cosmetic since the ring only keeps the last MSG_VIEWER_ENTRY_COUNT anyway.
  xQueueSend(msgQueue, &entry, 0);
}
