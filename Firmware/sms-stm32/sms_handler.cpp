#include "sms_handler.h"
#include "gsm_module.h"
#include "eeprom_utils.h"

// SMS operation state variables
SmsOperationState smsOpState = SMS_IDLE;
unsigned long smsOperationStartTime = 0;
int pendingSmsIndex = 0;
String pendingSmsMessage = "";
String pendingSmsRecipient = "";

// External variables
extern HardwareSerial sim800l;
extern String simResponseBuffer;
extern bool gsmIsInitializedAndReady;
extern String phoneNumbers[];
extern String customerID;

void handleSim800lInput() {
  if (!gsmIsInitializedAndReady) return;

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

void handleSmsOperations() {
  if (smsOpState == SMS_IDLE) return;
  
  switch (smsOpState) {
    case SMS_READING:
      if (millis() - smsOperationStartTime >= SIM800L_SMS_READ_DELETE_DELAY) {
        if (readSimResponseNonBlocking(SIM800L_RESPONSE_READ_TIMEOUT)) {
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

void readSms(int messageIndex) {
  if (!gsmIsInitializedAndReady) return;
  if (smsOpState != SMS_IDLE) return;

  Serial.print(F("Reading SMS at index: ")); Serial.println(messageIndex);
  pendingSmsIndex = messageIndex;
  sim800l.print("AT+CMGR=");
  sim800l.println(messageIndex);
  smsOpState = SMS_READING;
  smsOperationStartTime = millis();
}

void deleteSms(int messageIndex) {
  // Function handled by handleSmsOperations() state machine
}

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
