#include "DHT.h"  // Main DHT sensor library
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>  // For GSM module communication

// --- Configuration for DHT Sensor ---
#define DHTPIN PB1     // Digital pin connected to the DHT sensor (User specified)
#define DHTTYPE DHT11  // DHT 11 (User specified)

// --- Configuration for DS18B20 Sensor ---
#define ONE_WIRE_BUS PB0  // Data wire for DS18B20 (User specified)

// --- Configuration for GSM SIM800L Module ---
#define GSM_RX_PIN PA3                          // Bluepill RX from SIM800L TX
#define GSM_TX_PIN PA2                          // Bluepill TX to SIM800L RX
HardwareSerial sim800l(GSM_RX_PIN, GSM_TX_PIN);  // RX, TX

String phoneNumber = "+94719593248";  // Phone number to send SMS to

DHT dht(DHTPIN, DHTTYPE);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

String simResponseBuffer = "";  // Buffer for incoming SIM800L data

// --- Setup Function: Runs once when the Bluepill starts ---
void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }
  Serial.println(F("DHT, DS18B20 Sensor Test with GSM SMS (Send & Receive)"));

  // Sensor Initializations (DHT & DS18B20)
  Serial.print(F("Initializing DHT Sensor (Type: DHT11, Pin: PB1)\n"));
  dht.begin();
  Serial.println(F("DHT sensor dht.begin() called."));

  Serial.print(F("Initializing DS18B20 Sensor (Pin: PB0)\n"));
  ds18b20Sensors.begin();
  Serial.println(F("DS18B20 sensor ds18b20Sensors.begin() called."));

  // GSM SIM800L Initialization
  Serial.println(F("Initializing SIM800L GSM Module..."));
  sim800l.begin(115200);
  delay(1000);

  Serial.println(F("Configuring SIM800L..."));
  sim800l.println("ATE0");  // Echo off (optional, makes parsing cleaner)
  delay(500);
  readSimResponse();  // Clear buffer

  sim800l.println("AT");  // Handshake
  delay(500);
  readSimResponse();

  sim800l.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");  // Use SIM storage for SMS
  delay(500);
  readSimResponse();

  sim800l.println("AT+CMGF=1");  // Set SMS to TEXT mode
  delay(500);
  readSimResponse();

  // Configure New SMS Indication: +CMTI: <mem>,<index>
  // AT+CNMI=2,1,0,0,0 -> Forwards "+CMTI: <mem>,<index>" URC when SMS arrives in storage <mem>
  sim800l.println("AT+CNMI=2,1,0,0,0");
  delay(500);
  readSimResponse();

  Serial.println(F("GSM Module Initialized for sending and receiving SMS notifications."));
  Serial.println(F("------------------------------------"));
  // Reminders...
}

// --- Loop Function: Runs repeatedly ---
void loop() {
  handleSim800lInput();  // Check for incoming SMS or other data

  String sensorDataSms = "";

  // --- DHT Sensor Reading ---
  Serial.println(F("Waiting 2 seconds before DHT reading..."));
  delay(2000);

  Serial.println(F("--- Reading DHT Sensor ---"));
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F(">>> ERROR: Failed to read from DHT sensor! <<<"));
    sensorDataSms += "DHT Err; ";
  } else {
    Serial.print(F("DHT - H:"));
    Serial.print(h);
    Serial.print(F("% T:"));
    Serial.print(t);
    Serial.println(F("C"));
    sensorDataSms += "DHT H:" + String(h, 1) + "% T:" + String(t, 1) + "C; ";
  }

  // --- DS18B20 Sensor Reading ---
  delay(100);
  Serial.println(F("--- Reading DS18B20 Sensor ---"));
  Serial.print(F("Requesting DS18B20 temps... "));
  ds18b20Sensors.requestTemperatures();
  Serial.println(F("Done."));
  float tempC_ds18b20 = ds18b20Sensors.getTempCByIndex(0);

  if (tempC_ds18b20 == DEVICE_DISCONNECTED_C || tempC_ds18b20 == 85.0) {
    Serial.println(F(">>> ERROR: Failed to read from DS18B20! <<<"));
    sensorDataSms += "DS18B20 Err";
  } else {
    Serial.print(F("DS18B20 - T:"));
    Serial.print(tempC_ds18b20);
    Serial.println(F("C"));
    sensorDataSms += "DS18B20 T:" + String(tempC_ds18b20, 1) + "C";
  }

  // --- Send SMS with Sensor Data ---
  if (sensorDataSms.length() > 0) {
    Serial.println(F("--- Sending SMS with Sensor Data ---"));
    Serial.print(F("Message: "));
    Serial.println(sensorDataSms);
    sendSMS(sensorDataSms);
  }

  Serial.println(F("------------------------------------"));
  Serial.println(F("Waiting 30 seconds before next cycle..."));
  delay(30000);
}

// --- Read and print SIM800L response (basic version) ---
void readSimResponse() {
  unsigned long startTime = millis();
  simResponseBuffer = "";
  while (millis() - startTime < 1000) {  // Timeout after 1 second
    while (sim800l.available()) {
      char c = sim800l.read();
      Serial.write(c);  // Echo to Serial Monitor
      simResponseBuffer += c;
    }
  }
  if (simResponseBuffer.length() > 0) {
    Serial.println();  // Newline after printing buffer if it had content
  }
}

// --- Handle Incoming Data from SIM800L (including SMS notifications) ---
void handleSim800lInput() {
  simResponseBuffer = "";  // Clear or reuse buffer
  while (sim800l.available()) {
    char c = sim800l.read();
    simResponseBuffer += c;
    // Process complete lines
    if (c == '\n') {
      Serial.print(F("SIM_RECV: "));
      Serial.print(simResponseBuffer);  // Print raw line
      // Check for New SMS Notification: +CMTI: "SM",<index>
      if (simResponseBuffer.startsWith("+CMTI:")) {
        Serial.println(F(">>> New SMS Notification Received! <<<"));
        // Parse index: e.g., +CMTI: "SM",15
        int commaIndex = simResponseBuffer.indexOf(',');
        if (commaIndex != -1) {
          String indexStr = simResponseBuffer.substring(commaIndex + 1);
          indexStr.trim();  // Remove any leading/trailing whitespace/newlines
          int messageIndex = indexStr.toInt();
          if (messageIndex > 0) {
            Serial.print(F("Message Index: "));
            Serial.println(messageIndex);
            readSms(messageIndex);
          }
        }
      }
      simResponseBuffer = "";  // Clear buffer for next line
    }
  }
}

// --- Read a specific SMS message ---
void readSms(int messageIndex) {
  Serial.print(F("Reading SMS at index: "));
  Serial.println(messageIndex);
  sim800l.print("AT+CMGR=");
  sim800l.println(messageIndex);
  delay(1000);        // Wait for the response, which can be multi-line
  readSimResponse();  // This will print the full AT+CMGR response, including message body

  // Optionally, delete the message after reading
  deleteSms(messageIndex);
}

// --- Delete a specific SMS message ---
void deleteSms(int messageIndex) {
  Serial.print(F("Deleting SMS at index: "));
  Serial.println(messageIndex);
  sim800l.print("AT+CMGD=");
  sim800l.println(messageIndex);
  delay(1000);        // Wait for response
  readSimResponse();  // Print SIM800L's response (should be OK or ERROR)
}

// --- Helper function to send SMS ---
void sendSMS(String message) {
  Serial.print(F("Setting phone number: "));
  Serial.println(phoneNumber);
  sim800l.print("AT+CMGS=\"");
  sim800l.print(phoneNumber);
  sim800l.println("\"");
  delay(1000);        // Wait for '>' prompt
  readSimResponse();  // Should ideally check for '>' here

  Serial.print(F("Sending message: "));
  Serial.println(message);
  sim800l.println(message);
  delay(100);

  sim800l.write(26);  // CTRL+Z
  delay(5000);        // Wait for SMS send confirmation (e.g., +CMGS: XX)
  readSimResponse();
  Serial.println(F("SMS send attempt finished."));
}

// --- (Original updateSerial() removed as its functionality is partially
//      integrated into readSimResponse and handleSim800lInput for clarity,
//      and direct Serial->SIM forwarding is not essential for this automated flow) ---
