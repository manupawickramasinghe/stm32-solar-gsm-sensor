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
#define SIM800L_BOOTUP_DELAY 3000       // Increased delay after sim800l.begin()
#define SIM800L_GENERIC_CMD_DELAY 1000  // Increased general delay after AT commands
#define SIM800L_CREG_RESPONSE_DELAY 2000 // Increased delay for CREG? command response
#define SIM800L_TIMESTAMP_READ_DELAY 1000 // Increased delay for AT+CCLK? command response
#define SIM800L_SMS_READ_DELETE_DELAY 2000 // Increased delay for AT+CMGR/CMGD commands
#define SIM800L_SMS_SEND_CMD_DELAY 3000 // Increased delay after AT+CMGS command
#define SIM800L_SMS_SEND_DATA_DELAY 1000 // Increased delay after sending SMS message content
#define SIM800L_SMS_SEND_END_DELAY 10000 // Increased delay after sending CTRL+Z for SMS

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
#define EEPROM_PHONE_A_ADDR 4 // EEPROM address for phone number A (20 bytes)
#define EEPROM_PHONE_B_ADDR 24 // EEPROM address for phone number B (20 bytes)
#define EEPROM_PHONE_C_ADDR 44 // EEPROM address for phone number C (20 bytes)
#define EEPROM_CUSTOMER_ID_ADDR 64 // EEPROM address for customer ID (10 bytes)
#define EEPROM_INIT_FLAG_ADDR 74 // EEPROM address for initialization flag
#define SMS_SEND_THRESHOLD 12 // Send SMS when counter reaches this value

// Phone number storage
#define MAX_PHONE_LENGTH 20
#define MAX_CUSTOMER_ID_LENGTH 10

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

// GSM initialization states
enum GsmInitState {
    GSM_INIT_START,
    GSM_INIT_BOOT_DELAY,
    GSM_INIT_ECHO_OFF,
    GSM_INIT_ECHO_OFF_WAIT,
    GSM_INIT_AT_TEST,
    GSM_INIT_AT_TEST_WAIT,
    GSM_INIT_CPMS,
    GSM_INIT_CPMS_WAIT,
    GSM_INIT_CMGF,
    GSM_INIT_CMGF_WAIT,
    GSM_INIT_CNMI,
    GSM_INIT_CNMI_WAIT,
    GSM_INIT_CREG,
    GSM_INIT_CREG_WAIT,
    GSM_INIT_COMPLETE,
    GSM_INIT_FAILED
};

// SMS operation states
enum SmsOperationState {
    SMS_IDLE,
    SMS_READ_WAIT,
    SMS_DELETE_WAIT,
    SMS_SEND_CMD_WAIT,
    SMS_SEND_DATA_WAIT,
    SMS_SEND_END_WAIT
};

// SMS processing states
enum SmsProcessingState {
    SMS_PROC_IDLE,
    SMS_PROC_READING,
    SMS_PROC_PROCESSING,
    SMS_PROC_RESPONDING
};

// Timestamp operation state
enum TimestampState {
    TS_IDLE,
    TS_WAITING_RESPONSE
};

// Variables for sensor averaging and timing
float dhtHumSum = 0, dhtTempSum = 0, ds18b20TempSum = 0;
int readingCount = 0;
unsigned long lastSendTime = 0;
unsigned long lastStateChange = 0;    // Tracks the last state change time
unsigned long lastMainLoopTime = 0;   // Tracks the last main cycle completion
SystemState currentState = STATE_IDLE;

// GSM operation timing variables
GsmInitState gsmInitState = GSM_INIT_START;
unsigned long gsmInitTimestamp = 0;
SmsOperationState smsOpState = SMS_IDLE;
unsigned long smsOpTimestamp = 0;
int pendingSmsIndex = 0;
TimestampState tsState = TS_IDLE;
unsigned long tsTimestamp = 0;

// SMS sending state variables
String pendingSmsMessage = "";
String pendingSmsNumber = "";
int currentPhoneIndex = 0;

// SMS command processing variables
SmsProcessingState smsProcessingState = SMS_PROC_IDLE;
String receivedSmsContent = "";
String senderNumber = "";
int processingSmsIndex = 0;
unsigned long smsProcessingTimeout = 0;
String fullSmsResponse = ""; // Buffer to capture complete SMS response

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
bool updateGsmInitialization();
void updateSmsOperations();
void updateTimestampOperation();
bool isTimestampReady();
void startTimestampRequest();
void startSmsRead(int messageIndex);
void startSmsDelete(int messageIndex);
void startSmsSend(String message, String recipientNumber);
void loadConfigFromEEPROM();
void savePhoneNumberToEEPROM(int index, String phoneNumber);
void saveCustomerIDToEEPROM(String id);
void processSmsCommand(String smsContent, String sender);
void updateSmsProcessing();
void parseAndExecuteSmsCommand(String command, String sender);
void extractSmsContentAndSender();

// --- Non-blocking Timestamp Functions ---
void startTimestampRequest() {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("GSM not ready, cannot get timestamp."));
    tsState = TS_IDLE;
    return;
  }
  Serial.println(F("Attempting to get GSM timestamp..."));
  sim800l.println("AT+CCLK?"); // Command to query clock
  tsState = TS_WAITING_RESPONSE;
  tsTimestamp = millis();
}

bool isTimestampReady() {
  return tsState == TS_IDLE;
}

void updateTimestampOperation() {
  if (tsState == TS_WAITING_RESPONSE) {
    if (millis() - tsTimestamp >= SIM800L_TIMESTAMP_READ_DELAY) {
      readSimResponse();
      tsState = TS_IDLE;
    }
  }
}

String getGsmTimestamp() {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("GSM not ready, cannot get timestamp."));
    return "TS_ERR";
  }
  
  // Start the request if not already started
  if (tsState == TS_IDLE) {
    startTimestampRequest();
  }
  
  // Wait for completion (this makes it blocking for now, but allows other operations)
  while (tsState != TS_IDLE) {
    updateTimestampOperation();
  }

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

// --- EEPROM Configuration Functions ---
void loadConfigFromEEPROM() {
  // Check if EEPROM has been initialized
  if (EEPROM.read(EEPROM_INIT_FLAG_ADDR) != 0x55) {
    Serial.println(F("EEPROM not initialized, using defaults and saving them."));
    // Save default values to EEPROM
    savePhoneNumberToEEPROM(0, phoneNumbers[0]);
    savePhoneNumberToEEPROM(1, phoneNumbers[1]);
    savePhoneNumberToEEPROM(2, phoneNumbers[2]);
    saveCustomerIDToEEPROM(customerID);
    EEPROM.write(EEPROM_INIT_FLAG_ADDR, 0x55); // Mark as initialized
  } else {
    Serial.println(F("Loading configuration from EEPROM..."));
    // Load phone numbers
    for (int i = 0; i < NUM_PHONE_NUMBERS; i++) {
      phoneNumbers[i] = "";
      int addr = EEPROM_PHONE_A_ADDR + (i * MAX_PHONE_LENGTH);
      for (int j = 0; j < MAX_PHONE_LENGTH; j++) {
        char c = EEPROM.read(addr + j);
        if (c == 0 || c == 255) break; // End of string
        phoneNumbers[i] += c;
      }
    }
    
    // Load customer ID
    customerID = "";
    for (int i = 0; i < MAX_CUSTOMER_ID_LENGTH; i++) {
      char c = EEPROM.read(EEPROM_CUSTOMER_ID_ADDR + i);
      if (c == 0 || c == 255) break; // End of string
      customerID += c;
    }
  }
  
  Serial.println(F("Configuration loaded:"));
  Serial.print(F("Phone A: ")); Serial.println(phoneNumbers[0]);
  Serial.print(F("Phone B: ")); Serial.println(phoneNumbers[1]);
  Serial.print(F("Phone C: ")); Serial.println(phoneNumbers[2]);
  Serial.print(F("Customer ID: ")); Serial.println(customerID);
}

void savePhoneNumberToEEPROM(int index, String phoneNumber) {
  if (index < 0 || index >= NUM_PHONE_NUMBERS) return;
  
  int addr = EEPROM_PHONE_A_ADDR + (index * MAX_PHONE_LENGTH);
  phoneNumbers[index] = phoneNumber;
  
  // Clear the EEPROM area first
  for (int i = 0; i < MAX_PHONE_LENGTH; i++) {
    EEPROM.write(addr + i, 0);
  }
  
  // Write the new phone number
  for (int i = 0; i < phoneNumber.length() && i < MAX_PHONE_LENGTH - 1; i++) {
    EEPROM.write(addr + i, phoneNumber[i]);
  }
  
  Serial.print(F("Phone number ")); 
  Serial.print((char)('A' + index)); 
  Serial.print(F(" saved: ")); 
  Serial.println(phoneNumber);
}

void saveCustomerIDToEEPROM(String id) {
  customerID = id;
  
  // Clear the EEPROM area first
  for (int i = 0; i < MAX_CUSTOMER_ID_LENGTH; i++) {
    EEPROM.write(EEPROM_CUSTOMER_ID_ADDR + i, 0);
  }
  
  // Write the new customer ID
  for (int i = 0; i < id.length() && i < MAX_CUSTOMER_ID_LENGTH - 1; i++) {
    EEPROM.write(EEPROM_CUSTOMER_ID_ADDR + i, id[i]);
  }
  
  Serial.print(F("Customer ID saved: ")); Serial.println(id);
}

// --- Non-blocking GSM Initialization Function ---
bool updateGsmInitialization() {
  unsigned long currentMillis = millis();
  
  switch (gsmInitState) {
    case GSM_INIT_START:
      Serial.println(F("Attempting to initialize SIM800L GSM Module..."));
      sim800l.begin(SIM800L_BAUDRATE);
      gsmInitTimestamp = currentMillis;
      gsmInitState = GSM_INIT_BOOT_DELAY;
      return false;
      
    case GSM_INIT_BOOT_DELAY:
      if (currentMillis - gsmInitTimestamp >= SIM800L_BOOTUP_DELAY) {
        Serial.println(F("Configuring SIM800L..."));
        sim800l.println("ATE0");  // Echo off
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_ECHO_OFF_WAIT;
      }
      return false;
      
    case GSM_INIT_ECHO_OFF_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        // Disable unsolicited result codes
        sim800l.println("AT+CIURC=0"); // Disable URC
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_AT_TEST;
      }
      return false;
      
    case GSM_INIT_AT_TEST:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        sim800l.println("AT");  // Handshake
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_AT_TEST_WAIT;
      }
      return false;
      
    case GSM_INIT_AT_TEST_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        if (simResponseBuffer.indexOf("OK") == -1) {
          Serial.println(F("GSM Init Error: AT command failed."));
          gsmInitState = GSM_INIT_FAILED;
          return false;
        }
        sim800l.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");  // Use SIM storage for SMS
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_CPMS_WAIT;
      }
      return false;
      
    case GSM_INIT_CPMS_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        if (simResponseBuffer.indexOf("OK") == -1) {
          Serial.println(F("GSM Init Warning: AT+CPMS command failed or no OK."));
        }
        sim800l.println("AT+CMGF=1");  // Set SMS to TEXT mode
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_CMGF_WAIT;
      }
      return false;
      
    case GSM_INIT_CMGF_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        if (simResponseBuffer.indexOf("OK") == -1) {
          Serial.println(F("GSM Init Error: AT+CMGF=1 command failed."));
          gsmInitState = GSM_INIT_FAILED;
          return false;
        }
        sim800l.println("AT+CNMI=2,1,0,0,0"); // Configure New SMS Indication
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_CNMI_WAIT;
      }
      return false;
      
    case GSM_INIT_CNMI_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_GENERIC_CMD_DELAY) {
        readSimResponse();
        if (simResponseBuffer.indexOf("OK") == -1) {
          Serial.println(F("GSM Init Warning: AT+CNMI command failed or no OK."));
        }
        sim800l.println("AT+CREG?");
        gsmInitTimestamp = currentMillis;
        gsmInitState = GSM_INIT_CREG_WAIT;
      }
      return false;
      
    case GSM_INIT_CREG_WAIT:
      if (currentMillis - gsmInitTimestamp >= SIM800L_CREG_RESPONSE_DELAY) {
        readSimResponse();
        if (simResponseBuffer.indexOf("+CREG: 0,1") == -1 && simResponseBuffer.indexOf("+CREG: 0,5") == -1) {
          Serial.println(F("GSM Warning: Not registered on network yet. Timestamp and SMS might fail."));
        }
        gsmInitState = GSM_INIT_COMPLETE;
        Serial.println(F("GSM Module Initialized and Configured successfully."));
        return true;
      }
      return false;
      
    case GSM_INIT_FAILED:
      gsmInitState = GSM_INIT_START; // Reset to try again
      return false;
      
    case GSM_INIT_COMPLETE:
      return true;
  }
  return false;
}

// --- Legacy function kept for compatibility ---
bool initializeAndConfigureGsmModule() {
  gsmInitState = GSM_INIT_START;
  while (gsmInitState != GSM_INIT_COMPLETE && gsmInitState != GSM_INIT_FAILED) {
    if (!updateGsmInitialization()) {
      // Allow some processing time between states
      unsigned long startWait = millis();
      while (millis() - startWait < 10) {
        // Small delay to prevent tight loop
      }
    }
  }
  return (gsmInitState == GSM_INIT_COMPLETE);
}


// --- Setup Function: Runs once when the Bluepill starts ---
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUDRATE); // For debugging output to PC
  // Non-blocking serial initialization - removed while(!Serial) delay loop
  Serial.println(F("DHT, DS18B20 Sensor Test with GSM SMS (Send & Receive with Timestamp & ADC Trigger)"));
  // Serial.println(F("System will wait for ADC condition on PA0 >= 975 to initialize GSM and operate.")); // Removed ADC condition message

  // Initialize EEPROM and read counter
  smsCounter = EEPROM.read(EEPROM_COUNTER_ADDR);
  Serial.print(F("EEPROM SMS Counter initialized to: "));
  Serial.println(smsCounter);
  
  // Load configuration from EEPROM
  loadConfigFromEEPROM();
  
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
  
  // GSM Module will be initialized in non-blocking manner in loop
  Serial.println(F("------------------------------------"));
  Serial.println(F("Starting non-blocking GSM initialization..."));
  gsmInitState = GSM_INIT_START;
}

// --- Loop Function: Runs repeatedly ---
void loop() {
    // Always update ongoing operations
    updateTimestampOperation();
    updateSmsOperations();
    updateSmsProcessing();
    
    // Always check for incoming SMS messages
    handleSim800lInput();

    // Non-blocking GSM initialization
    if (!gsmIsInitializedAndReady) {
        gsmIsInitializedAndReady = updateGsmInitialization();
        if (!gsmIsInitializedAndReady && gsmInitState == GSM_INIT_FAILED) {
            static unsigned long lastGsmInitAttempt = 0;
            if (millis() - lastGsmInitAttempt >= GSM_INIT_FAILURE_RETRY_DELAY) {
                Serial.println(F("Retrying GSM module initialization..."));
                gsmInitState = GSM_INIT_START;
                lastGsmInitAttempt = millis();
            }
        }
        return; // Don't proceed with sensor operations until GSM is ready
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
  bool dataReceived = false;
  
  while (millis() - startTime < SIM800L_RESPONSE_READ_TIMEOUT) {  
    while (sim800l.available()) {
      char c = sim800l.read();
      Serial.write(c);  
      simResponseBuffer += c;
      dataReceived = true;
    }
    
    // If we've received data and there's been a small pause, break out
    if (dataReceived && (millis() - startTime > 100)) {
      unsigned long pauseStart = millis();
      bool moreData = false;
      while (millis() - pauseStart < 50) { // Wait 50ms for more data
        if (sim800l.available()) {
          moreData = true;
          break;
        }
      }
      if (!moreData) break; // No more data coming, exit
    }
  }
  
  if (dataReceived) {
    Serial.println(); // Add line break for better readability
  }
}

// --- Handle Incoming Data from SIM800L (including SMS notifications) ---
void handleSim800lInput() {
  if (!gsmIsInitializedAndReady) return; // Don't process if GSM not ready

  static String localSimBuffer = ""; 
  while (sim800l.available()) {
    char c = sim800l.read();
    localSimBuffer += c;
    
    // If we're processing SMS, capture everything in fullSmsResponse
    if (smsProcessingState == SMS_PROC_READING) {
      fullSmsResponse += c;
    }
    
    if (c == '\n') { 
      // Trim the buffer to remove extra whitespace
      localSimBuffer.trim();
      
      // Skip empty lines or lines with just whitespace
      if (localSimBuffer.length() == 0) {
        localSimBuffer = "";
        continue;
      }
      
      // Filter out common unsolicited result codes
      if (localSimBuffer.startsWith("+CFUN:") || 
          localSimBuffer.startsWith("+CPIN:") || 
          localSimBuffer.startsWith("RDY") ||
          localSimBuffer.startsWith("SMS Ready") ||
          localSimBuffer.startsWith("Call Ready")) {
        // Just acknowledge these without printing
        localSimBuffer = "";
        continue;
      }
      
      // Only print non-empty, meaningful lines
      if (localSimBuffer.length() > 0) {
        Serial.print(F("SIM_RECV_URC: "));
        Serial.println(localSimBuffer);  
      }
      
      if (localSimBuffer.startsWith("+CMTI:")) {
        Serial.println(F(">>> New SMS Notification Received! <<<"));
        int commaIndex = localSimBuffer.indexOf(',');
        if (commaIndex != -1) {
          String indexStr = localSimBuffer.substring(commaIndex + 1);
          indexStr.trim();  
          int messageIndex = indexStr.toInt();
          if (messageIndex > 0) {
            Serial.print(F("Message Index: ")); Serial.println(messageIndex);
            
            // Start SMS command processing
            if (smsProcessingState == SMS_PROC_IDLE) {
              processingSmsIndex = messageIndex;
              smsProcessingState = SMS_PROC_READING;
              smsProcessingTimeout = millis();
              fullSmsResponse = ""; // Clear the SMS response buffer
              startSmsRead(messageIndex);
            } else {
              // If already processing, just read and delete
              startSmsRead(messageIndex);
            }
          }
        }
      }
      localSimBuffer = "";  
    }
  }
}

// --- Non-blocking SMS Operations ---
void updateSmsOperations() {
  unsigned long currentMillis = millis();
  
  switch (smsOpState) {
    case SMS_READ_WAIT:
      if (currentMillis - smsOpTimestamp >= SIM800L_SMS_READ_DELETE_DELAY) {
        readSimResponse();
        
        // Check if this is a command processing read
        if (smsProcessingState == SMS_PROC_READING) {
          // Use the captured fullSmsResponse instead of simResponseBuffer
          simResponseBuffer = fullSmsResponse;
          extractSmsContentAndSender();
          smsProcessingState = SMS_PROC_PROCESSING;
        }
        
        startSmsDelete(pendingSmsIndex);
      }
      break;
      
    case SMS_DELETE_WAIT:
      if (currentMillis - smsOpTimestamp >= SIM800L_SMS_READ_DELETE_DELAY) {
        readSimResponse();
        smsOpState = SMS_IDLE;
      }
      break;
      
    case SMS_SEND_CMD_WAIT:
      if (currentMillis - smsOpTimestamp >= SIM800L_SMS_SEND_CMD_DELAY) {
        readSimResponse();
        // Check if we got the ">" prompt
        if (simResponseBuffer.indexOf(">") != -1) {
          Serial.print(F("Sending full message: ")); Serial.println(pendingSmsMessage);
          sim800l.print(pendingSmsMessage); // Use print instead of println to avoid extra newline
          smsOpTimestamp = currentMillis;
          smsOpState = SMS_SEND_DATA_WAIT;
        } else {
          Serial.println(F("No SMS prompt received, retrying..."));
          // Retry the command
          sim800l.print("AT+CMGS=\"");
          sim800l.print(pendingSmsNumber);
          sim800l.println("\"");
          smsOpTimestamp = currentMillis;
        }
      }
      break;
      
    case SMS_SEND_DATA_WAIT:
      if (currentMillis - smsOpTimestamp >= SIM800L_SMS_SEND_DATA_DELAY) {
        sim800l.write(26);  // CTRL+Z
        smsOpTimestamp = currentMillis;
        smsOpState = SMS_SEND_END_WAIT;
      }
      break;
      
    case SMS_SEND_END_WAIT:
      if (currentMillis - smsOpTimestamp >= SIM800L_SMS_SEND_END_DELAY) {
        readSimResponse();
        // Check if SMS was sent successfully
        if (simResponseBuffer.indexOf("+CMGS:") != -1 || simResponseBuffer.indexOf("OK") != -1) {
          Serial.println(F("SMS sent successfully!"));
        } else {
          Serial.println(F("SMS send may have failed - no confirmation received"));
        }
        smsOpState = SMS_IDLE;
      }
      break;
  }
}

void startSmsRead(int messageIndex) {
  if (smsOpState != SMS_IDLE) return; // Operation in progress
  
  Serial.print(F("Reading SMS at index: ")); Serial.println(messageIndex);
  sim800l.print("AT+CMGR=");
  sim800l.println(messageIndex);
  pendingSmsIndex = messageIndex;
  smsOpTimestamp = millis();
  smsOpState = SMS_READ_WAIT;
}

void startSmsDelete(int messageIndex) {
  Serial.print(F("Deleting SMS at index: ")); Serial.println(messageIndex);
  sim800l.print("AT+CMGD=");
  sim800l.println(messageIndex);
  smsOpTimestamp = millis();
  smsOpState = SMS_DELETE_WAIT;
}

void startSmsSend(String message, String recipientNumber) {
  if (smsOpState != SMS_IDLE) return; // Operation in progress
  
  Serial.print(F("Setting phone number: ")); Serial.println(recipientNumber);
  sim800l.print("AT+CMGS=\"");
  sim800l.print(recipientNumber);
  sim800l.println("\"");
  pendingSmsMessage = message;
  pendingSmsNumber = recipientNumber;
  smsOpTimestamp = millis();
  smsOpState = SMS_SEND_CMD_WAIT;
}

// --- Legacy SMS functions (now use non-blocking operations) ---
void readSms(int messageIndex) {
  startSmsRead(messageIndex);
}

void deleteSms(int messageIndex) {
  startSmsDelete(messageIndex);
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

  // Wait for any ongoing SMS operation to complete
  unsigned long smsWaitStart = millis();
  while (smsOpState != SMS_IDLE && (millis() - smsWaitStart < 30000)) {
    updateSmsOperations();
  }
  
  if (smsOpState != SMS_IDLE) {
    Serial.println(F("SMS operation timeout - forcing reset"));
    smsOpState = SMS_IDLE;
  }
  
  startSmsSend(messageWithTimestamp, recipientNumber);
  
  // Wait for this SMS to complete before returning
  smsWaitStart = millis();
  while (smsOpState != SMS_IDLE && (millis() - smsWaitStart < 30000)) {
    updateSmsOperations();
  }
  
  if (smsOpState != SMS_IDLE) {
    Serial.println(F("SMS send timeout - forcing reset"));
    smsOpState = SMS_IDLE;
  }
}

// --- SMS Command Processing Functions ---
void extractSmsContentAndSender() {
  receivedSmsContent = "";
  senderNumber = "";
  
  Serial.println(F("=== Raw SMS Response ==="));
  Serial.println(simResponseBuffer);
  Serial.println(F("=== End Raw Response ==="));
  
  // Parse the SMS response to extract sender and content
  // Expected format: +CMGR: "REC UNREAD","+1234567890","","YY/MM/DD,HH:MM:SS+TZ"
  int cmgrIndex = simResponseBuffer.indexOf("+CMGR:");
  if (cmgrIndex != -1) {
    // Find the sender number between quotes
    int firstQuote = simResponseBuffer.indexOf('"', cmgrIndex);
    if (firstQuote != -1) {
      int secondQuote = simResponseBuffer.indexOf('"', firstQuote + 1);
      if (secondQuote != -1) {
        int thirdQuote = simResponseBuffer.indexOf('"', secondQuote + 1);
        if (thirdQuote != -1) {
          int fourthQuote = simResponseBuffer.indexOf('"', thirdQuote + 1);
          if (fourthQuote != -1) {
            senderNumber = simResponseBuffer.substring(thirdQuote + 1, fourthQuote);
          }
        }
      }
    }
    
    // Find the SMS content - it comes after the header line
    // Look for the content after +CMGR line
    int headerEnd = simResponseBuffer.indexOf('\n', cmgrIndex);
    if (headerEnd != -1) {
      // The actual SMS content starts after the header
      int contentStart = headerEnd + 1;
      int contentEnd = simResponseBuffer.indexOf("OK", contentStart);
      if (contentEnd == -1) {
        contentEnd = simResponseBuffer.length();
      }
      
      if (contentStart < simResponseBuffer.length()) {
        receivedSmsContent = simResponseBuffer.substring(contentStart, contentEnd);
        receivedSmsContent.trim();
        
        // Remove any trailing newlines or carriage returns
        while (receivedSmsContent.endsWith("\r") || receivedSmsContent.endsWith("\n")) {
          receivedSmsContent = receivedSmsContent.substring(0, receivedSmsContent.length() - 1);
        }
      }
    }
  }
  
  Serial.print(F("Extracted SMS from: '")); Serial.print(senderNumber); Serial.println(F("'"));
  Serial.print(F("Content: '")); Serial.print(receivedSmsContent); Serial.println(F("'"));
}

void updateSmsProcessing() {
  // Add timeout protection
  if (smsProcessingState != SMS_PROC_IDLE && 
      millis() - smsProcessingTimeout > 30000) { // 30 second timeout
    Serial.println(F("SMS processing timeout - resetting"));
    smsProcessingState = SMS_PROC_IDLE;
    return;
  }
  
  switch (smsProcessingState) {
    case SMS_PROC_PROCESSING:
      parseAndExecuteSmsCommand(receivedSmsContent, senderNumber);
      smsProcessingState = SMS_PROC_IDLE;
      break;
  }
}

void parseAndExecuteSmsCommand(String command, String sender) {
  command.trim();
  command.toUpperCase();
  
  Serial.print(F("Processing SMS command: '")); Serial.print(command); Serial.println(F("'"));
  Serial.print(F("From sender: '")); Serial.print(sender); Serial.println(F("'"));
  
  String response = "";
  bool validCommand = false;
  
  // NUMSETA command
  if (command.startsWith("NUMSETA ")) {
    String newNumber = command.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0) {
      savePhoneNumberToEEPROM(0, newNumber);
      response = "ID:" + customerID + " NUMSETA updated to " + newNumber;
      validCommand = true;
    }
  }
  // NUMSETB command
  else if (command.startsWith("NUMSETB ")) {
    String newNumber = command.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0) {
      savePhoneNumberToEEPROM(1, newNumber);
      response = "ID:" + customerID + " NUMSETB updated to " + newNumber;
      validCommand = true;
    }
  }
  // NUMSETC command
  else if (command.startsWith("NUMSETC ")) {
    String newNumber = command.substring(8);
    newNumber.trim();
    if (newNumber.length() > 0) {
      savePhoneNumberToEEPROM(2, newNumber);
      response = "ID:" + customerID + " NUMSETC updated to " + newNumber;
      validCommand = true;
    }
  }
  // SETID command
  else if (command.startsWith("SETID ")) {
    String newID = command.substring(6);
    newID.trim();
    if (newID.length() > 0 && newID.length() <= MAX_CUSTOMER_ID_LENGTH - 1) {
      saveCustomerIDToEEPROM(newID);
      response = "ID:" + customerID + " Customer ID updated to " + newID;
      validCommand = true;
    }
  }
  // STATUS command (get current configuration)
  else if (command == "STATUS") {
    response = "ID:" + customerID + " A:" + phoneNumbers[0] + " B:" + phoneNumbers[1] + " C:" + phoneNumbers[2];
    validCommand = true;
  }
  // TEST command (simple test)
  else if (command == "TEST") {
    response = "ID:" + customerID + " Test response - system working";
    validCommand = true;
  }
  
  // Send response if it was a valid command
  if (validCommand && response.length() > 0) {
    Serial.print(F("Sending command response: '")); Serial.print(response); Serial.println(F("'"));
    sendSMS(response, sender);
  } else if (command.length() > 0) {
    Serial.println(F("Invalid SMS command received"));
    sendSMS("ID:" + customerID + " Invalid command", sender);
  } else {
    Serial.println(F("Empty SMS command - no response sent"));
  }
}
