#include <SoftwareSerial.h>
#include <LowPower.h>
#include <EEPROM.h>

SoftwareSerial sim800(9, 8); 

#define SOIL_PIN A0
#define SENSOR_POWER_PIN 10 
#define GSM_POWER_PIN 11 
#define SERIAL_TIMEOUT 5000 
#define MAX_RESPONSE_LENGTH 128 
#define MAX_GPRS_RETRIES 3 
#define MAX_AT_RETRIES 3 


#define EEPROM_ADDR_YEAR 0 
#define EEPROM_ADDR_MONTH 2 
#define EEPROM_ADDR_DAY 3 
#define EEPROM_ADDR_HOUR 4 
#define EEPROM_ADDR_MINUTE 5 
#define EEPROM_ADDR_SECOND 6 


String phone = "+94719751003"; 
String setupPhone = "+94768378406"; 
String baseURL = "https://script.google.com/macros/s/AKfycbwQpQfYRcNlkMecg-6csBLtCLEL6GCkW5Fu0j3Pr5IHcxAKuyTIQyXy7n3HroZb38Cs/exec";
String lastDateStr = "00/00/0000";
String lastTimeStr = "00:00:00";
float lastSoilPercent = 0.0;
bool internetConnected = false;
struct Timestamp {
  int year;
  byte month;
  byte day;
  byte hour;
  byte minute;
  byte second;
} lastActivation;

void setup() {
 
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  pinMode(GSM_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW); 
  digitalWrite(GSM_POWER_PIN, LOW); 

  Serial.begin(9600);
  delay(1000); 
  if (Serial) Serial.println("Booting...");

  readLastActivation();

  if (shouldPerformCycle()) {
  
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(100);
    int soilRaw = analogRead(SOIL_PIN);
    lastSoilPercent = map(soilRaw, 1023, 0, 0, 100); 
    digitalWrite(SENSOR_POWER_PIN, LOW);
    if (Serial) {
      Serial.print("Soil Moisture (%): ");
      Serial.println(lastSoilPercent);
    }

    powerCycleGSM();

    bool sim800OK = false;
    for (int i = 0; i < MAX_AT_RETRIES; i++) {
      if (sendCommand("AT", "OK")) {
        sim800OK = true;
        if (Serial) Serial.println("SIM800L is running");
        break;
      }
      if (Serial) Serial.println("AT attempt " + String(i + 1) + "/" + String(MAX_AT_RETRIES) + " failed");
      delay(1000);
    }

    if (sim800OK) {
    
      String message = "Date: " + lastDateStr + ", Time: " + lastTimeStr +
                      ", Soil Moisture (%): " + String(lastSoilPercent, 1) +
                      ", Internet: " + (internetConnected ? "Connected" : "Failed");
      if (sendSMS(message, setupPhone)) {
        if (Serial) Serial.println("Setup SMS sent successfully to " + setupPhone);
      } else {
        if (Serial) Serial.println("Setup SMS failed");
      }

      internetConnected = connectToGPRS();
      if (internetConnected) {
        String url = baseURL + "?sts=write" +
                     "&srs=ok" +
                     "&temp=" + String(lastSoilPercent, 1) +
                     "&humd=" + "0" +
                     "&swtc1=" + lastDateStr +
                     "&swtc2=" + lastTimeStr;
        if (sendGET(url)) {
          if (Serial) Serial.println("Google Sheet updated successfully in setup");
        } else {
          if (Serial) Serial.println("Google Sheet update failed in setup");
        }
      } else {
        if (Serial) Serial.println("Skipping Google Sheet update in setup (no internet)");
      }
    }

    sim800.end();
    digitalWrite(GSM_POWER_PIN, LOW);

    saveLastActivation();
  }

  enterSleepMode();
}

void loop() {
  // Not used; sleep mode handles wakeups
}

bool shouldPerformCycle() {
 
  powerCycleGSM();
  
  bool sim800OK = false;
  for (int i = 0; i < MAX_AT_RETRIES; i++) {
    if (sendCommand("AT", "OK")) {
      sim800OK = true;
      break;
    }
    if (Serial) Serial.println("SIM800L AT attempt " + String(i + 1) + "/" + String(MAX_AT_RETRIES));
    delay(1000);
  }

  if (!sim800OK) {
    if (Serial) Serial.println("SIM800L not responding");
    sim800.end();
    digitalWrite(GSM_POWER_PIN, LOW);
    return false; 
  }

  String dateTime = getNetworkTime();
  sim800.end();
  digitalWrite(GSM_POWER_PIN, LOW);

  if (dateTime == "") {
    if (Serial) Serial.println("Failed to get network time");
    return false;
  }

  lastDateStr = parseDate(dateTime);
  lastTimeStr = parseTime(dateTime);
  int currentHour = lastTimeStr.substring(0, 2).toInt();
  int currentMinute = lastTimeStr.substring(3, 5).toInt();

  if (currentMinute <= 5) {
 
    if (lastActivation.year == 0 || 
        lastActivation.hour != currentHour ||
        lastActivation.day != lastDateStr.substring(0, 2).toInt() ||
        lastActivation.month != lastDateStr.substring(3, 5).toInt() ||
        lastActivation.year != lastDateStr.substring(6, 10).toInt()) {
      return true;
    }
  }

  return false;
}

void performCycle() {
 
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(100);
  int soilRaw = analogRead(SOIL_PIN);
  lastSoilPercent = map(soilRaw, 1023, 0, 0, 100); 
  digitalWrite(SENSOR_POWER_PIN, LOW);
  if (Serial) {
    Serial.print("Soil Moisture (%): ");
    Serial.println(lastSoilPercent);
  }

  powerCycleGSM();

  bool sim800OK = false;
  for (int i = 0; i < MAX_AT_RETRIES; i++) {
    if (sendCommand("AT", "OK")) {
      sim800OK = true;
      if (Serial) Serial.println("SIM800L is running");
      break;
    }
    if (Serial) Serial.println("AT attempt " + String(i + 1) + "/" + String(MAX_AT_RETRIES) + " failed");
    delay(1000);
  }

  if (!sim800OK) {
    if (Serial) Serial.println("SIM800L not responding");
  } else {
  
    updateNetworkTime();

    internetConnected = connectToGPRS();

    String message = "Date: " + lastDateStr + ", Time: " + lastTimeStr +
                    ", Soil Moisture (%): " + String(lastSoilPercent, 1) +
                    ", Internet: " + (internetConnected ? "Connected" : "Failed");
    if (sendSMS(message, phone)) {
      if (Serial) Serial.println("SMS sent successfully to " + phone);
    } else {
      if (Serial) Serial.println("SMS failed");
    }

    if (internetConnected) {
      String url = baseURL + "?sts=write" +
                   "&srs=ok" +
                   "&temp=" + String(lastSoilPercent, 1) +
                   "&humd=" + "0" +
                   "&swtc1=" + lastDateStr +
                   "&swtc2=" + lastTimeStr;
      if (sendGET(url)) {
        if (Serial) Serial.println("Google Sheet updated successfully");
      } else {
        if (Serial) Serial.println("Google Sheet update failed");
      }
    } else {
      if (Serial) Serial.println("Skipping Google Sheet update (no internet)");
    }
  }

  sim800.end();
  digitalWrite(GSM_POWER_PIN, LOW);
}

void saveLastActivation() {
  lastActivation.year = lastDateStr.substring(6, 10).toInt();
  lastActivation.month = lastDateStr.substring(3, 5).toInt();
  lastActivation.day = lastDateStr.substring(0, 2).toInt();
  lastActivation.hour = lastTimeStr.substring(0, 2).toInt();
  lastActivation.minute = lastTimeStr.substring(3, 5).toInt();
  lastActivation.second = lastTimeStr.substring(6, 8).toInt();

  EEPROM.put(EEPROM_ADDR_YEAR, lastActivation.year);
  EEPROM.put(EEPROM_ADDR_MONTH, lastActivation.month);
  EEPROM.put(EEPROM_ADDR_DAY, lastActivation.day);
  EEPROM.put(EEPROM_ADDR_HOUR, lastActivation.hour);
  EEPROM.put(EEPROM_ADDR_MINUTE, lastActivation.minute);
  EEPROM.put(EEPROM_ADDR_SECOND, lastActivation.second);
}

void readLastActivation() {
  EEPROM.get(EEPROM_ADDR_YEAR, lastActivation.year);
  EEPROM.get(EEPROM_ADDR_MONTH, lastActivation.month);
  EEPROM.get(EEPROM_ADDR_DAY, lastActivation.day);
  EEPROM.get(EEPROM_ADDR_HOUR, lastActivation.hour);
  EEPROM.get(EEPROM_ADDR_MINUTE, lastActivation.minute);
  EEPROM.get(EEPROM_ADDR_SECOND, lastActivation.second);

  if (lastActivation.year < 2020 || lastActivation.year > 2100) {
    lastActivation.year = 0; 
  }
}

void enterSleepMode() {

  pinMode(SENSOR_POWER_PIN, OUTPUT);
  pinMode(GSM_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  digitalWrite(GSM_POWER_PIN, LOW);

  powerCycleGSM();

  bool sim800OK = false;
  for (int i = 0; i < MAX_AT_RETRIES; i++) {
    if (sendCommand("AT", "OK")) {
      sim800OK = true;
      break;
    }
    if (Serial) Serial.println("SIM800L AT attempt " + String(i + 1) + "/" + String(MAX_AT_RETRIES));
    delay(1000);
  }

  long secondsToNext = 3600; 
  if (sim800OK) {
    String dateTime = getNetworkTime();
    if (dateTime != "") {
      lastDateStr = parseDate(dateTime);
      lastTimeStr = parseTime(dateTime);
      secondsToNext = calculateSecondsToNext();
    }
  }

  sim800.end();
  digitalWrite(GSM_POWER_PIN, LOW);

  if (Serial) {
    Serial.println("Entering sleep mode for " + String(secondsToNext) + " seconds...");
    Serial.flush();
  }
  Serial.end(); 

  long sleepCycles = secondsToNext / 8;
  for (long i = 0; i < sleepCycles; i++) {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  }

  Serial.begin(9600);
  if (Serial) Serial.println("Waking up...");

  pinMode(SENSOR_POWER_PIN, OUTPUT);
  pinMode(GSM_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  digitalWrite(GSM_POWER_PIN, LOW);

  if (shouldPerformCycle()) {
    performCycle();
    saveLastActivation();
  }
  enterSleepMode();
}

long calculateSecondsToNext() {
  int currentHour = lastTimeStr.substring(0, 2).toInt();
  int currentMinute = lastTimeStr.substring(3, 5).toInt();
  int currentSecond = lastTimeStr.substring(6, 8).toInt();

  long secondsToNextHour = (60 - currentMinute) * 60 - currentSecond;

  if (currentMinute <= 5) {
    secondsToNextHour += 3600; 
  }

  return secondsToNextHour;
}

void powerCycleGSM() {
 
  digitalWrite(GSM_POWER_PIN, LOW);
  delay(1000); 
  digitalWrite(GSM_POWER_PIN, HIGH);
  delay(10000); 
  sim800.begin(9600);
  delay(2000);
}

bool connectToGPRS() {
  for (int i = 0; i < MAX_GPRS_RETRIES; i++) {
    if (sendCommand("AT", "OK") &&
        sendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK") &&
        sendCommand("AT+SAPBR=3,1,\"APN\",\"Mobitel\"", "OK") &&
        sendCommand("AT+SAPBR=1,1", "OK")) {
      delay(3000);
      if (sendCommand("AT+SAPBR=2,1", "OK")) {
        if (Serial) Serial.println("GPRS connected");
        return true;
      }
    }
    if (Serial) Serial.println("GPRS retry " + String(i + 1) + "/" + String(MAX_GPRS_RETRIES));
    delay(2000);
  }
  return false;
}

bool sendGET(String url) {
  bool success = sendCommand("AT+HTTPTERM", "OK") &&
                 sendCommand("AT+HTTPINIT", "OK") &&
                 sendCommand("AT+HTTPPARA=\"CID\",1", "OK") &&
                 sendCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", "OK") &&
                 sendCommand("AT+HTTPACTION=0", "OK");
  if (success) {
    delay(6000);
    success = sendCommand("AT+HTTPREAD", "OK") &&
              sendCommand("AT+HTTPTERM", "OK");
  }
  return success;
}

bool sendSMS(String message, String phoneNumber) {
  if (!sendCommand("AT", "OK") || !sendCommand("AT+CMGF=1", "OK")) {
    return false;
  }

  sim800.print("AT+CMGS=\"" + phoneNumber + "\"\r");
  delay(1000);
  sim800.print(message);
  delay(500);
  sim800.write(26); 
  delay(5000);

  String response = readSerialResponse(SERIAL_TIMEOUT);
  return response.indexOf("OK") != -1;
}

bool updateNetworkTime() {
  String dateTime = getNetworkTime();
  if (dateTime == "") {
    return false;
  }
  lastDateStr = parseDate(dateTime);
  lastTimeStr = parseTime(dateTime);
  if (Serial) Serial.println("Date: " + lastDateStr + ", Time: " + lastTimeStr);
  return true;
}

String getNetworkTime() {
  if (!sendCommand("AT+CLTS=1", "OK")) {
    return "";
  }
  sim800.println("AT+CCLK?");
  delay(1000);
  String response = readSerialResponse(SERIAL_TIMEOUT);
  int cclkIndex = response.indexOf("+CCLK:");
  if (cclkIndex != -1) {
    int start = response.indexOf("\"", cclkIndex);
    int end = response.indexOf("\"", start + 1);
    if (start != -1 && end != -1) {
      return response.substring(start, end + 1);
    }
  }
  return "";
}

String parseDate(String cclk) {
  if (cclk.length() < 8) return "00/00/0000";
  String datePart = cclk.substring(1, 9); // YY/MM/DD
  String year = "20" + datePart.substring(0, 2);
  String month = datePart.substring(3, 5);
  String day = cclk.substring(6, 8);
  return day + "/" + month + "/" + year;
}

String parseTime(String cclk) {
  if (cclk.length() < 17) return "00:00:00";
  return cclk.substring(10, 18); // HH:MM:SS
}

bool sendCommand(String cmd, String expectedResponse) {
  sim800.println(cmd);
  String response = readSerialResponse(SERIAL_TIMEOUT);
  if (Serial) {
    Serial.print("Command: ");
    Serial.print(cmd);
    Serial.print(" | Response: ");
    for (int i = 0; i < response.length(); i++) {
      if (response[i] >= 32 && response[i] <= 126) {
        Serial.print(response[i]);
      } else {
        Serial.print("[0x");
        Serial.print(response[i], HEX);
        Serial.print("]");
      }
    }
    Serial.println();
  }
  return response.indexOf(expectedResponse) != -1;
}

String readSerialResponse(unsigned long timeout) {
  char buffer[MAX_RESPONSE_LENGTH];
  int index = 0;
  unsigned long startTime = millis();

  memset(buffer, 0, MAX_RESPONSE_LENGTH);

  while (millis() - startTime < timeout && index < MAX_RESPONSE_LENGTH - 1) {
    if (sim800.available()) {
      buffer[index++] = sim800.read();
    }
  }

  while (sim800.available()) {
    sim800.read();
  }

  return String(buffer);
}