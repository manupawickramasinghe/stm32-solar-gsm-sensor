#include "sensor_manager.h"
#include "config.h"
#include "sms_handler.h"
#include <EEPROM.h>

// Sensor objects
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

// Sensor data variables
float dhtHumSum = 0, dhtTempSum = 0, ds18b20TempSum = 0;
int readingCount = 0;
unsigned long lastSendTime = 0;

// External variables
extern SystemState currentState;
extern unsigned long lastStateChange;
extern unsigned long lastMainLoopTime;
extern int smsCounter;
extern String phoneNumbers[];
extern String customerID;

void initializeSensors() {
  Serial.print(F("Initializing DHT Sensor (Type: DHT11, Pin: PB1)\n"));
  dht.begin();
  Serial.println(F("DHT sensor dht.begin() called."));

  Serial.print(F("Initializing DS18B20 Sensor (Pin: PB0)\n"));
  ds18b20Sensors.begin();
  Serial.println(F("DS18B20 sensor ds18b20Sensors.begin() called."));
}

void handleSensorStateMachine() {
  unsigned long currentMillis = millis();
  float h, t, tempC_ds18b20;

  switch (currentState) {
    case STATE_IDLE:
      if (currentMillis - lastMainLoopTime >= MAIN_LOOP_CYCLE_DELAY) {
        currentState = STATE_WAIT_DHT;
        lastStateChange = currentMillis;
        Serial.println(F("Starting new reading cycle..."));
      }
      break;

    case STATE_WAIT_DHT:
      if (currentMillis - lastStateChange >= DHT_READ_PRE_DELAY) {
        currentState = STATE_READ_DHT;
        Serial.println(F("--- Reading DHT Sensor ---"));
      }
      break;

    case STATE_READ_DHT:
      h = dht.readHumidity();
      t = dht.readTemperature();
      if (!isnan(h) && !isnan(t)) {
        Serial.print(F("DHT - H:")); Serial.print(h); 
        Serial.print(F("% T:")); Serial.print(t); Serial.println(F("C"));
        dhtHumSum += h;
        dhtTempSum += t;
      }
      currentState = STATE_WAIT_DS18B20;
      lastStateChange = currentMillis;
      break;

    case STATE_WAIT_DS18B20:
      if (currentMillis - lastStateChange >= DS18B20_READ_PRE_DELAY) {
        currentState = STATE_READ_DS18B20;
        Serial.println(F("--- Reading DS18B20 Sensor ---"));
      }
      break;

    case STATE_READ_DS18B20:
      ds18b20Sensors.requestTemperatures();
      tempC_ds18b20 = ds18b20Sensors.getTempCByIndex(0);
      if (tempC_ds18b20 != DEVICE_DISCONNECTED_C && tempC_ds18b20 != 85.0) {
        Serial.print(F("DS18B20 - T:")); 
        Serial.print(tempC_ds18b20); Serial.println(F("C"));
        ds18b20TempSum += tempC_ds18b20;
      }
      currentState = STATE_PROCESS_DATA;
      break;

    case STATE_PROCESS_DATA:
      readingCount++;
      smsCounter++;
      EEPROM.write(EEPROM_COUNTER_ADDR, smsCounter);
      Serial.print(F("SMS Counter: ")); Serial.println(smsCounter);

      if (smsCounter >= SMS_SEND_THRESHOLD && readingCount > 0) {
        float avgHum = dhtHumSum / readingCount;
        float avgDhtTemp = dhtTempSum / readingCount;
        float avgDs18b20Temp = ds18b20TempSum / readingCount;
        
        String sensorDataSms = "60min Avg - DHT H:" + String(avgHum, 1) + 
                             "% T:" + String(avgDhtTemp, 1) + "C; " +
                             "DS18B20 T:" + String(avgDs18b20Temp, 1) + "C";
        
        Serial.println(F("--- Sending SMS with Average Sensor Data ---"));
        for (int i = 0; i < NUM_PHONE_NUMBERS; i++) {
          sendSMS(sensorDataSms, phoneNumbers[i]);
        }
        
        // Reset averages and counter
        dhtHumSum = 0;
        dhtTempSum = 0;
        ds18b20TempSum = 0;
        readingCount = 0;
        smsCounter = 0;
        EEPROM.write(EEPROM_COUNTER_ADDR, smsCounter);
        lastSendTime = currentMillis;
      }

      Serial.println(F("------------------------------------"));
      Serial.println(F("Reading cycle complete. Waiting for next cycle..."));
      lastMainLoopTime = currentMillis;
      currentState = STATE_IDLE;
      break;
  }
}
