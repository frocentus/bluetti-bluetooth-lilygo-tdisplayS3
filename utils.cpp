#include "utils.h"
#include "crc16.h"

uint16_t swap_bytes(uint16_t number) {
    return (number << 8) | (number >> 8);
}

uint16_t modbus_crc(uint8_t buf[], int len){
      unsigned int crc = 0xFFFF;
      for (unsigned int i = 0; i < len; i++)
       {
        crc = crc16_update(crc, buf[i]);
       }
     
     return crc;
}

static SemaphoreHandle_t serialMutex = nullptr;

void serialLockInit() {
  if (serialMutex == nullptr) {
    serialMutex = xSemaphoreCreateMutex();
  }
}

SerialLock::SerialLock() {
  if (serialMutex != nullptr) {
    xSemaphoreTake(serialMutex, portMAX_DELAY);
  }
}

SerialLock::~SerialLock() {
  if (serialMutex != nullptr) {
    xSemaphoreGive(serialMutex);
  }
}
