#ifndef EEPROM_UTILS_H
#define EEPROM_UTILS_H

#include <Arduino.h>

void eepromWriteString(int addr, String data, int maxLen);
String eepromReadString(int addr, int maxLen);
void loadConfigFromEEPROM();

#endif
