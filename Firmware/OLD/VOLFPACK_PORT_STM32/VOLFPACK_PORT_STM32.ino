#include <EEPROM.h>
#include <STM32LowPower.h>

// --- DEBUG FLAG ---
// Set to 1 to enable Serial debug messages.
// Set to 0 to disable Serial messages and reduce program size to fit on 64k chips.
#define DEBUG_MODE 0

// --- Pin Configuration ---
#define SOIL_PIN PA0                // Soil moisture sensor analog input (ADC1_IN0)
#define SENSOR_POWER_PIN PA1        // Pin to power the soil moisture sensor
#define GSM_POWER_PIN PA4           // Pin to control power to the SIM800L module

// Use STM32's Hardware Serial for the SIM800L on USART2
// PA2 (TX) -> SIM800L RX
// PA3 (RX) -> SIM800L TX
HardwareSerial Serial2(PA3, PA2); // Explicitly declare Serial2 for USART2
#define sim800l Serial2

// --- Network & API Configuration ---
String phone = "+94719751003";
String setupPhone = "+94768378406";
String baseURL = "https://script.google.com/macros/s/AKfycbwQpQfYRcNlkMecg-6csBLtCLEL6GCkW5Fu0j3Pr5IHcxAKuyTIQyXy7n3HroZb38Cs/exec";

// --- Timeouts and Retries ---
#define SERIAL_TIMEOUT 5000
#define MAX_RESPONSE_LENGTH 128
#define MAX_GPRS_RETRIES 3
#define MAX_AT_RETRIES 3

// --- EEPROM Addresses for Timestamp ---
#define EEPROM_ADDR_VALID 0      // Address to check if a valid timestamp is stored
#define EEPROM_ADDR_YEAR 1
#define EEPROM_ADDR_MONTH 3
#define EEPROM_ADDR_DAY 4
#define EEPROM_ADDR_HOUR 5
#define EEPROM_ADDR_MINUTE 6
#define EEPROM_ADDR_SECOND 7

// --- Global Variables ---
String lastDateStr = "00/00/0000";
String lastTimeStr = "00:00:00";
float lastSoilPercent = 0.0;
bool internetConnected = false;

// STM32LowPower LowPower; // This line is removed. The library creates a global instance for us.

struct Timestamp {
  int year;
  byte month;
  byte day;
  byte hour;
  byte minute;
  byte second;
} lastActivation;


void setup() {
  #if DEBUG_MODE
  Serial.begin(9600); // Debug serial on default pins (PA9, PA10) or USB CDC
  delay(2000);
  Serial.println("Booting STM32 Soil Moisture Sensor...");
  #endif

  // Initialize Low Power library
  LowPower.begin();

  // Configure GPIOs
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  pinMode(GSM_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  digitalWrite(GSM_POWER_PIN, LOW);
  pinMode(SOIL_PIN, INPUT_ANALOG);

  readLastActivation();

  if (shouldPerformCycle()) {
    #if DEBUG_MODE
    Serial.println("Condition met. Performing full cycle...");
    #endif
    performCycle();
    saveLastActivation();
  } else {
    #if DEBUG_MODE
    Serial.println("Condition not met. Entering sleep.");
    #endif
  }

  enterSleepMode();
}

void loop() {
  // The device wakes up from sleep, runs setup(), and goes back to sleep.
  // The loop is intentionally left empty.
}

// --- Core Logic Functions ---

bool shouldPerformCycle() {
  powerCycleGSM();

  bool sim800OK = false;
  for (int i = 0; i < MAX_AT_RETRIES; i++) {
    if (sendCommand("AT", "OK")) {
      sim800OK = true;
      break;
    }
    #if DEBUG_MODE
    Serial.println("SIM800L AT attempt " + String(i + 1) + "/" + String(MAX_AT_RETRIES));
    #endif
    delay(1000);
  }

  if (!sim800OK) {
    #if DEBUG_MODE
    Serial.println("SIM800L not responding. Going to sleep.");
    #endif
    sim800l.end();
    digitalWrite(GSM_POWER_PIN, LOW);
    return false;
  }

  String dateTime = getNetworkTime();
  sim800l.end();
  digitalWrite(GSM_POWER_PIN, LOW);

  if (dateTime == "") {
    #if DEBUG_MODE
    Serial.println("Failed to get network time. Going to sleep.");
    #endif
    return false;
  }

  lastDateStr = parseDate(dateTime);
  lastTimeStr = parseTime(dateTime);
  int currentHour = lastTimeStr.substring(0, 2).toInt();
  int currentMinute = lastTimeStr.substring(3, 5).toInt();

  // Condition: Run the cycle only during the first 5 minutes of every hour.
  if (currentMinute <= 5) {
    if (lastActivation.year == 0 || lastActivation.hour != currentHour || lastActivation.day != parseDate(dateTime).substring(0, 2).toInt()) {
      #if DEBUG_MODE
      Serial.println("New hour detected within the first 5 minutes. Time to run.");
      #endif
      return true;
    } else {
      #if DEBUG_MODE
      Serial.println("Already ran this hour. Skipping.");
      #endif
      return false;
    }
  }
  
  #if DEBUG_MODE
  Serial.println("Not within the first 5 minutes of the hour. Skipping.");
  #endif
  return false;
}

void performCycle() {
  // 1. Read Soil Sensor
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(200); // Allow sensor to stabilize
  int soilRaw = analogRead(SOIL_PIN);
  // Note: STM32 ADC is 12-bit (0-4095). We map this to 0-100%.
  // Adjust the raw min/max values based on your sensor's calibration in dry air and wet soil.
  lastSoilPercent = map(soilRaw, 4095, 0, 0, 100);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  #if DEBUG_MODE
  Serial.print("Soil Moisture (%): ");
  Serial.println(lastSoilPercent);
  #endif

  // 2. Power up GSM and connect
  powerCycleGSM();
  bool sim800OK = false;
  for (int i = 0; i < MAX_AT_RETRIES; i++) {
    if (sendCommand("AT", "OK")) {
      sim800OK = true;
      #if DEBUG_MODE
      Serial.println("SIM800L is running");
      #endif
      break;
    }
    delay(1000);
  }

  if (!sim800OK) {
    #if DEBUG_MODE
    Serial.println("SIM800L not responding after sensor read. Aborting cycle.");
    #endif
    return;
  }
  
  // 3. Update time (might have drifted slightly)
  updateNetworkTime();

  // 4. Connect to GPRS
  internetConnected = connectToGPRS();
  
  // 5. Send data via SMS and GPRS
  String message = "Date: " + lastDateStr + ", Time: " + lastTimeStr +
                   ", Soil Moisture (%): " + String(lastSoilPercent, 1) +
                   ", Internet: " + (internetConnected ? "Connected" : "Failed");

  if (sendSMS(message, phone)) {
    #if DEBUG_MODE
    Serial.println("Data SMS sent successfully to " + phone);
    #endif
  } else {
    #if DEBUG_MODE
    Serial.println("Data SMS failed");
    #endif
  }

  if (internetConnected) {
    String url = baseURL + "?sts=write" +
                 "&srs=ok" +
                 "&temp=" + String(lastSoilPercent, 1) + // Using 'temp' field for soil moisture
                 "&humd=" + "0" + // Humidity not used in this version
                 "&swtc1=" + lastDateStr +
                 "&swtc2=" + lastTimeStr;
    if (sendGET(url)) {
      #if DEBUG_MODE
      Serial.println("Google Sheet updated successfully");
      #endif
    } else {
      #if DEBUG_MODE
      Serial.println("Google Sheet update failed");
      #endif
    }
  } else {
    #if DEBUG_MODE
    Serial.println("Skipping Google Sheet update (no internet)");
    #endif
  }

  // 6. Power down GSM
  sim800l.end();
  digitalWrite(GSM_POWER_PIN, LOW);
  #if DEBUG_MODE
  Serial.println("Cycle complete. GSM powered down.");
  #endif
}

void enterSleepMode() {
  // Ensure everything is powered down
  digitalWrite(SENSOR_POWER_PIN, LOW);
  digitalWrite(GSM_POWER_PIN, LOW);
  if (sim800l) {
    sim800l.end();
  }
  
  #if DEBUG_MODE
  Serial.println("Entering deep sleep for 1 hour...");
  Serial.flush(); // Ensure all serial messages are sent before sleeping
  #endif

  // Sleep for approximately 1 hour. The device will reset on wakeup.
  // Note: STM32LowPower sleep is not perfectly accurate over long durations.
  LowPower.deepSleep(3600000); 
}


// --- EEPROM Functions ---
void saveLastActivation() {
  lastActivation.year = lastDateStr.substring(6, 10).toInt();
  lastActivation.month = lastDateStr.substring(3, 5).toInt();
  lastActivation.day = lastDateStr.substring(0, 2).toInt();
  lastActivation.hour = lastTimeStr.substring(0, 2).toInt();
  
  EEPROM.put(EEPROM_ADDR_YEAR, lastActivation.year);
  EEPROM.put(EEPROM_ADDR_MONTH, lastActivation.month);
  EEPROM.put(EEPROM_ADDR_DAY, lastActivation.day);
  EEPROM.put(EEPROM_ADDR_HOUR, lastActivation.hour);
  EEPROM.put(EEPROM_ADDR_VALID, (byte)1); // Mark data as valid
  // EEPROM.commit(); // This function does not exist in the STM32 EEPROM library
  #if DEBUG_MODE
  Serial.println("Saved last activation time to EEPROM.");
  #endif
}

void readLastActivation() {
  EEPROM.begin();
  byte isValid = 0;
  EEPROM.get(EEPROM_ADDR_VALID, isValid);

  if (isValid) {
    EEPROM.get(EEPROM_ADDR_YEAR, lastActivation.year);
    EEPROM.get(EEPROM_ADDR_MONTH, lastActivation.month);
    EEPROM.get(EEPROM_ADDR_DAY, lastActivation.day);
    EEPROM.get(EEPROM_ADDR_HOUR, lastActivation.hour);
    #if DEBUG_MODE
    Serial.println("Read last activation time from EEPROM.");
    #endif
  } else {
    lastActivation.year = 0; // Invalidate if no valid data found
    #if DEBUG_MODE
    Serial.println("No valid activation time in EEPROM.");
    #endif
  }
}

// --- GSM Helper Functions ---

void powerCycleGSM() {
  #if DEBUG_MODE
  Serial.println("Power cycling GSM module...");
  #endif
  digitalWrite(GSM_POWER_PIN, LOW);
  delay(1000);
  digitalWrite(GSM_POWER_PIN, HIGH);
  delay(10000); // Wait for SIM800L to boot and register
  sim800l.begin(9600); // Standard baud rate for SIM800L
  delay(2000);
}

bool connectToGPRS() {
  for (int i = 0; i < MAX_GPRS_RETRIES; i++) {
    if (sendCommand("AT", "OK") &&
        sendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK") &&
        sendCommand("AT+SAPBR=3,1,\"APN\",\"Mobitel\"", "OK") && // Change APN if needed
        sendCommand("AT+SAPBR=1,1", "OK")) {
      delay(3000);
      if (sendCommand("AT+SAPBR=2,1", "OK")) {
        #if DEBUG_MODE
        Serial.println("GPRS connected");
        #endif
        return true;
      }
    }
    #if DEBUG_MODE
    Serial.println("GPRS retry " + String(i + 1) + "/" + String(MAX_GPRS_RETRIES));
    #endif
    delay(2000);
  }
  return false;
}

bool sendGET(String url) {
  bool success = sendCommand("AT+HTTPINIT", "OK") &&
                 sendCommand("AT+HTTPPARA=\"CID\",1", "OK") &&
                 sendCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", "OK") &&
                 sendCommand("AT+HTTPACTION=0", "OK");
  if (success) {
    delay(8000); // Wait for HTTP action to complete
    sendCommand("AT+HTTPREAD", "OK"); // Read response (optional)
    sendCommand("AT+HTTPTERM", "OK"); // Terminate HTTP
  } else {
    sendCommand("AT+HTTPTERM", "OK"); // Terminate if failed
  }
  return success;
}

bool sendSMS(String message, String phoneNumber) {
  if (!sendCommand("AT+CMGF=1", "OK")) return false; // Set SMS text mode
  
  sim800l.print("AT+CMGS=\"" + phoneNumber + "\"\r");
  delay(1000);
  sim800l.print(message);
  delay(500);
  sim800l.write(26); // CTRL+Z to send
  
  String response = readSerialResponse(10000); // Wait longer for SMS send confirmation
  return response.indexOf("+CMGS:") != -1;
}

String getNetworkTime() {
  if (!sendCommand("AT+CLTS=1", "OK") || !sendCommand("AT&W", "OK")) {
      #if DEBUG_MODE
      Serial.println("Failed to enable network time sync.");
      #endif
      return "";
  }
  
  sim800l.println("AT+CCLK?");
  String response = readSerialResponse(2000);
  int cclkIndex = response.indexOf("+CCLK:");
  if (cclkIndex != -1) {
    int start = response.indexOf("\"", cclkIndex);
    int end = response.indexOf("\"", start + 1);
    if (start != -1 && end != -1) {
      return response.substring(start + 1, end);
    }
  }
  return "";
}

void updateNetworkTime() {
    String dateTime = getNetworkTime();
    if (dateTime != "") {
        lastDateStr = parseDate(dateTime);
        lastTimeStr = parseTime(dateTime);
        #if DEBUG_MODE
        Serial.println("Updated Time: " + lastDateStr + " " + lastTimeStr);
        #endif
    }
}

// --- Utility Functions ---
String parseDate(String cclk) {
  if (cclk.length() < 8) return "00/00/0000";
  String year = "20" + cclk.substring(0, 2);
  String month = cclk.substring(3, 5);
  String day = cclk.substring(6, 8);
  return day + "/" + month + "/" + year;
}

String parseTime(String cclk) {
  if (cclk.length() < 17) return "00:00:00";
  return cclk.substring(9, 17);
}

bool sendCommand(String cmd, String expectedResponse) {
  sim800l.println(cmd);
  String response = readSerialResponse(SERIAL_TIMEOUT);
  #if DEBUG_MODE
  Serial.print("CMD: " + cmd + " | RSP: " + response);
  #endif
  return response.indexOf(expectedResponse) != -1;
}

String readSerialResponse(unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    if (sim800l.available()) {
      response += (char)sim800l.read();
    }
  }
  return response;
}
