#include "eeprom_utils.h"
#include "config.h"
#include <EEPROM.h>

extern String phoneNumbers[];
extern String customerID;
extern int smsCounter;

void eepromWriteString(int addr, String data, int maxLen) {
    for (int i = 0; i < maxLen; i++) {
        char c = (i < data.length()) ? data[i] : 0;
        EEPROM.write(addr + i, c);
    }
}

String eepromReadString(int addr, int maxLen) {
    String result = "";
    for (int i = 0; i < maxLen; i++) {
        char c = EEPROM.read(addr + i);
        if (c == 0) break;
        result += c;
    }
    return result;
}

void loadConfigFromEEPROM() {
    smsCounter = EEPROM.read(EEPROM_COUNTER_ADDR);
    Serial.print(F("EEPROM SMS Counter initialized to: "));
    Serial.println(smsCounter);
    
    String numA = eepromReadString(EEPROM_NUMSETA_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
    String numB = eepromReadString(EEPROM_NUMSETB_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
    String numC = eepromReadString(EEPROM_NUMSETC_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
    String custID = eepromReadString(EEPROM_CUSTOMER_ID_ADDR, EEPROM_CUSTOMER_ID_MAX_LEN);

    if (numA.length() > 0) phoneNumbers[0] = numA;
    if (numB.length() > 0) phoneNumbers[1] = numB;
    if (numC.length() > 0) phoneNumbers[2] = numC;
    if (custID.length() > 0) customerID = custID;

    Serial.print(F("Phone Numbers: "));
    Serial.print(phoneNumbers[0]); Serial.print(", ");
    Serial.print(phoneNumbers[1]); Serial.print(", ");
    Serial.println(phoneNumbers[2]);
    Serial.print(F("Customer ID: ")); Serial.println(customerID);
}
