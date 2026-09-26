#ifndef DEVICE_AC200M_H
#define DEVICE_AC200M_H
#include "Arduino.h"


enum auto_sleep_mode {
  THIRTY_SECONDS = 2,
  ONE_MINNUTE = 3,
  FIVE_MINUTES = 4,
  NEVER = 5  
};


// { FIELD_NAME, PAGE, OFFSET, SIZE, SCALE (if scale is needed e.g. decimal value, defaults to 0) , ENUM (if data is enum, defaults to 0) , FIELD_TYPE }
static device_field_data_t bluetti_device_state[] = {


  /*Page 0x00 Core */
  {DEVICE_TYPE,               0x00, 0x0A, 7, 0, 0, STRING_FIELD},
  {SERIAL_NUMBER,             0x00, 0x11, 4, 0 ,0, SN_FIELD},
  {ARM_VERSION,               0x00, 0x17, 2, 0, 0, VERSION_FIELD},
  {DSP_VERSION,               0x00, 0x19, 2, 0, 0, VERSION_FIELD},
  {DC_INPUT_POWER,            0x00, 0x24, 1, 0, 0, UINT_FIELD},
  {AC_INPUT_POWER,            0x00, 0x25, 1, 0, 0, UINT_FIELD},
  {AC_OUTPUT_POWER,           0x00, 0x26, 1, 0, 0, UINT_FIELD},
  {DC_OUTPUT_POWER,           0x00, 0x27, 1, 0, 0, UINT_FIELD},
  {POWER_GENERATION,          0x00, 0x29, 1, 1, 0, DECIMAL_FIELD},
  {TOTAL_BATTERY_PERCENT,     0x00, 0x2B, 1, 0, 0, UINT_FIELD},

  /* Input-port presence flags.
   * Measured across three states - AC port only, DC-in port only, and both:
   *   0x2C/0x2E = 1 only while the DC-in is connected
   *   0x2D/0x2F = 1 only while the AC input is connected
   * The second register in each pair tracked the first identically in every
   * capture, so only one of each is exposed; 0x2E/0x2F are left unread because
   * three samples cannot establish how, if at all, they differ. */
  {AC_INPUT_CONNECTED,        0x00, 0x2D, 1, 0, 0, BOOL_FIELD},
  {DC_INPUT_CONNECTED,        0x00, 0x2C, 1, 0, 0, BOOL_FIELD},
  {AC_OUTPUT_ON,              0x00, 0x30, 1, 0, 0, BOOL_FIELD},
  {DC_OUTPUT_ON,              0x00, 0x31, 1, 0, 0, BOOL_FIELD},

  /* AC output group, contiguous from 0x46.
   * 0x46 is the OUTPUT MODE, not a status flag: 0=STOP, 1=Inverter,
   * 2=Bypass C, 3=Bypass D, 4=Load Matching - i.e. it says whether the load is
   * running off the battery or being passed through from the AC input.
   * 0x48 is the output current. Cross-checked against
   * github.com/ebangerter/bluetti (AC200MAX), which names the same registers
   * ac_output_mode / internal_current_one with these scales. (0x49 there is
   * internal_power_one, a duplicate of ac_output_power at 0x26, so it is left
   * unread.) */
  {AC_OUTPUT_MODE,            0x00, 0x46, 1, 0, ENUM_OUTPUT_MODE, ENUM_FIELD},
  {INTERNAL_AC_VOLTAGE,       0x00, 0x47, 1, 0, 0, DECIMAL_FIELD},
  {INTERNAL_CURRENT_ONE,      0x00, 0x48, 1, 1, 0, DECIMAL_FIELD},
  // Single register holding whole tenths of a hertz: 0x4A reads 500 while
  // inverting (50.00 Hz) and 0x4B reads 0 with the AC output on or off, so it
  // is not part of this field. Size 2 was harmless - the 0x4B half is always
  // zero - but it claims a register that is not this field's.
  {INTERNAL_AC_FREQUENCY,     0x00, 0x4A, 1, 1, 0, DECIMAL_FIELD},

  {AC_INPUT_VOLTAGE,          0x00, 0x4D, 1, 1, 0, DECIMAL_FIELD},
  // AC input current, in centiamps: register 0x4E read 861 (8.61 A) while the AC
  // input carried 471 W at 55.5 V during the input tests, and 0x4C/0x4E tracked
  // together. Only the voltage and power of this port were mapped upstream.
  {AC_INPUT_CURRENT,          0x00, 0x4E, 1, 2, 0, DECIMAL_FIELD},

  // DC output voltage/current. Not in upstream's table at all, though the
  // station reports them. Scales verified against the unit's own display:
  // 0x53 read 135 with the screen showing 13.5V, and with 0x54 = 175 the
  // product 13.5 x 17.5 = 236W matches dc_output_power (0x27 = 235) and the
  // measured load. Note the V x A == W identity alone cannot pin these: 135V
  // x 1.75A gives the same product, so the display reading is the evidence.
  {DC_OUTPUT_VOLTAGE,        0x00, 0x53, 1, 1, 0, DECIMAL_FIELD},
  {DC_OUTPUT_CURRENT,        0x00, 0x54, 1, 1, 0, DECIMAL_FIELD},

  // Scale 0, not upstream's 1. Register 0x56 holds whole volts.
  // Measured on the DC input with the same class of supply (58.8V/8A) as the
  // AC input: 0x4D reads 555 at scale 1 = 55.5V, while 0x56 read 56 at scale 1
  // = 5.6V. Equivalent supplies cannot differ 10x, so 0x56 is 56V.
  // Note V x A == W holds at EITHER scaling (the product is scale invariant),
  // so the evidence is this cross-comparison and the supply nameplate, not the
  // power equality. It is also what settles the scale against bluetti-bt-lib,
  // which reads this register at scale 1 and would report 5.5V for the 55V
  // measured here.
  {INTERNAL_DC_INPUT_VOLTAGE, 0x00, 0x56, 1, 0, 0, DECIMAL_FIELD},
  // 0x58 is the DC input current in centiamps, checked against the two other
  // registers describing the same circuit instead of a nameplate:
  // 0x57/10 / 0x56 == 0x58/100 held to two decimals in every capture taken -
  // 475.1W / 55V = 8.64A vs 859 -> 8.59A, and 471.6W / 55V = 8.574A vs
  // 857 -> 8.57A; 0x59 is zero. bluetti-bt-lib agrees: its
  // DecimalField(INTERNAL_DC_INPUT_CURRENT, 88, 2) is scale 2 on one register.
  // Scale 1 would give 84.2A.
  // Upstream lists this register at scale 1 for AC300/EP500P, but that is
  // unverified for those units and has been left untouched here.
  {INTERNAL_DC_INPUT_CURRENT, 0x00, 0x58, 1, 2, 0, DECIMAL_FIELD},
  

  //Page 0x00 Battery Details
  //constant value, number off possible battery packs, 3 on AC200M, one internal and two external
  {PACK_NUM_MAX,              0x00, 0x5B, 1, 0, 0, UINT_FIELD },
  

  //Page 0x00 Battery Data 
  {INTERNAL_PACK_VOLTAGE,     0x00, 0x5C, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL01_VOLTAGE,   0x00, 0x69, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL02_VOLTAGE,   0x00, 0x6A, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL03_VOLTAGE,   0x00, 0x6B, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL04_VOLTAGE,   0x00, 0x6C, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL05_VOLTAGE,   0x00, 0x6D, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL06_VOLTAGE,   0x00, 0x6E, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL07_VOLTAGE,   0x00, 0x6F, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL08_VOLTAGE,   0x00, 0x70, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL09_VOLTAGE,   0x00, 0x71, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL10_VOLTAGE,   0x00, 0x72, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL11_VOLTAGE,   0x00, 0x73, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL12_VOLTAGE,   0x00, 0x74, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL13_VOLTAGE,   0x00, 0x75, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL14_VOLTAGE,   0x00, 0x76, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL15_VOLTAGE,   0x00, 0x77, 1, 2 ,0, DECIMAL_FIELD},
  {INTERNAL_CELL16_VOLTAGE,   0x00, 0x78, 1, 2 ,0, DECIMAL_FIELD},

  //Page 0x0B Controls 
  // Time after the display switches off -> READ
  {AUTO_SLEEP_MODE,           0x0B, 0xF5, 1, 0, 0, UINT_FIELD},

};

// parameters that can be set via mqtt.
// Hint: In the case topics not appearing automatically on the mqtt server they need to be created manually.
// This can be done with MqttExplorer for instance
static device_field_data_t bluetti_device_command[] = {
  /*Page 0x0B Core */
  {DC_OUTPUT_ON,              0x0B, 0xC0, 1, 0, 0, BOOL_FIELD},
  {AC_OUTPUT_ON,              0x0B, 0xBF, 1, 0, 0, BOOL_FIELD},

  // Time after the display switches off -> WRITE
  // Caution: there is no check on the device, if the value is within the list of alowed values.
  // for allowed values see <enum auto_sleep_mode> above, use of other values seems to confuse the HMI.
  // The possibility to set this parameter on the HMI (Diplay) disappears.
  // But by writing an allowed value it turns back to normality.
  // I guess this is true for all "enum type" settings
  {AUTO_SLEEP_MODE,           0x0B, 0xF5, 1, 0, 0, UINT_FIELD},
  {POWER_OFF,                 0x0B, 0xF4, 1, 0, 0, BOOL_FIELD},
  
};

static device_field_data_t bluetti_polling_command[] = {
  // Status
  // changed to only one page 0 request (a portion of 7F bytes)
  {FIELD_UNDEFINED,           0x00, 0x0A, 0x7F, 0, 0, TYPE_UNDEFINED},
  // Settings  
  {FIELD_UNDEFINED,           0x0B, 0xB9, 0x3F, 0, 0, TYPE_UNDEFINED}
};

#endif
