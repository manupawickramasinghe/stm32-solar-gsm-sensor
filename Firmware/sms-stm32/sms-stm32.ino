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
#define SMS_SEND_THRESHOLD 12 // Send SMS when counter reaches this value

// Array of phone numbers to send SMS to
String phoneNumbers[] = {"+94719593248", "+94719751003", "+94768378406"};
const int NUM_PHONE_NUMBERS = 3;

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
            startSmsRead(messageIndex);
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
        Serial.print(F("Sending full message: ")); Serial.println(pendingSmsMessage);
        sim800l.println(pendingSmsMessage);
        smsOpTimestamp = currentMillis;
        smsOpState = SMS_SEND_DATA_WAIT;
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
        Serial.println(F("SMS send attempt finished."));
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
  while (smsOpState != SMS_IDLE) {
    updateSmsOperations();
  }
  
  startSmsSend(messageWithTimestamp, recipientNumber);
  
  // Wait for this SMS to complete before returning
  while (smsOpState != SMS_IDLE) {
    updateSmsOperations();
  }
}
