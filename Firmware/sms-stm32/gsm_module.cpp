#include "gsm_module.h"
#include "config.h"

// Timing variables for non-blocking operations
unsigned long gsmCommandStartTime = 0;
unsigned long gsmResponseStartTime = 0;
bool waitingForGsmResponse = false;
String pendingGsmCommand = "";

bool readSimResponseNonBlocking(unsigned long timeout) {
  if (!waitingForGsmResponse) {
    gsmResponseStartTime = millis();
    waitingForGsmResponse = true;
    simResponseBuffer = "";
  }
  
  while (sim800l.available()) {
    char c = sim800l.read();
    Serial.write(c);
    simResponseBuffer += c;
  }
  
  if (millis() - gsmResponseStartTime >= timeout) {
    waitingForGsmResponse = false;
    return true;
  }
  
  return false;
}

bool sendGsmCommandNonBlocking(String command, unsigned long delayTime) {
  if (pendingGsmCommand != command) {
    pendingGsmCommand = command;
    gsmCommandStartTime = millis();
    sim800l.println(command);
    return false;
  }
  
  if (millis() - gsmCommandStartTime >= delayTime) {
    pendingGsmCommand = "";
    return true;
  }
  
  return false;
}

String getGsmTimestamp() {
  if (!gsmIsInitializedAndReady) {
    Serial.println(F("GSM not ready, cannot get timestamp."));
    return "TS_ERR";
  }
  
  static bool timestampRequested = false;
  static unsigned long timestampStartTime = 0;
  
  if (!timestampRequested) {
    Serial.println(F("Attempting to get GSM timestamp..."));
    sim800l.println("AT+CCLK?");
    timestampStartTime = millis();
    timestampRequested = true;
    return "PENDING";
  }
  
  if (millis() - timestampStartTime < SIM800L_TIMESTAMP_READ_DELAY) {
    return "PENDING";
  }
  
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

bool initializeAndConfigureGsmModule() {
  static int initStep = 0;
  static unsigned long stepStartTime = 0;
  static bool stepInProgress = false;
  
  if (!stepInProgress) {
    stepStartTime = millis();
    stepInProgress = true;
  }
  
  switch (initStep) {
    case 0:
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
      
    case 1:
      if (sendGsmCommandNonBlocking("ATE0", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 2:
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
      
    case 3:
      if (sendGsmCommandNonBlocking("AT+CPMS=\"SM\",\"SM\",\"SM\"", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 4:
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
      
    case 5:
      if (sendGsmCommandNonBlocking("AT+CNMI=2,1,0,0,0", SIM800L_GENERIC_CMD_DELAY)) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
          initStep++;
          stepInProgress = false;
        }
      }
      break;
      
    case 6:
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
  
  return false;
}

void readSimResponse() {
  readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT);
}
