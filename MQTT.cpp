#include "BluettiConfig.h"
#include "MQTT.h"
#include "BWifi.h"
#include "BTooth.h"
#include "utils.h"
#include "display_tft.h"
#include "HADiscovery.h"
#include "config.h"

#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient mqttClient;  
PubSubClient client(mqttClient);
int publishErrorCount = 0;
unsigned long lastMQTTMessage = 0;
unsigned long previousDeviceStatePublish = 0;
unsigned long previousDeviceStateStatusPublish = 0;
unsigned long previousMqttReconnect = 0;

/* Set once the Home Assistant discovery configs have been published for this
 * connection; cleared on reconnect so they are re-sent. */
static bool haPublished = false;

String map_field_name(enum field_names f_name){
   switch(f_name) {
      case DC_OUTPUT_POWER:
        return "dc_output_power";
        break; 
      case AC_OUTPUT_POWER:
        return "ac_output_power";
        break; 
      case DC_OUTPUT_ON:
        return "dc_output_on";
        break; 
      case AC_OUTPUT_ON:
        return "ac_output_on";
        break; 
      case AC_OUTPUT_MODE:
        return "ac_output_mode";
        break; 
      case POWER_GENERATION:
        return "power_generation";
        break;       
      case TOTAL_BATTERY_PERCENT:
        return "total_battery_percent";
        break; 
      case DC_INPUT_POWER:
        return "dc_input_power";
        break;
      case AC_INPUT_POWER:
        return "ac_input_power";
        break;
      case AC_INPUT_VOLTAGE:
        return "ac_input_voltage";
        break; 
      case AC_INPUT_CURRENT:
        return "ac_input_current";
        break;
      case AC_INPUT_FREQUENCY:
        return "ac_input_frequency";
        break;
      case PACK_VOLTAGE:
        return "pack_voltage";
        break;
      case INTERNAL_PACK_VOLTAGE:
        return "internal_pack_voltage";
        break;
      case SERIAL_NUMBER:
        return "serial_number";
        break;
      case ARM_VERSION:
        return "arm_version";
        break;
      case DSP_VERSION:
        return "dsp_version";
        break;
      case DEVICE_TYPE:
        return "device_type";
        break;
      case UPS_MODE:
        return "ups_mode";
        break;
      case DISPLAY_TIMEOUT:
        return "display_timeout";
        break;
      case GRID_CHARGE_ON:
        return "grid_charge_on";
        break;
      case INTERNAL_AC_VOLTAGE:
        return "internal_ac_voltage";
        break;
      case INTERNAL_AC_FREQUENCY:
        return "internal_ac_frequency";
        break;
      case INTERNAL_CURRENT_ONE:
        return "internal_current_one";
        break;
      case INTERNAL_POWER_ONE:
        return "internal_power_one";
        break;
      case INTERNAL_CURRENT_TWO:
        return "internal_current_two";
        break;
      case INTERNAL_POWER_TWO:
        return "internal_power_two";
        break;
      case INTERNAL_CURRENT_THREE:
        return "internal_current_three";
        break;
      case INTERNAL_POWER_THREE:
        return "internal_power_three";
        break;
      case PACK_NUM_MAX:
        return "pack_max_num";
        break;
      case PACK_NUM:
        return "pack_num";
        break;
      case PACK_BATTERY_PERCENT:
        return "pack_battery_percent";
        break;
      case INTERNAL_DC_INPUT_VOLTAGE:
        return "internal_dc_input_voltage";
        break;
      case INTERNAL_DC_INPUT_POWER:
        return "internal_dc_input_power";
        break;
      case INTERNAL_DC_INPUT_CURRENT:
        return "internal_dc_input_current";
        break;
      case INTERNAL_CELL01_VOLTAGE:
        return "internal_cell01_voltage";    
        break;
      case INTERNAL_CELL02_VOLTAGE:
        return "internal_cell02_voltage";    
        break;
      case INTERNAL_CELL03_VOLTAGE:
        return "internal_cell03_voltage";    
        break;
      case INTERNAL_CELL04_VOLTAGE:
        return "internal_cell04_voltage";    
        break;
      case INTERNAL_CELL05_VOLTAGE:
        return "internal_cell05_voltage";    
        break;
      case INTERNAL_CELL06_VOLTAGE:
        return "internal_cell06_voltage";    
        break;
      case INTERNAL_CELL07_VOLTAGE:
        return "internal_cell07_voltage";    
        break;
      case INTERNAL_CELL08_VOLTAGE:
        return "internal_cell08_voltage";    
        break;
      case INTERNAL_CELL09_VOLTAGE:
        return "internal_cell09_voltage";    
        break;
      case INTERNAL_CELL10_VOLTAGE:
        return "internal_cell10_voltage";    
        break;
      case INTERNAL_CELL11_VOLTAGE:
        return "internal_cell11_voltage";    
        break;
      case INTERNAL_CELL12_VOLTAGE:
        return "internal_cell12_voltage";    
        break;
      case INTERNAL_CELL13_VOLTAGE:
        return "internal_cell13_voltage";    
        break;
      case INTERNAL_CELL14_VOLTAGE:
        return "internal_cell14_voltage";    
        break;
      case INTERNAL_CELL15_VOLTAGE:
        return "internal_cell15_voltage";    
        break;
      case INTERNAL_CELL16_VOLTAGE:
        return "internal_cell16_voltage";    
        break;     
      case LED_MODE:
        return "led_mode";
        break;
      case POWER_OFF:
        return "power_off";
        break;
      case ECO_ON:
        return "eco_on";
        break;
      case ECO_SHUTDOWN:
        return "eco_shutdown";
        break;
      case CHARGING_MODE:
        return "charging_mode";
        break;
      case POWER_LIFTING_ON:
        return "power_lifting_on";
        break;
      case AC_INPUT_POWER_MAX:
        return "ac_input_power_max";
        break;
      case AC_INPUT_CURRENT_MAX:
        return "ac_input_current_max";
        break;
      case AC_OUTPUT_POWER_MAX:
        return "ac_output_power_max";
        break;
      case AC_OUTPUT_CURRENT_MAX:
        return "ac_output_current_max";
        break;
      case BATTERY_MIN_PERCENTAGE:
        return "battery_min_percentage";
        break;
      case AC_CHARGE_MAX_PERCENTAGE:
        return "ac_charge_max_percentage";
        break;
      case AC_INPUT_CONNECTED:
        return "ac_input_connected";
        break;
      case DC_INPUT_CONNECTED:
        return "dc_input_connected";
        break;
      case DC_OUTPUT_VOLTAGE:
        return "dc_output_voltage";
        break;
      case DC_OUTPUT_CURRENT:
        return "dc_output_current";
        break;
      default:
        #ifdef DEBUG
          Serial.println(F("Info 'map_field_name' found unknown field!"));
        #endif
        return "unknown";
        break;
   }
  
}

//There is no reflection to do string to enum
//There are a couple of ways to work aroung it... but basically are just "case" statements
//Wapped them in a fuction
String map_command_value(String command_name, String value){
  String toRet = value;
  value.toUpperCase();
  command_name.toUpperCase(); //force case indipendence

  //on / off commands
  if(command_name == "POWER_OFF" || command_name == "AC_OUTPUT_ON" || command_name == "DC_OUTPUT_ON" || command_name == "ECO_ON" || command_name == "POWER_LIFTING_ON") {
    if (value == "ON") {
      toRet = "1";
    }
    if (value == "OFF") {
      toRet = "0";
    }
  }

  //See DEVICE_EB3A enums
  if(command_name == "LED_MODE"){
    if (value == "LED_LOW") {
      toRet = "1";
    }
    if (value == "LED_HIGH") {
      toRet = "2";
    }
    if (value == "LED_SOS") {
      toRet = "3";
    }
    if (value == "LED_OFF") {
      toRet = "4";
    }
  }

  //See DEVICE_EB3A enums
  if(command_name == "ECO_SHUTDOWN"){
    if (value == "ONE_HOUR") {
      toRet = "1";
    }
    if (value == "TWO_HOURS") {
      toRet = "2";
    }
    if (value == "THREE_HOURS") {
      toRet = "3";
    }
    if (value == "FOUR_HOURS") {
      toRet = "4";
    }
  }

  //See DEVICE_EB3A enums
  if(command_name == "CHARGING_MODE"){
    if (value == "STANDARD") {
      toRet = "0";
    }
    if (value == "SILENT") {
      toRet = "1";
    }
    if (value == "TURBO") {
      toRet = "2";
    }
  }


  /* The display timeout publishes these tokens as its state, and the labels in
   * PayloadParser.cpp are meant to be writable straight back. Without this case
   * the fallthrough reaches toInt(), where "MIN_5" is 0 - outside the 2..5
   * range the device accepts, which the headers warn confuses the HMI. */
  if(command_name == "DISPLAY_TIMEOUT"){
    if (value == "SEC_30") {
      toRet = "2";
    }
    if (value == "MIN_1") {
      toRet = "3";
    }
    if (value == "MIN_5") {
      toRet = "4";
    }
    if (value == "NEVER") {
      toRet = "5";
    }
  }

  return toRet;
}

// Callback function
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String topic_path = String(topic);
  topic_path.toLowerCase();//in case we recieve DC_OUTPUT_ON instead of the expected dc_output_on

  // Match the last topic segment exactly. The old code used
  // topic_path.indexOf(field_name), which also matched a field name appearing
  // anywhere else in the topic - including inside the Bluetti device id -
  // and silently kept the last match instead of the first.
  int slash = topic_path.lastIndexOf('/');
  String topic_command = (slash > -1) ? topic_path.substring(slash + 1) : topic_path;

  Serial.print("MQTT Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(" Payload: ");
  String strPayload = String((char * ) payload);
  Serial.println(strPayload);

  for (int i=0; i< sizeof(bluetti_device_command)/sizeof(device_field_data_t); i++){
      String current_name = map_field_name(bluetti_device_command[i].f_name);
      if (topic_command != current_name){
        continue;
      }

      // Declared here so there is no path that sends an uninitialised frame.
      // Previously `command` was declared outside the loop and sent even when
      // no command matched, with garbage page/offset.
      bt_command_t command;
      command.prefix = 0x01;
      command.field_update_cmd = 0x06;
      command.page = bluetti_device_command[i].f_page;
      command.offset = bluetti_device_command[i].f_offset;

      String switched = map_command_value(current_name, strPayload);
      Serial.print(" Payload - switched: ");
      Serial.println(switched);

      command.len = swap_bytes(switched.toInt());
      command.check_sum = modbus_crc((uint8_t*)&command,6);
      lastMQTTMessage = millis();

      sendBTCommand(command);
      return;
  }

  Serial.println(F("MQTT command topic matched no known command, ignored"));
  AddtoMsgView(String(millis()) + ": ignore unknown command topic " + topic_command);
}

void subscribeTopic(enum field_names field_name) {
#ifdef DEBUG
  Serial.println("[MQTT] subscribe to topic: " +  map_field_name(field_name));
#endif
  char subscribeTopicBuf[512];
  ESPBluettiSettings settings = get_esp32_bluetti_settings();

  sprintf(subscribeTopicBuf, "bluetti/%s/command/%s", settings.bluetti_device_id, map_field_name(field_name).c_str() );
  client.subscribe(subscribeTopicBuf);
  lastMQTTMessage = millis();

}

void publishTopic(enum field_names field_name, String value){
  char publishTopicBuf[1024];
  ESPBluettiSettings settings = get_esp32_bluetti_settings();
 
#ifdef DEBUG
  { SerialLock lock; Serial.println("[MQTT] publish topic for field: " +  map_field_name(field_name)); }
#endif
  
  //sometimes we get empty values / wrong vales - all the time device_type is empty
  if (map_field_name(field_name) == "device_type" && value.length() < 3){

    //Serial.println(F("[MQTT] Error while publishTopic! 'device_type' can't be empty, reboot device)"));
    ESP.restart();
    Serial.println(F("[MQTT] Error while publishTopic! 'device_type' can't be empty, restarting BlueTooth Stack)"));
   // btResetStack();
   
  } 
  
  // Remember identity fields so the HA discovery payloads can describe the real
  // device; they only arrive with the first poll cycle.
  haRecordIdentity(field_name, value);

  // Feed the on-board dashboard from the same funnel - no extra polling.
  disp_setField(field_name, value);

  sprintf(publishTopicBuf, "bluetti/%s/state/%s", settings.bluetti_device_id, map_field_name(field_name).c_str() ); 
  if (strlen(settings.mqtt_server) == 0){
    AddtoMsgView(String(millis()) +": " + map_field_name(field_name) + " -> " + value); 
    #ifdef DEBUG
      Serial.println("[MQTT] No MQTT server specified!");
    #endif
  }else{
    lastMQTTMessage = millis();
    if (!client.publish(publishTopicBuf, value.c_str() )){
      publishErrorCount++;
      #ifdef DEBUG
        { SerialLock lock; Serial.println("[MQTT] Publish error: " + String(lastMQTTMessage) + ": publish ERROR! " + map_field_name(field_name) + " -> " + value); }
      #endif
      AddtoMsgView(String(lastMQTTMessage) + ": publish ERROR! " + map_field_name(field_name) + " -> " + value);
    }
    else{
      #ifdef DEBUG
        { SerialLock lock; Serial.println("[MQTT] Last Message: " + String(lastMQTTMessage) + ": " + map_field_name(field_name) + " -> " + value); }
      #endif
      AddtoMsgView(String(lastMQTTMessage) + ": " + map_field_name(field_name) + " -> " + value);
    }
  }
  
 
}

void publishDeviceState(){
  char publishTopicBuf[1024];

  ESPBluettiSettings settings = get_esp32_bluetti_settings();
  sprintf(publishTopicBuf, "bluetti/%s/state/%s", settings.bluetti_device_id, "device" ); 
  String value = "{\"IP\":\"" + WiFi.localIP().toString() + "\", \"MAC\":\"" + WiFi.macAddress() + "\", \"Uptime\":" + millis() + "}";
  #ifdef DEBUG
    Serial.println("[MQTT] PublishingDeviceState: "+value);
  #endif
  if (!client.publish(publishTopicBuf, value.c_str() )){
    publishErrorCount++;
  }
  lastMQTTMessage = millis();
  previousDeviceStatePublish = millis();
 
}

void publishDeviceStateStatus(){
  char publishTopicBuf[1024];

  ESPBluettiSettings settings = get_esp32_bluetti_settings();
  sprintf(publishTopicBuf, "bluetti/%s/state/%s", settings.bluetti_device_id, "device_status" ); 
  String value = "{\"MQTTconnected\":" + String(isMQTTconnected()) + ", \"BTconnected\":" + String(isBTconnected()) + "}"; 
  #ifdef DEBUG
    Serial.println("[MQTT] PublishingDeviceStateStatus: "+value);
  #endif
  if (!client.publish(publishTopicBuf, value.c_str() )){
    publishErrorCount++;
  }
  lastMQTTMessage = millis();
  previousDeviceStateStatusPublish = millis();

  // Re-assert availability on every status cycle.
  //
  // After an unclean reboot (crash, power cut, OTA, flashing) the broker
  // publishes the PREVIOUS session's retained "offline" last will roughly 20s
  // later - i.e. AFTER this session has already published "online". The
  // retained value would then stay "offline" and Home Assistant would show
  // every entity as unavailable indefinitely, even though the bridge is up.
  // Re-publishing here bounds that staleness to one status cycle.
  char availTopic[128];
  snprintf(availTopic, sizeof(availTopic), "bluetti/%s/status", settings.bluetti_device_id);
  mqttPublish(availTopic, "online", true);
 
}

void initMQTT(){

    ESPBluettiSettings settings = get_esp32_bluetti_settings();
    Serial.println("[MQTT] init MQTT");
    if (strlen(settings.mqtt_server) == 0){
      Serial.println("[MQTT] No MQTT server configured");
      return;
    }
    Serial.print("[MQTT] Connecting to MQTT at: ");
    Serial.print(settings.mqtt_server);
    Serial.print(":");
    Serial.println(F(settings.mqtt_port));
    
    client.setServer(settings.mqtt_server, atoi(settings.mqtt_port));
    client.setCallback(callback);
    // Default buffer is 256 bytes. A Home Assistant discovery payload is several
    // times that, and PubSubClient reacts to an oversized publish by closing the
    // connection (_client->stop()) instead of returning an error - so this must
    // be set before connect().
    if (!client.setBufferSize(MQTT_BUFFER_SIZE)) {
      Serial.println(F("[MQTT] WARNING: could not grow the MQTT buffer"));
    }

    // Last will: Home Assistant marks every entity unavailable if the bridge
    // drops off without a clean disconnect.
    char availTopic[128];
    snprintf(availTopic, sizeof(availTopic), "bluetti/%s/status", settings.bluetti_device_id);

    bool connect_result;
    // Unique per device. Two bridges - or a stale session - sharing one client id
    // make the broker evict them alternately, which surfaces as intermittent
    // publish failures. MAC-derived, so it is stable across reboots.
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String clientId = "Bluetti_ESP32_" + mac;

    // Pass NULL rather than "" when unconfigured: PubSubClient sets the username
    // flag whenever the pointer is non-NULL, and brokers that allow anonymous
    // access reject a CONNECT carrying a blank username.
    const char *user = strlen(settings.mqtt_username) ? settings.mqtt_username : nullptr;
    const char *pass = strlen(settings.mqtt_password) ? settings.mqtt_password : nullptr;
    connect_result = client.connect(clientId.c_str(), user, pass, availTopic, 0, true, "offline");

    if (connect_result) {

      Serial.println(F("[MQTT] Connected to MQTT Server... "));
      mqttPublish(availTopic, "online", true);

      // subscribe to topics for commands
      for (int i=0; i< sizeof(bluetti_device_command)/sizeof(device_field_data_t); i++){
        subscribeTopic(bluetti_device_command[i].f_name);
      }

      publishDeviceState();
      publishDeviceStateStatus();
    } else {
      Serial.printf("[MQTT] connect failed (state=%d, client id %s)\n", client.state(), clientId.c_str());
    }

    
      
};

void handleMQTT(){
    ESPBluettiSettings settings = get_esp32_bluetti_settings();
    if (strlen(settings.mqtt_server) == 0){
      return;
    }
    if ((millis() - lastMQTTMessage) > (MAX_DISCONNECTED_TIME_UNTIL_REBOOT * 60000)){ 
      Serial.println(F("MQTT is disconnected over allowed limit, reboot device"));
      ESP.restart();
    }
      
    if ((millis() - previousDeviceStatePublish) > (DEVICE_STATE_UPDATE * 60000)){ 
      publishDeviceState();
    }
    if ((millis() - previousDeviceStateStatusPublish) > (DEVICE_STATE_STATUS_UPDATE * 60000)){ 
      publishDeviceStateStatus();
    }
    // Retry whenever the connection is down, not only after publish failures.
    // Gating this on publishErrorCount > 5 assumed publishes were happening, but
    // with the BLE link down there are none - so a dropped MQTT connection used to
    // sit disconnected until the 5 minute reboot timer fired, which is why
    // mqtt_connected could read 0 indefinitely.
    if (!isMQTTconnected()){
      if ((millis() - previousMqttReconnect) > 15000)
      {
        previousMqttReconnect = millis();
        Serial.println(F("[MQTT] lost connection, try to reconnect"));
        disp_setMqttStatus(false);
        client.disconnect();
        lastMQTTMessage=0;
        previousDeviceStatePublish=0;
        previousDeviceStateStatusPublish=0;
        publishErrorCount=0;
        haPublished=false;
        AddtoMsgView(String(millis()) + ": MQTT connection lost, try reconnect");
        initMQTT();
      }
    }
    
    // Home Assistant discovery: published once the device identity is known
    // (only filled in by the first poll cycle), then re-sent after every
    // reconnect. The configs are retained, so repeating them is harmless.
    if (!haPublished && isMQTTconnected() && haIdentityReady()){
      publishHAConfig();
      haPublished = true;
    }

    client.loop();
}

bool mqttPublish(const char *topic, const String &payload, bool retained) {
  return client.publish(topic, payload.c_str(), retained);
}

bool isMQTTconnected(){
    if (client.connected()){
      return true;
    }
    else
    {
      return false;
    }  
}

int getPublishErrorCount(){
    return publishErrorCount;
}
unsigned long getLastMQTTMessageTime(){
    return lastMQTTMessage;
}
