#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "config.h"
#include "gsm_module.h"
#include "eeprom_utils.h"
#include "sms_handler.h"
#include "sensor_manager.h"

// Global variables shared across modules
String phoneNumbers[] = {"+94719593248", "+94719751003", "+94768378406"};
String customerID = "00000";
HardwareSerial sim800l(GSM_RX_PIN, GSM_TX_PIN);
String simResponseBuffer = "";
bool gsmIsInitializedAndReady = false;
int smsCounter = 0;
SystemState currentState = STATE_IDLE;

// Timing variables
unsigned long lastStateChange = 0;
unsigned long lastMainLoopTime = 0;

void setup() {
  Serial.begin(DEBUG_SERIAL_BAUDRATE);
  while (!Serial) { delay(SERIAL_INIT_WAIT_DELAY); }
  Serial.println(F("DHT, DS18B20 Sensor Test with GSM SMS"));

  loadConfigFromEEPROM();
  initializeSensors();
  
  Serial.println(F("------------------------------------"));
  Serial.println(F("Setup complete. Starting main loop..."));
}

void loop() {
  handleSim800lInput();
  handleSmsOperations();

  if (!gsmIsInitializedAndReady) {
    static unsigned long lastGsmInitAttempt = 0;
    if (millis() - lastGsmInitAttempt >= GSM_INIT_FAILURE_RETRY_DELAY) {
      gsmIsInitializedAndReady = initializeAndConfigureGsmModule();
      if (!gsmIsInitializedAndReady) {
        lastGsmInitAttempt = millis();
      }
    }
    return;
  }

  handleSensorStateMachine();
}
    sim800l.println("AT+CCLK?");
    timestampStartTime = millis();
    timestampRequested = true;
    return "PENDING";
  }
  
  if (millis() - timestampStartTime < SIM800L_TIMESTAMP_READ_DELAY) {
    return "PENDING";
  }
  
  // Read response
  simResponseBuffer = "";
  unsigned long responseStart = millis();
  while (millis() - responseStart < SIM800L_RESPONSE_READ_TIMEOUT) {
    while (sim800l.available()) {
      char c = sim800l.read();
      Serial.write(c);
      simResponseBuffer += c;
    }
  }
  
  timestampRequested = false;
  
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
  static int initStep = 0;
  static unsigned long stepStartTime = 0;
  static bool stepInProgress = false;
  
  if (!stepInProgress) {
    stepStartTime = millis();
    stepInProgress = true;
  }
  
  switch (initStep) {
    case 0: // Begin and bootup delay
      if (initStep == 0 && millis() - stepStartTime == 0) {
        Serial.println(F("Attempting to initialize SIM800L GSM Module..."));
        sim800l.begin(SIM800L_BAUDRATE);
      }
      if (millis() - stepStartTime >= SIM800L_BOOTUP_DELAY) {
        Serial.println(F("Configuring SIM800L..."));
        initStep++;
        stepInProgress = false;
      }
      break;
      
    case 1: // ATE0
      if (sendGsmCommandNonBlocking("ATE0", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 2: // AT handshake
      if (sendGsmCommandNonBlocking("AT", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          if (simResponseBuffer.indexOf("OK") == -1) {
            Serial.println(F("GSM Init Error: AT command failed."));
            initStep = 0;
            stepInProgress = false;
            return false;
          }
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 3: // CPMS
      if (sendGsmCommandNonBlocking("AT+CPMS=\"SM\",\"SM\",\"SM\"", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 4: // CMGF
      if (sendGsmCommandNonBlocking("AT+CMGF=1", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          if (simResponseBuffer.indexOf("OK") == -1) {
            Serial.println(F("GSM Init Error: AT+CMGF=1 command failed."));
            initStep = 0;
            stepInProgress = false;
            return false;
          }
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 5: // CNMI
      if (sendGsmCommandNonBlocking("AT+CNMI=2,1,0,0,0", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 6: // CREG
      if (sendGsmCommandNonBlocking("AT+CREG?", SIM800L_CREG_RESPONSE_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          if (simResponseBuffer.indexOf("+CREG: 0,1") == -1 && simResponseBuffer.indexOf("+CREG: 0,5") == -1) {
            Serial.println(F("GSM Warning: Not registered on network yet."));
          }
          Serial.println(F("GSM Module Initialized and Configured successfully."));
          initStep = 0;
          stepInProgress = false;
          return true;
        }
      }
      break;
  }
  
  return false; // Still initializing
}

// --- EEPROM String Read/Write Helpers ---
void eepromWriteString(int addr, String data, int maxLen) {
    for (int i = 0; i < maxLen; i++) {
        char c = (i < data.length()) ? data[i] : 0;
        EEPROM.write(addr + i, c);
    }
}

String eepromReadString(int addr, int maxLen) {
    String result = "";
    for (int i = 0; i < maxLen; i++) {
        char c = EEPROM.read(addr + i);
        if (c == 0) break;
        result += c;
    }
    return result;
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
  String numA = eepromReadString(EEPROM_NUMSETA_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
  String numB = eepromReadString(EEPROM_NUMSETB_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
  String numC = eepromReadString(EEPROM_NUMSETC_ADDR, EEPROM_PHONE_NUMBER_MAX_LEN);
  String custID = eepromReadString(EEPROM_CUSTOMER_ID_ADDR, EEPROM_CUSTOMER_ID_MAX_LEN);

  if (numA.length() > 0) phoneNumbers[0] = numA;
  if (numB.length() > 0) phoneNumbers[1] = numB;
  if (numC.length() > 0) phoneNumbers[2] = numC;
  if (custID.length() > 0) customerID = custID;

  Serial.print(F("Phone Numbers: "));
  Serial.print(phoneNumbers[0]); Serial.print(", ");
  Serial.print(phoneNumbers[1]); Serial.print(", ");
  Serial.println(phoneNumbers[2]);
  Serial.print(F("Customer ID: ")); Serial.println(customerID);
  
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
    
    // Handle SMS operations non-blockingly
    handleSmsOperations();

    // Try to initialize GSM if not ready
    if (!gsmIsInitializedAndReady) {
        static unsigned long lastGsmInitAttempt = 0;
        if (millis() - lastGsmInitAttempt >= GSM_INIT_FAILURE_RETRY_DELAY) {
            gsmIsInitializedAndReady = initializeAndConfigureGsmModule();
            if (!gsmIsInitializedAndReady) {
                lastGsmInitAttempt = millis();
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


// --- Handle SMS Operations Non-blockingly ---
void handleSmsOperations() {
  if (smsOpState == SMS_IDLE) return;
  
  switch (smsOpState) {
    case SMS_READING:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_READ_DELETE_DELAY) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          // Parse SMS content for commands (existing parsing code)
          parseSmsCommands();
          smsOpState = SMS_DELETING;
          smsOperationStartTime = millis();
          sim800l.print("AT+CMGD=");
          sim800l.println(pendingSmsIndex);
        }
      }
      break;
      
    case SMS_DELETING:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_READ_DELETE_DELAY) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          Serial.print(F("SMS at index ")); Serial.print(pendingSmsIndex); Serial.println(F(" deleted"));
          smsOpState = SMS_IDLE;
        }
      }
      break;
      
    case SMS_SENDING_NUMBER:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_SEND_CMD_DELAY) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          sim800l.println(pendingSmsMessage);
          smsOpState = SMS_SENDING_MESSAGE;
          smsOperationStartTime = millis();
        }
      }
      break;
      
    case SMS_SENDING_MESSAGE:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_SEND_DATA_DELAY) {
        sim800l.write(26);
        smsOpState = SMS_SENDING_END;
        smsOperationStartTime = millis();
      }
      break;
      
    case SMS_SENDING_END:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_SEND_END_DELAY) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          Serial.println(F("SMS send completed"));
          smsOpState = SMS_IDLE;
        }
      }
      break;
  }
}

// --- Parse SMS Commands ---
void parseSmsCommands() {
  int msgStart = simResponseBuffer.indexOf("\n");
  if (msgStart != -1) {
    String smsContent = simResponseBuffer.substring(msgStart + 1);
    smsContent.trim();

    if (smsContent.startsWith("NUMSETA")) {
      String newNum = smsContent.substring(7);
      newNum.trim();
      if (newNum.length() > 0 && newNum.length() < EEPROM_PHONE_NUMBER_MAX_LEN) {
        phoneNumbers[0] = newNum;
        eepromWriteString(EEPROM_NUMSETA_ADDR, newNum, EEPROM_PHONE_NUMBER_MAX_LEN);
        Serial.print(F("Phone Number A updated: ")); Serial.println(newNum);
      }
    }
    else if (smsContent.startsWith("NUMSETB")) {
      String newNum = smsContent.substring(7);
      newNum.trim();
      if (newNum.length() > 0 && newNum.length() < EEPROM_PHONE_NUMBER_MAX_LEN) {
        phoneNumbers[1] = newNum;
        eepromWriteString(EEPROM_NUMSETB_ADDR, newNum, EEPROM_PHONE_NUMBER_MAX_LEN);
        Serial.print(F("Phone Number B updated: ")); Serial.println(newNum);
      }
    }
    else if (smsContent.startsWith("NUMSETC")) {
      String newNum = smsContent.substring(7);
      newNum.trim();
      if (newNum.length() > 0 && newNum.length() < EEPROM_PHONE_NUMBER_MAX_LEN) {
        phoneNumbers[2] = newNum;
        eepromWriteString(EEPROM_NUMSETC_ADDR, newNum, EEPROM_PHONE_NUMBER_MAX_LEN);
        Serial.print(F("Phone Number C updated: ")); Serial.println(newNum);
      }
    }
    else if (smsContent.startsWith("SETID")) {
      String newID = smsContent.substring(5);
      newID.trim();
      if (newID.length() > 0 && newID.length() < EEPROM_CUSTOMER_ID_MAX_LEN) {
        customerID = newID;
        eepromWriteString(EEPROM_CUSTOMER_ID_ADDR, newID, EEPROM_CUSTOMER_ID_MAX_LEN);
        Serial.print(F("Customer ID updated: ")); Serial.println(newID);
      }
    }
  }
}

// --- Read a specific SMS message (Non-blocking) ---
void readSms(int messageIndex) {
  if (!gsmIsInitializedAndReady) return;
  if (smsOpState != SMS_IDLE) return; // Busy with another SMS operation

  Serial.print(F("Reading SMS at index: ")); Serial.println(messageIndex);
  pendingSmsIndex = messageIndex;
  sim800l.print("AT+CMGR=");
  sim800l.println(messageIndex);
  smsOpState = SMS_READING;
  smsOperationStartTime = millis();
}

// --- Helper function to send SMS with Timestamp and Customer ID (Non-blocking) ---
void sendSMS(String message, String recipientNumber) {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("Cannot send SMS, GSM module not ready."));
    return;
  }
  
  if (smsOpState != SMS_IDLE) {
    Serial.println(F("SMS operation in progress, skipping send"));
    return;
  }

  String timestamp = getGsmTimestamp();
  if (timestamp == "PENDING") {
    // Try again later
    return;
  }
  
  String messageWithTimestamp;
  if (timestamp == "TS_ERR" || timestamp.length() == 0) {
    Serial.println(F("Failed to get GSM timestamp. Sending SMS with placeholder."));
    messageWithTimestamp = "No TS - " + message;
  } else {
    messageWithTimestamp = timestamp + " - " + message;
  }

  messageWithTimestamp = "ID:" + customerID + " - " + messageWithTimestamp;

  if (messageWithTimestamp.length() > 160) {
    Serial.print(F("Warning: SMS (len ")); Serial.print(messageWithTimestamp.length()); Serial.println(F(") is long, may be truncated or split."));
  }

  Serial.print(F("Setting phone number: ")); Serial.println(recipientNumber);
  pendingSmsMessage = messageWithTimestamp;
  pendingSmsRecipient = recipientNumber;
  
  sim800l.print("AT+CMGS=\"");
  sim800l.print(recipientNumber);
  sim800l.println("\"");
  
  smsOpState = SMS_SENDING_NUMBER;
  smsOperationStartTime = millis();
}

// --- Sensor State Machine Handler ---
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
