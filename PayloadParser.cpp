#include "BluettiConfig.h"
#include "MQTT.h"
#include "PayloadParser.h"
#include "BWifi.h"


uint16_t parse_uint_field(uint8_t data[]) {
  return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

bool parse_bool_field(uint8_t data[]) {
  return (data[1]) == 1;
}

float parse_decimal_field(uint8_t data[], uint8_t scale) {
  uint16_t raw_value = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
  return (raw_value) / pow(10, scale);
}

float parse_version_field(uint8_t data[]) {

  uint16_t low = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
  uint16_t high = ((uint16_t)data[2] << 8) | (uint16_t)data[3];
  // uint32_t throughout: uint16_t promotes to a signed int, so `high << 16`
  // is signed overflow (undefined) for any high value >= 0x8000.
  uint32_t val = ((uint32_t)low) | ((uint32_t)high << 16);

  return (float)val / 100;
}

uint64_t parse_serial_field(uint8_t data[]) {

  uint16_t val1 = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
  uint16_t val2 = ((uint16_t)data[2] << 8) | (uint16_t)data[3];
  uint16_t val3 = ((uint16_t)data[4] << 8) | (uint16_t)data[5];
  uint16_t val4 = ((uint16_t)data[6] << 8) | (uint16_t)data[7];

  uint64_t sn = ((((uint64_t)val1) | ((uint64_t)val2 << 16)) | ((uint64_t)val3 << 32)) | ((uint64_t)val4 << 48);

  return sn;
}

String parse_string_field(uint8_t data[]) {
  // Relies on the caller NUL-terminating the field, which
  // parse_bluetooth_data() now does.
  return String((char*)data);
}

/* Labels for enum-valued fields. The values come from the enums documented in
 * the DEVICE_*.h headers; these are declared as {value, label} pairs rather than
 * a plain array so a gap in the range (DISPLAY_TIMEOUT starts at 2, UPS_MODE at
 * 1) needs no fake filler entries.
 *
 * The tokens deliberately match what map_command_value() accepts, so a value read
 * from here can be written straight back to the corresponding command topic. */
typedef struct {
  int value;
  const char *label;
} enum_label_t;

static const enum_label_t output_mode_labels[] = {
  {0, "STOP"}, {1, "INVERTER"}, {2, "BYPASS_C"}, {3, "BYPASS_D"}, {4, "LOAD_MATCHING"}
};

static const enum_label_t ups_mode_labels[] = {
  {1, "CUSTOMIZED"}, {2, "PV_PRIORITY"}, {3, "STANDARD"}, {4, "TIME_CONTROL"}
};

static const enum_label_t display_timeout_labels[] = {
  {2, "SEC_30"}, {3, "MIN_1"}, {4, "MIN_5"}, {5, "NEVER"}
};

static const enum_label_t led_mode_labels[] = {
  {1, "LED_LOW"}, {2, "LED_HIGH"}, {3, "LED_SOS"}, {4, "LED_OFF"}
};

static const enum_label_t eco_shutdown_labels[] = {
  {1, "ONE_HOUR"}, {2, "TWO_HOURS"}, {3, "THREE_HOURS"}, {4, "FOUR_HOURS"}
};

static const enum_label_t charging_mode_labels[] = {
  {0, "STANDARD"}, {1, "SILENT"}, {2, "TURBO"}
};

#define ENUM_LABEL_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const char *lookup_enum_label(const enum_label_t *table, size_t count, int value) {
  for (size_t i = 0; i < count; i++) {
    if (table[i].value == value) {
      return table[i].label;
    }
  }
  return nullptr;
}

/* The {value, label} table for one enum id, shared by the state parser and the
 * Home Assistant select options so the two cannot drift apart. */
static bool enumTableFor(uint8_t enum_id, const enum_label_t **table, size_t *count) {
  switch (enum_id) {
    case ENUM_OUTPUT_MODE:
      *table = output_mode_labels;     *count = ENUM_LABEL_COUNT(output_mode_labels);     return true;
    case ENUM_UPS_MODE:
      *table = ups_mode_labels;        *count = ENUM_LABEL_COUNT(ups_mode_labels);        return true;
    case ENUM_DISPLAY_TIMEOUT:
      *table = display_timeout_labels; *count = ENUM_LABEL_COUNT(display_timeout_labels); return true;
    case ENUM_LED_MODE:
      *table = led_mode_labels;        *count = ENUM_LABEL_COUNT(led_mode_labels);        return true;
    case ENUM_ECO_SHUTDOWN:
      *table = eco_shutdown_labels;    *count = ENUM_LABEL_COUNT(eco_shutdown_labels);    return true;
    case ENUM_CHARGING_MODE:
      *table = charging_mode_labels;   *count = ENUM_LABEL_COUNT(charging_mode_labels);   return true;
    default:
      return false;
  }
}

/* An enum's labels as a JSON array fragment, for a Home Assistant "select"
 * options list. Empty when the id has no table, so the caller can fall back to
 * a number entity instead. */
String enum_label_options(uint8_t enum_id) {
  const enum_label_t *table = nullptr;
  size_t count = 0;
  if (!enumTableFor(enum_id, &table, &count)) return String();

  String options = "[";
  for (size_t i = 0; i < count; i++) {
    if (i) options += ",";
    options += "\"" + String(table[i].label) + "\"";
  }
  return options + "]";
}

String parse_enum_field(uint8_t data[], uint8_t enum_id) {
  int value = (int)parse_uint_field(data);

  const enum_label_t *table = nullptr;
  size_t count = 0;
  enumTableFor(enum_id, &table, &count);

  const char *label = (table != nullptr) ? lookup_enum_label(table, count, value) : nullptr;

  // Fall back to the raw number for an unmapped id or value. The previous stub
  // returned "", which published an empty payload and left the entity blank.
  if (label == nullptr) {
    return String(value);
  }
  return String(label);
}

void parse_bluetooth_data(uint8_t page, uint8_t offset, uint8_t* pData, size_t length){
  char Byte_In_Hex_offset[3];
  char Byte_In_Hex_page[3];
  sprintf(Byte_In_Hex_offset, "%x", offset);
  sprintf(Byte_In_Hex_page, "%x", page);
  
    switch(pData[1]){
      // range request

    case 0x03:

      for (int i = 0; i < sizeof(bluetti_device_state) / sizeof(device_field_data_t); i++) {


            // filter fields not in range, reworked by https://github.com/AlexBurghardt
            // the original code didn't work completely and skipped some fields to be published
            if(
              // it's the correct page
              bluetti_device_state[i].f_page == page && 
              // data offset greater than or equal to page offset
              bluetti_device_state[i].f_offset >= offset &&
              // local offset does not exceed the page length, likely not needed because of the last condition check
              ((2* ((int)bluetti_device_state[i].f_offset - (int)offset)) + HEADER_SIZE) <= length &&
              // local offset + data size do not exceed the page length
              ((2* ((int)bluetti_device_state[i].f_offset - (int)offset + bluetti_device_state[i].f_size)) + HEADER_SIZE) <= length
            ){
    
                uint8_t data_start = (2* ((int)bluetti_device_state[i].f_offset - (int)offset)) + HEADER_SIZE;
                int data_len = 2 * bluetti_device_state[i].f_size;

                // +1 for the terminator. The old loop ran `i <= data_end`,
                // which wrote one byte PAST this (VLA) buffer on every field and
                // read one byte past the field. STRING_FIELD also needs a NUL
                // or String() runs off the end of the buffer.
                uint8_t data_payload_field[data_len + 1];

                for (int j = 0; j < data_len; j++){
                      data_payload_field[j] = pData[data_start - 1 + j];
                }
                data_payload_field[data_len] = 0;

                switch (bluetti_device_state[i].f_type){
                 
                  case UINT_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, String(parse_uint_field(data_payload_field)));
                    break;
    
                  case BOOL_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, String((int)parse_bool_field(data_payload_field)));
                    break;
    
                  case DECIMAL_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, String(parse_decimal_field(data_payload_field, bluetti_device_state[i].f_scale ), 2) );
                    break;
    
                  case SN_FIELD:  
                    char sn[16];
                    sprintf(sn, "%lld", parse_serial_field(data_payload_field));
                    publishTopic(bluetti_device_state[i].f_name, String(sn));
                    break;
    
                  case VERSION_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, String(parse_version_field(data_payload_field),2) );    
                    break;

                  case STRING_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, parse_string_field(data_payload_field));
                    break;
                  case ENUM_FIELD:
                    publishTopic(bluetti_device_state[i].f_name, parse_enum_field(data_payload_field, bluetti_device_state[i].f_enum));
                    break;
                  default:
                    break;
                  
                }
                
            }
            else{
              /* causes way too many messages, for debugging only
              //AddtoMsgView(String(millis()) + ": skip filtered field: "+ Byte_In_Hex_page + " offset: " + Byte_In_Hex_offset);
              */
            }
        }
        
        break; 
      case 0x06:
        AddtoMsgView(String(millis()) + ":skip 0x06 request! page: " + Byte_In_Hex_page + " offset: " + Byte_In_Hex_offset);
        break;
      default:
        AddtoMsgView(String(millis()) + ":skip unknow request! page: " + Byte_In_Hex_page + " offset: " + Byte_In_Hex_offset);
        break;

    }
    
}
