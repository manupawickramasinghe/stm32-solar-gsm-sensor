#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>

extern DHT dht;
extern OneWire oneWire;
extern DallasTemperature ds18b20Sensors;

extern float dhtHumSum, dhtTempSum, ds18b20TempSum;
extern int readingCount;
extern unsigned long lastSendTime;

void initializeSensors();
void handleSensorStateMachine();

#endif
