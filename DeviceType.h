#ifndef __DEVICE_TYPE_H__
#define __DEVICE_TYPE_H__
#include "Arduino.h"

enum field_types{
   UINT_FIELD,
   BOOL_FIELD,
   ENUM_FIELD,
   STRING_FIELD,
   DECIMAL_ARRAY_FIELD,
   DECIMAL_FIELD,
   VERSION_FIELD,
   SN_FIELD, 
   TYPE_UNDEFINED
};

/* Which label set a field needs when its type is ENUM_FIELD. Device tables pass
 * one of these as the f_enum member; the actual strings live in
 * PayloadParser.cpp, which is also where an unknown value falls back to the raw
 * number. Keep this list and that one in sync. */
enum field_enum_ids {
  ENUM_NONE = 0,
  ENUM_OUTPUT_MODE,       /* 0 STOP, 1 INVERTER, 2 BYPASS_C, 3 BYPASS_D, 4 LOAD_MATCHING */
  ENUM_UPS_MODE,          /* 1 CUSTOMIZED, 2 PV_PRIORITY, 3 STANDARD, 4 TIME_CONTROL */
  ENUM_AUTO_SLEEP_MODE,   /* 2 THIRTY_SECONDS .. 5 NEVER */
  ENUM_LED_MODE,          /* 1 LED_LOW .. 4 LED_OFF */
  ENUM_ECO_SHUTDOWN,      /* 1 ONE_HOUR .. 4 FOUR_HOURS */
  ENUM_CHARGING_MODE      /* 0 STANDARD, 1 SILENT, 2 TURBO */
};

enum field_names {
  DC_OUTPUT_POWER,
  DC_OUTPUT_ON,
  AC_OUTPUT_POWER,
  AC_OUTPUT_ON,
  AC_OUTPUT_MODE,
  POWER_GENERATION,
  TOTAL_BATTERY_PERCENT, 
  DC_INPUT_POWER,
  AC_INPUT_POWER,
  AC_INPUT_VOLTAGE,
  AC_INPUT_FREQUENCY,
  PACK_VOLTAGE,
  INTERNAL_PACK_VOLTAGE,
  SERIAL_NUMBER,
  ARM_VERSION,
  DSP_VERSION,
  DEVICE_TYPE, 
  UPS_MODE,
  AUTO_SLEEP_MODE,
  GRID_CHARGE_ON,
  FIELD_UNDEFINED,
  INTERNAL_AC_VOLTAGE,
  INTERNAL_AC_FREQUENCY,
  INTERNAL_CURRENT_ONE,
  INTERNAL_POWER_ONE,
  INTERNAL_CURRENT_TWO,
  INTERNAL_POWER_TWO,
  INTERNAL_CURRENT_THREE,
  INTERNAL_POWER_THREE,
  INTERNAL_DC_INPUT_VOLTAGE,
  INTERNAL_DC_INPUT_POWER,
  INTERNAL_DC_INPUT_CURRENT,
  PACK_BATTERY_PERCENT,
  PACK_NUM,
  PACK_NUM_MAX,
  INTERNAL_CELL01_VOLTAGE,
  INTERNAL_CELL02_VOLTAGE,
  INTERNAL_CELL03_VOLTAGE,
  INTERNAL_CELL04_VOLTAGE,
  INTERNAL_CELL05_VOLTAGE,
  INTERNAL_CELL06_VOLTAGE,
  INTERNAL_CELL07_VOLTAGE,
  INTERNAL_CELL08_VOLTAGE,
  INTERNAL_CELL09_VOLTAGE,
  INTERNAL_CELL10_VOLTAGE,
  INTERNAL_CELL11_VOLTAGE,
  INTERNAL_CELL12_VOLTAGE,
  INTERNAL_CELL13_VOLTAGE,
  INTERNAL_CELL14_VOLTAGE,
  INTERNAL_CELL15_VOLTAGE,
  INTERNAL_CELL16_VOLTAGE,
  LED_MODE,
  POWER_OFF,
  ECO_ON,
  ECO_SHUTDOWN,
  CHARGING_MODE,
  POWER_LIFTING_ON,
  AC_INPUT_POWER_MAX,
  AC_INPUT_CURRENT_MAX,
  AC_OUTPUT_POWER_MAX,
  AC_OUTPUT_CURRENT_MAX,
  BATTERY_MIN_PERCENTAGE,   // Discharge lower limit
  AC_CHARGE_MAX_PERCENTAGE, // Percentage to which point battery will be charged with AC

  /* Input-port presence flags. These MUST stay after INTERNAL_CELL16_VOLTAGE:
   * HADiscovery.cpp and display_tft.cpp both test
   *   f >= INTERNAL_CELL01_VOLTAGE && f <= INTERNAL_CELL16_VOLTAGE
   * to recognise cell voltages, so any value inserted inside that range would be
   * misread as a cell. Verified behaviour on AC200M: see Device_AC200M.h. */
  AC_INPUT_CONNECTED,
  DC_INPUT_CONNECTED,

  /* Output-side measurements read off the raw page; see Device_AC200M.h for how
   * the scales were established. */
  DC_OUTPUT_VOLTAGE,
  DC_OUTPUT_CURRENT,
  AC_INPUT_CURRENT

};

typedef struct device_field_data {
  enum field_names f_name;
  uint8_t f_page;
  uint8_t f_offset;
  int8_t f_size;
  int8_t f_scale;
  int8_t f_enum;
  enum field_types f_type;
} device_field_data_t;

#endif