#include <Arduino.h>
#include <WiFi.h>

#include "HADiscovery.h"
#include "BluettiConfig.h"
#include "MQTT.h"
#include "PayloadParser.h"
#include "BWifi.h"
#include "config.h"

/* ---------------------------------------------------------------------------
 * Home Assistant MQTT discovery.
 *
 * Publishes one retained config payload per entity to
 *   <MQTT_DISCOVERY_PREFIX>/<component>/<node>/<object>/config
 * so Home Assistant creates every entity by itself. The existing
 *   bluetti/<id>/state/<field>     -> state_topic
 *   bluetti/<id>/command/<field>   -> command_topic
 * topics are reused unchanged, so nothing about the current publishing
 * behaviour moves and the raw topics stay usable for other consumers.
 *
 * Which entities exist is decided by the power station's own field tables
 * (bluetti_device_state / bluetti_device_command in DEVICE_*.h), so this file
 * carries no per-device knowledge:
 *   state[] only          -> sensor / binary_sensor
 *   state[] and command[] -> switch or number
 *   command[] only        -> switch (optimistic) or button
 * ------------------------------------------------------------------------- */

/* ---- identity, recorded from published state ----------------------------
 * THREADING: haRecordIdentity() is called from publishTopic(), which runs in the
 * NimBLE host task, while haIdentityReady() and publishHAConfig() are called from
 * the Arduino loop task. Sharing String objects across those two tasks races on
 * the heap and the refcount, so the identity lives in fixed char buffers behind a
 * critical section and every reader copies out under the same lock.
 * ------------------------------------------------------------------------ */
struct ha_identity_t {
  char devType[24];
  char serial[24];
  char arm[16];
  char dsp[16];
};
static ha_identity_t g_id = {};
static portMUX_TYPE g_idMux = portMUX_INITIALIZER_UNLOCKED;

static ha_identity_t haIdentitySnapshot() {
  ha_identity_t snap;
  portENTER_CRITICAL(&g_idMux);
  snap = g_id;
  portEXIT_CRITICAL(&g_idMux);
  return snap;
}

void haRecordIdentity(enum field_names field, const String &value) {
  portENTER_CRITICAL(&g_idMux);
  switch (field) {
    case DEVICE_TYPE:   strlcpy(g_id.devType, value.c_str(), sizeof(g_id.devType)); break;
    case SERIAL_NUMBER: strlcpy(g_id.serial,  value.c_str(), sizeof(g_id.serial));  break;
    case ARM_VERSION:   strlcpy(g_id.arm,     value.c_str(), sizeof(g_id.arm));     break;
    case DSP_VERSION:   strlcpy(g_id.dsp,     value.c_str(), sizeof(g_id.dsp));     break;
    default: break;
  }
  portEXIT_CRITICAL(&g_idMux);
}

bool haIdentityReady() {
  // device_type alone is enough for a usable device block. Waiting for all four
  // would stall discovery forever if a device never reports one of them.
  return haIdentitySnapshot().devType[0] != '\0';
}

/* ---- per-field presentation metadata ------------------------------------
 * Unit / device_class / icon live here because they cannot be derived. The
 * component (sensor, switch, ...) is derived from the device tables instead,
 * so this table never has to know what a given device supports.
 * A field missing from this table still becomes a plain unitless sensor.
 * ----------------------------------------------------------------------- */
struct ha_meta_t {
  enum field_names f_name;
  const char *unit;
  const char *device_class;
  const char *state_class;
  const char *icon;
  bool diag;            /* entity_category: diagnostic */
  int min, max, step;   /* applied to "number" entities only */
};

static const ha_meta_t ha_meta[] = {
  /* --- switches and buttons: no unit, just an icon --------------------- */
  {AC_OUTPUT_ON,             "",   "",          "",            "mdi:power-socket",        false, 0, 0, 0},
  {DC_OUTPUT_ON,             "",   "",          "",            "mdi:power-socket",        false, 0, 0, 0},  {ECO_ON,                   "",   "",          "",            "mdi:leaf",                false, 0, 0, 0},
  {POWER_LIFTING_ON,         "",   "",          "",            "mdi:arrow-up-bold",       false, 0, 0, 0},
  {GRID_CHARGE_ON,           "",   "",          "",            "mdi:transmission-tower",  false, 0, 0, 0},
  {POWER_OFF,                "",   "",          "",            "mdi:power-off",           false, 0, 0, 0},

  /* --- input-port presence: bools in state[] only, so HADiscovery derives
     binary_sensor for them automatically --- */
  {AC_INPUT_CONNECTED,       "",   "",          "",            "mdi:power-plug",          false, 0, 0, 0},
  {DC_INPUT_CONNECTED,       "",   "",          "",            "mdi:solar-panel",         false, 0, 0, 0},

  /* --- power, watts ---------------------------------------------------- */
  {DC_OUTPUT_POWER,          "W",  "power",     "measurement", "mdi:flash",               false, 0, 0, 0},
  {DC_INPUT_POWER,           "W",  "power",     "measurement", "mdi:solar-panel",         false, 0, 0, 0},
  {AC_OUTPUT_POWER,          "W",  "power",     "measurement", "mdi:flash",               false, 0, 0, 0},
  {AC_INPUT_POWER,           "W",  "power",     "measurement", "mdi:transmission-tower",  false, 0, 0, 0},
  {INTERNAL_POWER_ONE,       "W",  "power",     "measurement", "mdi:flash",               true,  0, 0, 0},
  {INTERNAL_POWER_TWO,       "W",  "power",     "measurement", "mdi:flash",               true,  0, 0, 0},
  {INTERNAL_POWER_THREE,     "W",  "power",     "measurement", "mdi:flash",               true,  0, 0, 0},
  {INTERNAL_DC_INPUT_POWER,  "W",  "power",     "measurement", "mdi:solar-panel",         true,  0, 0, 0},
  {AC_INPUT_POWER_MAX,       "W",  "power",     "",            "mdi:transmission-tower",  true,  0, 0, 0},
  {AC_OUTPUT_POWER_MAX,      "W",  "power",     "",            "mdi:flash",               true,  0, 0, 0},

  /* --- volts ------------------------------------------------------------ */
  {AC_INPUT_VOLTAGE,         "V",  "voltage",   "measurement", "mdi:sine-wave",           false, 0, 0, 0},
  {AC_INPUT_CURRENT,         "A",  "current",   "measurement", "mdi:current-ac",          false, 0, 0, 0},
  {INTERNAL_AC_VOLTAGE,      "V",  "voltage",   "measurement", "mdi:sine-wave",           true,  0, 0, 0},
  {DC_OUTPUT_VOLTAGE,        "V",  "voltage",   "measurement", "mdi:flash",               false, 0, 0, 0},
  {PACK_VOLTAGE,             "V",  "voltage",   "measurement", "mdi:battery",             false, 0, 0, 0},
  {INTERNAL_PACK_VOLTAGE,    "V",  "voltage",   "measurement", "mdi:battery",             false, 0, 0, 0},
  {INTERNAL_DC_INPUT_VOLTAGE,"V",  "voltage",   "measurement", "mdi:solar-panel",         false, 0, 0, 0},

  /* --- amps ------------------------------------------------------------- */
  {INTERNAL_CURRENT_ONE,     "A",  "current",   "measurement", "mdi:current-ac",          false, 0, 0, 0},
  {INTERNAL_CURRENT_TWO,     "A",  "current",   "measurement", "mdi:current-ac",          true,  0, 0, 0},
  {INTERNAL_CURRENT_THREE,   "A",  "current",   "measurement", "mdi:current-ac",          true,  0, 0, 0},
  {INTERNAL_DC_INPUT_CURRENT,"A",  "current",   "measurement", "mdi:current-dc",          true,  0, 0, 0},
  {DC_OUTPUT_CURRENT,        "A",  "current",   "measurement", "mdi:current-dc",          false, 0, 0, 0},
  {AC_INPUT_CURRENT_MAX,     "A",  "current",   "",            "mdi:current-ac",          true,  0, 0, 0},
  {AC_OUTPUT_CURRENT_MAX,    "A",  "current",   "",            "mdi:current-ac",          true,  0, 0, 0},

  /* --- hertz ------------------------------------------------------------ */
  {AC_INPUT_FREQUENCY,       "Hz", "frequency", "measurement", "mdi:sine-wave",           false, 0, 0, 0},
  {INTERNAL_AC_FREQUENCY,    "Hz", "frequency", "measurement", "mdi:sine-wave",           true,  0, 0, 0},

  /* --- battery ---------------------------------------------------------- */
  {TOTAL_BATTERY_PERCENT,    "%",  "battery",   "measurement", "mdi:battery",             false, 0, 0, 0},
  {PACK_BATTERY_PERCENT,     "%",  "battery",   "measurement", "mdi:battery",             false, 0, 0, 0},
  {BATTERY_MIN_PERCENTAGE,   "%",  "battery",   "",            "mdi:battery-minus",       false, 0, 100, 1},
  {AC_CHARGE_MAX_PERCENTAGE, "%",  "battery",   "",            "mdi:battery-plus",        false, 0, 100, 1},

  /* --- writable numbers. Ranges come from the enums documented in
     DEVICE_*.h; the AC200M/EB3A notes there warn that out-of-range values
     confuse the HMI, so they are worth being explicit about. */
  /* 0xBF5 is the panel's own display timeout, not a power-saving sleep: values
     2..5 mean 30 s / 1 min / 5 min / never. bluetti-bt-lib's DisplayMode enum
     pairs the same register (3061) with the same four values on the AC200M,
     EP500P, AC500 and EP500 - and has it commented out for the AC300, which is
     consistent with that device table being unverified. */
  {DISPLAY_TIMEOUT,          "",   "",          "",            "mdi:monitor-off",         false, 2, 5, 1},
  {UPS_MODE,                 "",   "",          "",            "mdi:power-settings",      false, 1, 4, 1},
  /* device dependent: PACK_NUM_MAX reports the real ceiling for this unit */
  {PACK_NUM,                 "",   "",          "",            "mdi:battery-plus",        false, 1, 8, 1},
  {PACK_NUM_MAX,             "",   "",          "",            "mdi:battery-plus",        true,  0, 0, 0},

  /* PV generation. The unit comes from Patrick762/bluetti-bt-lib's FieldUnit
     table (FieldName.POWER_GENERATION: "kWh") - the only source that states one,
     which is why this project had left the field unitless and out of the energy
     dashboard rather than guess. Whether the station's counter is lifetime,
     daily or per-session is not established: it reads 0.00 with the station
     switched off, so it is probably not lifetime. total_increasing is right in
     all three readings - a drop counts as a new cycle and the cycles sum to the
     cumulative energy - so the class is claimed here while the register map
     keeps publishing the raw value. */
  {POWER_GENERATION,         "kWh", "energy",  "total_increasing", "mdi:chart-line",     false, 0, 0, 0},

  /* --- text / enum ------------------------------------------------------ */
  /* These two are the AC *output* group on AC200M, so they are user-facing
     rather than diagnostic: the mode says battery-vs-bypass, and the current is
     the load's draw. */
  {AC_OUTPUT_MODE,           "",   "",          "",            "mdi:swap-horizontal",     false, 0, 0, 0},
  {LED_MODE,                 "",   "",          "",            "mdi:led-on",              false, 0, 0, 0},
  {ECO_SHUTDOWN,             "",   "",          "",            "mdi:timer-off",           false, 0, 0, 0},
  {CHARGING_MODE,            "",   "",          "",            "mdi:battery-charging",    false, 0, 0, 0},

  /* --- identity: static for a whole run, so HA hides it under "Diagnostic" */
  {DEVICE_TYPE,              "",   "",          "",            "mdi:information-outline", true,  0, 0, 0},
  {SERIAL_NUMBER,            "",   "",          "",            "mdi:identifier",          true,  0, 0, 0},
  {ARM_VERSION,              "",   "",          "",            "mdi:chip",                true,  0, 0, 0},
  {DSP_VERSION,              "",   "",          "",            "mdi:chip",                true,  0, 0, 0},
};

static const ha_meta_t g_noMeta  = {FIELD_UNDEFINED, "", "", "", "", false, 0, 0, 0};
static const ha_meta_t g_cellMeta = {FIELD_UNDEFINED, "V", "voltage", "measurement", "mdi:battery-outline", false, 0, 0, 0};

/* 16 identically shaped cell fields: handled as a range, not 16 table rows */
static bool isCellVoltage(enum field_names f) {
  return f >= INTERNAL_CELL01_VOLTAGE && f <= INTERNAL_CELL16_VOLTAGE;
}

static const ha_meta_t *metaFor(enum field_names f) {
  if (isCellVoltage(f)) return &g_cellMeta;
  for (size_t i = 0; i < sizeof(ha_meta) / sizeof(ha_meta[0]); i++) {
    if (ha_meta[i].f_name == f) return &ha_meta[i];
  }
  return &g_noMeta;
}

/* ---- helpers ------------------------------------------------------------ */

#define STATE_COUNT (sizeof(bluetti_device_state) / sizeof(device_field_data_t))
#define CMD_COUNT   (sizeof(bluetti_device_command) / sizeof(device_field_data_t))

static const device_field_data_t *findField(const device_field_data_t *arr, size_t count, enum field_names f) {
  for (size_t i = 0; i < count; i++) {
    if (arr[i].f_name == f) return &arr[i];
  }
  return nullptr;
}

/* HA object ids may only contain [a-z0-9_]. The Bluetti BLE name is usually
 * clean already, but it comes from the user, so normalise it. */
static String slug(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c)) {
      out += (char)tolower(c);
    } else if (out.length() && out[out.length() - 1] != '_') {
      out += '_';
    }
  }
  return out;
}

static String jsonEscape(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

static String capitalise(const String &token) {
  // keep established acronyms fully upper case, otherwise "Ac"/"Dc"
  String up = token;
  up.toUpperCase();
  if (up == "AC" || up == "DC" || up == "SN" || up == "UPS" || up == "ARM" ||
      up == "DSP" || up == "LED" || up == "ECO" || up == "ID" || up == "MQTT") {
    return up;
  }
  if (!token.length()) return token;
  String out = token;
  out.setCharAt(0, toupper(out[0]));
  return out;
}

static String prettyName(enum field_names f) {
  String s = map_field_name(f);
  s.replace("internal_cell", "cell");   // "cell01_voltage"

  String out;
  int start = 0;
  while (start < (int)s.length()) {
    int us = s.indexOf('_', start);
    String token = (us < 0) ? s.substring(start) : s.substring(start, us);
    start = (us < 0) ? s.length() : us + 1;

    // split a trailing digit run so cell01 becomes "Cell 01"
    int d = token.length();
    while (d > 0 && isdigit((unsigned char)token[d - 1])) d--;
    String word = token.substring(0, d);
    String num  = token.substring(d);

    if (out.length()) out += ' ';
    out += capitalise(word);
    if (num.length()) {
      out += ' ';
      out += num;
    }
  }
  return out;
}

/* ---- publishing --------------------------------------------------------- */

/* scratch for one discovery pass, so the helpers keep short signatures */
static String g_prefix, g_node, g_uidBase, g_stateBase, g_cmdBase, g_avail, g_deviceJson;
static int g_published = 0, g_skipped = 0, g_failed = 0;

static void addCommon(String &json, const ha_meta_t *m, const String &objectId, const String &name) {
  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + g_uidBase + "_" + objectId + "\",";
  json += "\"availability_topic\":\"" + g_avail + "\",";
  json += "\"payload_available\":\"online\",";
  json += "\"payload_not_available\":\"offline\",";
  if (m->unit[0])         json += "\"unit_of_measurement\":\"" + String(m->unit) + "\",";
  if (m->device_class[0]) json += "\"device_class\":\"" + String(m->device_class) + "\",";
  if (m->state_class[0])  json += "\"state_class\":\"" + String(m->state_class) + "\",";
  if (m->icon[0])         json += "\"icon\":\"" + String(m->icon) + "\",";
  if (m->diag)            json += "\"entity_category\":\"diagnostic\",";
}

static void publishEntity(enum field_names f, const device_field_data_t *st,
                          const device_field_data_t *cm) {
  const ha_meta_t *m = metaFor(f);
  String objectId = map_field_name(f);
  String json = "{";
  String component;
  String stateExtra;    // state-side on/off mapping (switch only)
  String cmdExtra;      // command-side payloads

  if (cm && f == POWER_OFF) {
    // a momentary action, not a state - a switch would misrepresent it
    component = "button";
    cmdExtra = "\"payload_press\":\"ON\",";
  } else if (cm && cm->f_type == BOOL_FIELD) {
    component = "switch";
    cmdExtra = "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",";
    if (!st) {
      // command only: nothing reports the real state back, so let HA assume it
      cmdExtra += "\"optimistic\":true,";
    }
  } else if (cm && (cm->f_type == UINT_FIELD || cm->f_type == DECIMAL_FIELD)) {
    component = "number";
    if (m->max > m->min) {
      cmdExtra = "\"min\":" + String(m->min) + ",\"max\":" + String(m->max) +
                 ",\"step\":" + String(m->step ? m->step : 1) + ",";
    }
  } else if (cm && cm->f_type == ENUM_FIELD && st && cm->f_enum != ENUM_NONE) {
    // A writable enum that also reports its state: "select" is the component that
    // both shows the label and can set it. The options come from the same table
    // parse_enum_field() reads, so what is displayed is what can be chosen, and
    // map_command_value() accepts those same labels on the command topic.
    // The ENUM_NONE guard matters: the EB3A/EP500P enum commands are typed
    // ENUM_FIELD with no enum id, so a device that later reports one of those
    // back would otherwise get an empty options list - rejected by HA as invalid.
    component = "select";
    cmdExtra = "\"options\":" + enum_label_options((uint8_t)cm->f_enum) + ",";
  } else if (cm && cm->f_type == ENUM_FIELD) {
    // No readable state, so the select would sit permanently "unknown" - skipped
    // (LED_MODE, ECO_SHUTDOWN, CHARGING_MODE on EB3A / EP500P). Control still
    // works through the raw command topic meanwhile.
    g_skipped++;
    #ifdef DEBUG
      Serial.println("[HA] skipped enum command with no readable state: " + objectId);
    #endif
    return;
  } else if (st && st->f_type == BOOL_FIELD) {
    // binary_sensor takes payload_on/payload_off; state_on/state_off is a
    // switch/fan concept and is rejected here
    component = "binary_sensor";
    cmdExtra = "\"payload_on\":\"1\",\"payload_off\":\"0\",";
  } else {
    component = "sensor";
  }

  if (component == "switch" && st) {
    // the device publishes "1"/"0" on the state topic
    stateExtra = "\"state_on\":\"1\",\"state_off\":\"0\",";
  }

  json += cmdExtra;
  if (st) json += stateExtra + "\"state_topic\":\"" + g_stateBase + objectId + "\",";
  if (cm) json += "\"command_topic\":\"" + g_cmdBase + objectId + "\",";
  addCommon(json, m, objectId, prettyName(f));
  json += "\"device\":" + g_deviceJson;
  json += "}";

  char topic[192];
  snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config",
           g_prefix.c_str(), component.c_str(), g_node.c_str(), objectId.c_str());

  if (mqttPublish(topic, json, true)) {
    g_published++;
  } else {
    g_failed++;
    Serial.println("[HA] FAILED discovery publish for " + objectId + " (" + String(json.length()) + " bytes)");
  }
}

void publishHAConfig() {
  ESPBluettiSettings s = get_esp32_bluetti_settings();
  String devId = String(s.bluetti_device_id);
  // Copy the identity out once, under the lock, so nothing below reads state the
  // BLE task could be rewriting.
  ha_identity_t id = haIdentitySnapshot();

  g_prefix    = MQTT_DISCOVERY_PREFIX;
  g_node      = slug(devId);
  g_uidBase   = "bluetti_" + g_node;
  g_stateBase = "bluetti/" + devId + "/state/";
  g_cmdBase   = "bluetti/" + devId + "/command/";
  g_avail     = "bluetti/" + devId + "/status";
  g_published = g_skipped = g_failed = 0;

  String swVersion = "ARM " + (id.arm[0] ? String(id.arm) : String("?")) +
                     " / DSP " + (id.dsp[0] ? String(id.dsp) : String("?"));
  g_deviceJson = "{";
  g_deviceJson += "\"identifiers\":[\"" + g_uidBase + "\"],";
  g_deviceJson += "\"name\":\"" + jsonEscape("Bluetti " + devId) + "\",";
  g_deviceJson += "\"manufacturer\":\"BLUETTI\",";
  g_deviceJson += "\"model\":\"" + jsonEscape(String(id.devType)) + "\",";
  if (id.serial[0]) g_deviceJson += "\"serial_number\":\"" + jsonEscape(String(id.serial)) + "\",";
  g_deviceJson += "\"sw_version\":\"" + jsonEscape(swVersion) + "\",";
  g_deviceJson += "\"configuration_url\":\"http://" + WiFi.localIP().toString() + "/\"";
  g_deviceJson += "}";

  // Walk the device's own tables: they define what actually exists.
  for (size_t i = 0; i < STATE_COUNT; i++) {
    enum field_names f = bluetti_device_state[i].f_name;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (bluetti_device_state[j].f_name == f) { duplicate = true; break; }
    }
    if (duplicate) continue;
    publishEntity(f,
                  findField(bluetti_device_state, STATE_COUNT, f),
                  findField(bluetti_device_command, CMD_COUNT, f));
  }
  for (size_t i = 0; i < CMD_COUNT; i++) {
    enum field_names f = bluetti_device_command[i].f_name;
    if (findField(bluetti_device_state, STATE_COUNT, f)) continue;   // already done
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (bluetti_device_command[j].f_name == f) { duplicate = true; break; }
    }
    if (duplicate) continue;
    publishEntity(f, nullptr, &bluetti_device_command[i]);
  }

  Serial.printf("[HA] discovery: %d entities for %s (%d skipped, %d failed)\n",
                g_published, id.devType, g_skipped, g_failed);
}
