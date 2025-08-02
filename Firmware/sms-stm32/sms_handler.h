#ifndef SMS_HANDLER_H
#define SMS_HANDLER_H

#include <Arduino.h>
#include "config.h"

extern SmsOperationState smsOpState;
extern unsigned long smsOperationStartTime;
extern int pendingSmsIndex;
extern String pendingSmsMessage;
extern String pendingSmsRecipient;

void handleSim800lInput();
void handleSmsOperations();
void parseSmsCommands();
void readSms(int messageIndex);
void deleteSms(int messageIndex);
void sendSMS(String message, String recipientNumber);

#endif
