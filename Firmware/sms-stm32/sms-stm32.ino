#include "DHT.h"  // Main DHT sensor library
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h> // Include the EEPROM library

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
#define SIM800L_BAUDRATE 9600

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

// --- EEPROM Configuration ---
#define EEPROM_COUNTER_ADDR 0 // EEPROM address to store the counter
#define EEPROM_PHONE_A_ADDR 1 // EEPROM address to store phone number A (15 bytes)
#define EEPROM_PHONE_B_ADDR 16 // EEPROM address to store phone number B (15 bytes)
#define EEPROM_PHONE_C_ADDR 31 // EEPROM address to store phone number C (15 bytes)
#define EEPROM_CUSTOMER_ID_ADDR 46 // EEPROM address to store customer ID (6 bytes)
#define PHONE_NUMBER_MAX_LENGTH 15
#define CUSTOMER_ID_MAX_LENGTH 6
#define SMS_SEND_THRESHOLD 12 // Send SMS when counter reaches this value

// Array of phone numbers to send SMS to (will be loaded from EEPROM)
String phoneNumbers[] = {"+94719593248", "+94719751003", "+94768378406"};
const int NUM_PHONE_NUMBERS = 3;
String customerID = "00000"; // Default customer ID

// State machine states
enum SystemState {
    STATE_INIT,
    STATE_WAIT_DHT,
    STATE_READ_DHT,
    STATE_WAIT_DS18B20,
    STATE_READ_DS18B20,
    STATE_PROCESS_DATA,
    STATE_IDLE
};

// Variables for sensor averaging and timing
float dhtHumSum = 0, dhtTempSum = 0, ds18b20TempSum = 0;
int readingCount = 0;
unsigned long lastSendTime = 0;
unsigned long lastStateChange = 0;    // Tracks the last state change time
unsigned long lastMainLoopTime = 0;   // Tracks the last main cycle completion
SystemState currentState = STATE_IDLE;

DHT dht(DHTPIN, DHTTYPE);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

String simResponseBuffer = "";  // Buffer for incoming SIM800L data
bool gsmIsInitializedAndReady = false; // Flag to track GSM module initialization status
int smsCounter = 0; // Counter for SMS sending

// Forward declarations for functions
void readSimResponse();
void handleSim800lInput();
void readSms(int messageIndex);
void deleteSms(int messageIndex);
void sendSMS(String message, String recipientNumber);
void loadPhoneNumbersFromEEPROM();
void savePhoneNumberToEEPROM(int index, String phoneNumber);
void loadCustomerIDFromEEPROM();
void saveCustomerIDToEEPROM(String id);
void processSmsCommand(String smsContent, String senderNumber);

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
  // Serial.println(F("System will wait for ADC condition on PA0 >= 975 to initialize GSM and operate.")); // Removed ADC condition message

  // Initialize EEPROM and read counter
  smsCounter = EEPROM.read(EEPROM_COUNTER_ADDR);
  Serial.print(F("EEPROM SMS Counter initialized to: "));
  Serial.println(smsCounter);
  
  // Load phone numbers and customer ID from EEPROM
  loadPhoneNumbersFromEEPROM();
  loadCustomerIDFromEEPROM();
  Serial.print(F("Customer ID: "));
  Serial.println(customerID);
  
  // ADC Pin Initialization (Removed as per request)
  // pinMode(ADC_INPUT_PIN, INPUT_ANALOG);
  // Serial.print(F("ADC Input Pin PA0 configured.\n"));

  // Sensor Initializations (DHT & DS18B20)
  Serial.print(F("Initializing DHT Sensor (Type: DHT11, Pin: PB1)\n"));
  dht.begin();
  Serial.println(F("DHT sensor dht.begin() called."));

  Serial.print(F("Initializing DS18B20 Sensor (Pin: PB0)\n"));
  ds18b20Sensors.begin();
  Serial.println(F("DS18B20 sensor ds18b20Sensors.begin() called."));
  
  // GSM Module will be initialized directly now
  Serial.println(F("------------------------------------"));
  gsmIsInitializedAndReady = initializeAndConfigureGsmModule(); // Initialize GSM directly
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("GSM Module initialization failed during setup."));
  }
}

// --- Loop Function: Runs repeatedly ---
void loop() {
    // Always check for incoming SMS messages
    handleSim800lInput();

    // Try to initialize GSM if not ready
    if (!gsmIsInitializedAndReady) {
        static unsigned long lastGsmInitAttempt = 0;
        if (millis() - lastGsmInitAttempt >= GSM_INIT_FAILURE_RETRY_DELAY) {
            Serial.println(F("Attempting to initialize GSM module..."));
            gsmIsInitializedAndReady = initializeAndConfigureGsmModule();
            lastGsmInitAttempt = millis();
            if (!gsmIsInitializedAndReady) {
                Serial.println(F("GSM Module initialization failed. Will retry after delay."));
                return;
            }
        }
        return;
    }

    // Main state machine for sensor readings and data processing
    unsigned long currentMillis = millis();
    float h, t, tempC_ds18b20;  // Declare variables outside of switch

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
                
                String sensorDataSms = "ID:" + customerID + " 60min Avg - DHT H:" + String(avgHum, 1) + 
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

  // Parse SMS content and sender number from response
  String smsContent = "";
  String senderNumber = "";
  
  // Look for SMS content in the response
  // Format: +CMGR: "REC UNREAD","+1234567890","","YY/MM/DD,HH:MM:SS+TZ"
  int cmgrIndex = simResponseBuffer.indexOf("+CMGR:");
  if (cmgrIndex != -1) {
    // Extract sender number
    int firstQuote = simResponseBuffer.indexOf("\"", cmgrIndex);
    if (firstQuote != -1) {
      int secondQuote = simResponseBuffer.indexOf("\"", firstQuote + 1);
      if (secondQuote != -1) {
        int thirdQuote = simResponseBuffer.indexOf("\"", secondQuote + 1);
        if (thirdQuote != -1) {
          int fourthQuote = simResponseBuffer.indexOf("\"", thirdQuote + 1);
          if (fourthQuote != -1) {
            senderNumber = simResponseBuffer.substring(thirdQuote + 1, fourthQuote);
          }
        }
      }
    }
    
    // Extract SMS content (after the header line)
    int newlineAfterHeader = simResponseBuffer.indexOf("\n", cmgrIndex);
    if (newlineAfterHeader != -1) {
      int contentStart = newlineAfterHeader + 1;
      int okIndex = simResponseBuffer.indexOf("\nOK", contentStart);
      if (okIndex != -1) {
        smsContent = simResponseBuffer.substring(contentStart, okIndex);
        smsContent.trim();
      }
    }
  }
  
  Serial.print(F("SMS from: ")); Serial.println(senderNumber);
  Serial.print(F("SMS content: ")); Serial.println(smsContent);
  
  // Process SMS commands if content is not empty
  if (smsContent.length() > 0) {
    processSmsCommand(smsContent, senderNumber);
  }

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

// --- Load phone numbers from EEPROM ---
void loadPhoneNumbersFromEEPROM() {
  Serial.println(F("Loading phone numbers from EEPROM..."));
  
  // Load Phone A
  phoneNumbers[0] = "";
  for (int i = 0; i < PHONE_NUMBER_MAX_LENGTH; i++) {
    char c = EEPROM.read(EEPROM_PHONE_A_ADDR + i);
    if (c != 0 && c != 255) { // 255 is uninitialized EEPROM
      phoneNumbers[0] += c;
    } else {
      break;
    }
  }
  if (phoneNumbers[0].length() == 0) {
    phoneNumbers[0] = "+94719593248"; // Default value
    savePhoneNumberToEEPROM(0, phoneNumbers[0]);
  }
  
  // Load Phone B
  phoneNumbers[1] = "";
  for (int i = 0; i < PHONE_NUMBER_MAX_LENGTH; i++) {
    char c = EEPROM.read(EEPROM_PHONE_B_ADDR + i);
    if (c != 0 && c != 255) {
      phoneNumbers[1] += c;
    } else {
      break;
    }
  }
  if (phoneNumbers[1].length() == 0) {
    phoneNumbers[1] = "+94719751003"; // Default value
    savePhoneNumberToEEPROM(1, phoneNumbers[1]);
  }
  
  // Load Phone C
  phoneNumbers[2] = "";
  for (int i = 0; i < PHONE_NUMBER_MAX_LENGTH; i++) {
    char c = EEPROM.read(EEPROM_PHONE_C_ADDR + i);
    if (c != 0 && c != 255) {
      phoneNumbers[2] += c;
    } else {
      break;
    }
  }
  if (phoneNumbers[2].length() == 0) {
    phoneNumbers[2] = "+94768378406"; // Default value
    savePhoneNumberToEEPROM(2, phoneNumbers[2]);
  }
  
  Serial.print(F("Phone A: ")); Serial.println(phoneNumbers[0]);
  Serial.print(F("Phone B: ")); Serial.println(phoneNumbers[1]);
  Serial.print(F("Phone C: ")); Serial.println(phoneNumbers[2]);
}

// --- Save phone number to EEPROM ---
void savePhoneNumberToEEPROM(int index, String phoneNumber) {
  int addr;
  switch (index) {
    case 0: addr = EEPROM_PHONE_A_ADDR; break;
    case 1: addr = EEPROM_PHONE_B_ADDR; break;
    case 2: addr = EEPROM_PHONE_C_ADDR; break;
    default: return;
  }
  
  // Clear the EEPROM area first
  for (int i = 0; i < PHONE_NUMBER_MAX_LENGTH; i++) {
    EEPROM.write(addr + i, 0);
  }
  
  // Write the phone number
  for (int i = 0; i < phoneNumber.length() && i < PHONE_NUMBER_MAX_LENGTH - 1; i++) {
    EEPROM.write(addr + i, phoneNumber.charAt(i));
  }
  
  Serial.print(F("Saved phone number ")); 
  Serial.print(char('A' + index)); 
  Serial.print(F(": ")); 
  Serial.println(phoneNumber);
}

// --- Load customer ID from EEPROM ---
void loadCustomerIDFromEEPROM() {
  customerID = "";
  for (int i = 0; i < CUSTOMER_ID_MAX_LENGTH; i++) {
    char c = EEPROM.read(EEPROM_CUSTOMER_ID_ADDR + i);
    if (c != 0 && c != 255) { // 255 is uninitialized EEPROM
      customerID += c;
    } else {
      break;
    }
  }
  if (customerID.length() == 0) {
    customerID = "00000"; // Default value
    saveCustomerIDToEEPROM(customerID);
  }
}

// --- Save customer ID to EEPROM ---
void saveCustomerIDToEEPROM(String id) {
  // Clear the EEPROM area first
  for (int i = 0; i < CUSTOMER_ID_MAX_LENGTH; i++) {
    EEPROM.write(EEPROM_CUSTOMER_ID_ADDR + i, 0);
  }
  
  // Write the customer ID
  for (int i = 0; i < id.length() && i < CUSTOMER_ID_MAX_LENGTH - 1; i++) {
    EEPROM.write(EEPROM_CUSTOMER_ID_ADDR + i, id.charAt(i));
  }
  
  Serial.print(F("Saved customer ID: ")); 
  Serial.println(id);
}

// --- Process SMS commands ---
void processSmsCommand(String smsContent, String senderNumber) {
  smsContent.trim();
  smsContent.toUpperCase();
  
  Serial.print(F("Processing command: ")); Serial.println(smsContent);
  
  String responseMessage = "";
  bool commandProcessed = false;
  
  // Check for NUMSETA command
  if (smsContent.startsWith("NUMSETA ")) {
    String newNumber = smsContent.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0 && newNumber.length() <= PHONE_NUMBER_MAX_LENGTH - 1) {
      phoneNumbers[0] = newNumber;
      savePhoneNumberToEEPROM(0, newNumber);
      responseMessage = "ID:" + customerID + " Phone A set to: " + newNumber;
      commandProcessed = true;
    } else {
      responseMessage = "ID:" + customerID + " Error: Invalid phone number format";
      commandProcessed = true;
    }
  }
  // Check for NUMSETB command
  else if (smsContent.startsWith("NUMSETB ")) {
    String newNumber = smsContent.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0 && newNumber.length() <= PHONE_NUMBER_MAX_LENGTH - 1) {
      phoneNumbers[1] = newNumber;
      savePhoneNumberToEEPROM(1, newNumber);
      responseMessage = "ID:" + customerID + " Phone B set to: " + newNumber;
      commandProcessed = true;
    } else {
      responseMessage = "ID:" + customerID + " Error: Invalid phone number format";
      commandProcessed = true;
    }
  }
  // Check for NUMSETC command
  else if (smsContent.startsWith("NUMSETC ")) {
    String newNumber = smsContent.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0 && newNumber.length() <= PHONE_NUMBER_MAX_LENGTH - 1) {
      phoneNumbers[2] = newNumber;
      savePhoneNumberToEEPROM(2, newNumber);
      responseMessage = "ID:" + customerID + " Phone C set to: " + newNumber;
      commandProcessed = true;
    } else {
      responseMessage = "ID:" + customerID + " Error: Invalid phone number format";
      commandProcessed = true;
    }
  }
  // Check for SETID command
  else if (smsContent.startsWith("SETID ")) {
    String newID = smsContent.substring(6);
    newID.trim();
    if (newID.length() > 0 && newID.length() <= CUSTOMER_ID_MAX_LENGTH - 1) {
      customerID = newID;
      saveCustomerIDToEEPROM(newID);
      responseMessage = "ID:" + customerID + " Customer ID set to: " + newID;
      commandProcessed = true;
    } else {
      responseMessage = "ID:" + customerID + " Error: Invalid ID format";
      commandProcessed = true;
    }
  }
  
  // Send response SMS if a command was processed
  if (commandProcessed) {
    Serial.println(F("Sending command response SMS..."));
    sendSMS(responseMessage, senderNumber);
  }
}
