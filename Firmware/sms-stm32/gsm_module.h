#ifndef GSM_MODULE_H
#define GSM_MODULE_H

#include <Arduino.h>

extern HardwareSerial sim800l;
extern String simResponseBuffer;
extern bool gsmIsInitializedAndReady;

bool readSimResponseNonBlocking(unsigned long timeout);
bool sendGsmCommandNonBlocking(String command, unsigned long delayTime);
String getGsmTimestamp();
bool initializeAndConfigureGsmModule();
void readSimResponse();

#endif
