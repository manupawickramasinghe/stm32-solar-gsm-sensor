#include "DHT.h"  // Main DHT sensor library
#include <OneWire.h>
#include <DallasTemperature.h>
// #include <SoftwareSerial.h> // Not needed if using HardwareSerial correctly
// For STM32, ensure you are using the correct HardwareSerial include if necessary,
// or that your core handles HardwareSerial instantiation as you've done.

// --- Configuration for DHT Sensor ---
#define DHTPIN PB1     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11  // DHT 11

// --- Configuration for DS18B20 Sensor ---
#define ONE_WIRE_BUS PB0  // Data wire for DS18B20

// --- Configuration for ADC Input ---
#define ADC_INPUT_PIN PA0 // Analog input pin for the trigger condition

// --- Configuration for GSM SIM800L Module ---
#define GSM_RX_PIN PA3                          // Bluepill RX from SIM800L TX (USART2_RX)
#define GSM_TX_PIN PA2                          // Bluepill TX to SIM800L RX (USART2_TX)
HardwareSerial sim800l(GSM_RX_PIN, GSM_TX_PIN); // Assumes this constructor works for your STM32 core for USART2
                                                // Standard way might be: HardwareSerial Serial2(PA3, PA2); and use Serial2

// --- Baud Rates ---
#define DEBUG_SERIAL_BAUDRATE 9600
#define SIM800L_BAUDRATE 115200

// --- GSM Module Delays & Timeouts ---
#define SIM800L_BOOTUP_DELAY 2000       // Delay after sim800l.begin()
#define SIM800L_GENERIC_CMD_DELAY 500   // General delay after AT commands
#define SIM800L_CREG_RESPONSE_DELAY 1000 // Delay for CREG? command response
#define SIM800L_TIMESTAMP_READ_DELAY 200 // Delay for AT+CCLK? command response
#define SIM800L_SMS_READ_DELETE_DELAY 1000 // Delay for AT+CMGR/CMGD commands
#define SIM800L_SMS_SEND_CMD_DELAY 1000 // Delay after AT+CMGS command
#define SIM800L_SMS_SEND_DATA_DELAY 100 // Delay after sending SMS message content
#define SIM800L_SMS_SEND_END_DELAY 5000 // Delay after sending CTRL+Z for SMS

// --- Sensor Reading Delays ---
#define DHT_READ_PRE_DELAY 2000         // Delay before reading DHT sensor
#define DS18B20_READ_PRE_DELAY 100      // Delay before reading DS18B20 sensor

// --- Loop Control Delays ---
#define SERIAL_INIT_WAIT_DELAY 10       // Delay for Serial port initialization
#define MAIN_LOOP_CYCLE_DELAY 300000    // 5 minutes delay between full reading cycles
#define GSM_INIT_FAILURE_RETRY_DELAY 5000 // Delay before retrying GSM init after failure
#define ADC_IDLE_CHECK_DELAY 5000       // Delay when ADC condition is not met

// --- SIM800L Response Buffer Timeout ---
#define SIM800L_RESPONSE_READ_TIMEOUT 1000 // Timeout for reading SIM800L serial response

// Array of phone numbers to send SMS to
String phoneNumbers[] = {"+94719593248", "+94719751003", "+94768378406"};
const int NUM_PHONE_NUMBERS = 3;

// Variables for sensor averaging
float dhtHumSum = 0, dhtTempSum = 0, ds18b20TempSum = 0;
int readingCount = 0;
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 3600000; // 60 minutes in milliseconds

DHT dht(DHTPIN, DHTTYPE);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

String simResponseBuffer = "";  // Buffer for incoming SIM800L data
bool gsmIsInitializedAndReady = false; // Flag to track GSM module initialization status

// --- Function to get Timestamp from GSM Module ---
String getGsmTimestamp() {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("GSM not ready, cannot get timestamp."));
    return "TS_ERR";
  }
  Serial.println(F("Attempting to get GSM timestamp..."));
  sim800l.println("AT+CCLK?"); // Command to query clock
  delay(SIM800L_TIMESTAMP_READ_DELAY); 
  readSimResponse(); 

  int cclkIndex = simResponseBuffer.indexOf("+CCLK: \"");
  if (cclkIndex != -1) {
    int startIndex = cclkIndex + 8; 
    int endIndex = simResponseBuffer.indexOf("\"", startIndex);
    if (endIndex != -1) {
      String dateTimeGsm = simResponseBuffer.substring(startIndex, endIndex);
      if (dateTimeGsm.length() >= 17) { 
        String year = "20" + dateTimeGsm.substring(0, 2);
        String month = dateTimeGsm.substring(3, 5);
        String day = dateTimeGsm.substring(6, 8);
        String hour = dateTimeGsm.substring(9,11);
        String minute = dateTimeGsm.substring(12,14);
        String second = dateTimeGsm.substring(15,17);
        return year + "-" + month + "-" + day + " " + hour + ":" + minute + ":" + second;
      }
    }
  }
  Serial.println(F("Error parsing CCLK response or CCLK not found in buffer."));
  return "TS_ERR"; 
}

// --- Function to Initialize and Configure GSM Module ---
bool initializeAndConfigureGsmModule() {
  Serial.println(F("Attempting to initialize SIM800L GSM Module..."));
  sim800l.begin(SIM800L_BAUDRATE); // Baud rate for SIM800L communication
  delay(SIM800L_BOOTUP_DELAY); // Give module time to boot up after serial begin

  Serial.println(F("Configuring SIM800L..."));
  
  sim800l.println("ATE0");  // Echo off
  delay(SIM800L_GENERIC_CMD_DELAY);
  readSimResponse(); 
  // No critical check for ATE0 response, usually works or is optional.

  sim800l.println("AT");  // Handshake
  delay(SIM800L_GENERIC_CMD_DELAY);
  readSimResponse();
  if (simResponseBuffer.indexOf("OK") == -1) {
    Serial.println(F("GSM Init Error: AT command failed."));
    return false;
  }

  sim800l.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");  // Use SIM storage for SMS
  delay(SIM800L_GENERIC_CMD_DELAY);
  readSimResponse();
  if (simResponseBuffer.indexOf("OK") == -1) {
    Serial.println(F("GSM Init Warning: AT+CPMS command failed or no OK."));
    // This might not be critical for sending, but good for consistency
  }

  sim800l.println("AT+CMGF=1");  // Set SMS to TEXT mode
  delay(SIM800L_GENERIC_CMD_DELAY);
  readSimResponse();
  if (simResponseBuffer.indexOf("OK") == -1) {
    Serial.println(F("GSM Init Error: AT+CMGF=1 command failed."));
    return false;
  }

  sim800l.println("AT+CNMI=2,1,0,0,0"); // Configure New SMS Indication
  delay(SIM800L_GENERIC_CMD_DELAY);
  readSimResponse();
  if (simResponseBuffer.indexOf("OK") == -1) {
    Serial.println(F("GSM Init Warning: AT+CNMI command failed or no OK."));
    // This is important for receiving SMS notifications, but sending might still work
  }
  
  // Check network registration (optional but good for ensuring timestamp/SMS success)
  sim800l.println("AT+CREG?");
  delay(SIM800L_CREG_RESPONSE_DELAY); // Give time for CREG response
  readSimResponse();
  // Expected: +CREG: 0,1 (home network) or +CREG: 0,5 (roaming)
  if (simResponseBuffer.indexOf("+CREG: 0,1") == -1 && simResponseBuffer.indexOf("+CREG: 0,5") == -1) {
      Serial.println(F("GSM Warning: Not registered on network yet. Timestamp and SMS might fail."));
      // Consider if this should return false. For now, allow proceeding.
  }

  Serial.println(F("GSM Module Initialized and Configured successfully."));
  return true;
}


// --- Setup Function: Runs once when the Bluepill starts ---
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUDRATE); // For debugging output to PC
  while (!Serial) { delay(SERIAL_INIT_WAIT_DELAY); } // Wait for serial port to connect (needed for some boards)
  Serial.println(F("DHT, DS18B20 Sensor Test with GSM SMS (Send & Receive with Timestamp & ADC Trigger)"));
  Serial.println(F("System will wait for ADC condition on PA0 >= 975 to initialize GSM and operate."));

  // ADC Pin Initialization
  pinMode(ADC_INPUT_PIN, INPUT_ANALOG);
  Serial.print(F("ADC Input Pin PA0 configured.\n"));

  // Sensor Initializations (DHT & DS18B20)
  Serial.print(F("Initializing DHT Sensor (Type: DHT11, Pin: PB1)\n"));
  dht.begin();
  Serial.println(F("DHT sensor dht.begin() called."));

  Serial.print(F("Initializing DS18B20 Sensor (Pin: PB0)\n"));
  ds18b20Sensors.begin();
  Serial.println(F("DS18B20 sensor ds18b20Sensors.begin() called."));
  
  // GSM Module will be initialized in loop() based on ADC condition
  Serial.println(F("------------------------------------"));
}

// --- Loop Function: Runs repeatedly ---
void loop() {
  // --- ADC Reading Check ---
  int adcValue = analogRead(ADC_INPUT_PIN);
  Serial.print(F("Current ADC reading on PA0: "));
  Serial.println(adcValue);

  if (adcValue >= 975) {
    if (!gsmIsInitializedAndReady) {
      Serial.println(F("ADC condition met. Attempting to initialize GSM module..."));
      gsmIsInitializedAndReady = initializeAndConfigureGsmModule();
      if (!gsmIsInitializedAndReady) {
        Serial.println(F("GSM Module initialization failed. Will retry on next valid ADC reading."));
        delay(GSM_INIT_FAILURE_RETRY_DELAY); // Wait before retrying or next ADC check
        return; // Exit loop early if GSM init failed
      }
    }

    // Proceed only if GSM is initialized and ready
    if (gsmIsInitializedAndReady) {
      Serial.println(F("GSM Ready. Proceeding with sensor readings."));
      handleSim800lInput();  // Check for incoming SMS or other data

      // --- DHT Sensor Reading ---
      Serial.println(F("Waiting 2 seconds before DHT reading..."));
      delay(DHT_READ_PRE_DELAY); 

      Serial.println(F("--- Reading DHT Sensor ---"));
      float h = dht.readHumidity();
      float t = dht.readTemperature(); 

      if (!isnan(h) && !isnan(t)) {
        Serial.print(F("DHT - H:")); Serial.print(h); Serial.print(F("% T:")); Serial.print(t); Serial.println(F("C"));
        dhtHumSum += h;
        dhtTempSum += t;
      }

      // --- DS18B20 Sensor Reading ---
      delay(DS18B20_READ_PRE_DELAY); 
      Serial.println(F("--- Reading DS18B20 Sensor ---"));
      Serial.print(F("Requesting DS18B20 temps... "));
      ds18b20Sensors.requestTemperatures();
      Serial.println(F("Done."));
      float tempC_ds18b20 = ds18b20Sensors.getTempCByIndex(0); 

      if (tempC_ds18b20 != DEVICE_DISCONNECTED_C && tempC_ds18b20 != 85.0) { 
        Serial.print(F("DS18B20 - T:")); Serial.print(tempC_ds18b20); Serial.println(F("C"));
        ds18b20TempSum += tempC_ds18b20;
      }

      readingCount++;

      // Check if it's time to send the averaged data (every 60 minutes)
      if (millis() - lastSendTime >= SEND_INTERVAL) {
        String sensorDataSms = "";
        
        // Calculate averages
        if (readingCount > 0) {
          float avgHum = dhtHumSum / readingCount;
          float avgDhtTemp = dhtTempSum / readingCount;
          float avgDs18b20Temp = ds18b20TempSum / readingCount;
          
          sensorDataSms = "60min Avg - DHT H:" + String(avgHum, 1) + "% T:" + String(avgDhtTemp, 1) + "C; ";
          sensorDataSms += "DS18B20 T:" + String(avgDs18b20Temp, 1) + "C";
          
          // Send SMS to all numbers
          Serial.println(F("--- Sending SMS with Average Sensor Data ---"));
          for (int i = 0; i < NUM_PHONE_NUMBERS; i++) {
            sendSMS(sensorDataSms, phoneNumbers[i]);
          }
          
          // Reset averages
          dhtHumSum = 0;
          dhtTempSum = 0;
          ds18b20TempSum = 0;
          readingCount = 0;
          lastSendTime = millis();
        }
      }

      Serial.println(F("------------------------------------"));
      Serial.println(F("Reading cycle complete. Waiting 5 minutes..."));
      delay(MAIN_LOOP_CYCLE_DELAY); // 5 minute delay between readings
    }
  } else {
    Serial.println(F("ADC value below 975. System idle. GSM operations paused."));
    if (gsmIsInitializedAndReady) {
        Serial.println(F("GSM module was active, now pausing operations. Will re-initialize if ADC condition met again."));
        // Optionally, you could send AT+CPOWD=1 to power down the module here to save power,
        // but then initializeAndConfigureGsmModule would need to handle waking it or a longer startup.
        // For now, just setting the flag ensures re-initialization logic.
    }
    gsmIsInitializedAndReady = false; // Reset flag so GSM re-initializes if ADC goes high again
    delay(ADC_IDLE_CHECK_DELAY); // Wait for 5 seconds before checking ADC again
  }
}

// --- Read and print SIM800L response ---
void readSimResponse() {
  unsigned long startTime = millis();
  simResponseBuffer = ""; 
  while (millis() - startTime < SIM800L_RESPONSE_READ_TIMEOUT) {  
    while (sim800l.available()) {
      char c = sim800l.read();
      Serial.write(c);  
      simResponseBuffer += c;
    }
  }
}

// --- Handle Incoming Data from SIM800L (including SMS notifications) ---
void handleSim800lInput() {
  if (!gsmIsInitializedAndReady) return; // Don't process if GSM not ready

  static String localSimBuffer = ""; 
  while (sim800l.available()) {
    char c = sim800l.read();
    localSimBuffer += c;
    if (c == '\n') { 
      Serial.print(F("SIM_RECV_URC: "));
      Serial.print(localSimBuffer);  
      
      if (localSimBuffer.startsWith("+CMTI:")) {
        Serial.println(F(">>> New SMS Notification Received! <<<"));
        int commaIndex = localSimBuffer.indexOf(',');
        if (commaIndex != -1) {
          String indexStr = localSimBuffer.substring(commaIndex + 1);
          indexStr.trim();  
          int messageIndex = indexStr.toInt();
          if (messageIndex > 0) {
            Serial.print(F("Message Index: ")); Serial.println(messageIndex);
            readSms(messageIndex);
          }
        }
      }
      localSimBuffer = "";  
    }
  }
}

// --- Read a specific SMS message ---
void readSms(int messageIndex) {
  if (!gsmIsInitializedAndReady) return;

  Serial.print(F("Reading SMS at index: ")); Serial.println(messageIndex);
  sim800l.print("AT+CMGR=");
  sim800l.println(messageIndex);
  delay(SIM800L_SMS_READ_DELETE_DELAY);        
  readSimResponse();  

  deleteSms(messageIndex);
}

// --- Delete a specific SMS message ---
void deleteSms(int messageIndex) {
  if (!gsmIsInitializedAndReady) return;

  Serial.print(F("Deleting SMS at index: ")); Serial.println(messageIndex);
  sim800l.print("AT+CMGD=");
  sim800l.println(messageIndex);
  delay(SIM800L_SMS_READ_DELETE_DELAY);        
  readSimResponse();  
}

// --- Helper function to send SMS with Timestamp ---
void sendSMS(String message, String recipientNumber) {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("Cannot send SMS, GSM module not ready."));
    return;
  }

  String timestamp = getGsmTimestamp();
  String messageWithTimestamp;

  if (timestamp == "TS_ERR" || timestamp.length() == 0) {
    Serial.println(F("Failed to get GSM timestamp. Sending SMS with placeholder."));
    messageWithTimestamp = "No TS - " + message; 
  } else {
    messageWithTimestamp = timestamp + " - " + message;
  }
  
  if (messageWithTimestamp.length() > 160) {
      Serial.print(F("Warning: SMS (len ")); Serial.print(messageWithTimestamp.length()); Serial.println(F(") is long, may be truncated or split."));
  }

  Serial.print(F("Setting phone number: ")); Serial.println(recipientNumber);
  sim800l.print("AT+CMGS=\"");
  sim800l.print(recipientNumber);
  sim800l.println("\"");
  delay(SIM800L_SMS_SEND_CMD_DELAY);        
  readSimResponse();  

  Serial.print(F("Sending full message: ")); Serial.println(messageWithTimestamp);
  sim800l.println(messageWithTimestamp); 
  delay(SIM800L_SMS_SEND_DATA_DELAY); 

  sim800l.write(26);  
  delay(SIM800L_SMS_SEND_END_DELAY);        
  readSimResponse();  
  Serial.println(F("SMS send attempt finished."));
}
