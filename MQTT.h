#ifndef MQTT_H
#define MQTT_H
#include "Arduino.h"
#include "DeviceType.h"

extern void publishTopic(enum field_names field_name, String value);
extern void publishDeviceState();
extern void publishDeviceStateStatus();
extern void handleMQTT();
extern void initMQTT();
extern bool isMQTTconnected();
extern int getPublishErrorCount();
unsigned long getLastMQTTMessageTime();

/* Shared helpers. publishHAConfig() itself is declared in HADiscovery.h. */
String map_field_name(enum field_names f_name);

/* Publish on the shared client. Needed by HADiscovery.cpp for the retained
 * discovery configs; `retained` matters because Home Assistant only reads
 * discovery topics it receives while subscribed. */
bool mqttPublish(const char *topic, const String &payload, bool retained);

#endif
