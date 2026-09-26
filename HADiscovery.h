#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include <Arduino.h>
#include "DeviceType.h"

/* Home Assistant MQTT discovery (see HADiscovery.cpp).
 *
 * Records the device identity fields as they are published, so the discovery
 * payloads can describe the real power station. They are only known after the
 * first poll cycle, hence the separate "ready" check.
 */
void haRecordIdentity(enum field_names field, const String &value);
bool haIdentityReady();

/* Publishes retained discovery configs for every field the configured power
 * station actually exposes. Idempotent, so it is safe to call after every
 * reconnect. */
void publishHAConfig();

#endif
