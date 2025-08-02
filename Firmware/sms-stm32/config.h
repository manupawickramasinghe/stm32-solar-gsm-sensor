#ifndef CONFIG_H
#define CONFIG_H

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
#define EEPROM_NUMSETA_ADDR 10 // EEPROM address for phone number A
#define EEPROM_NUMSETB_ADDR 30 // EEPROM address for phone number B
#define EEPROM_NUMSETC_ADDR 50 // EEPROM address for phone number C
#define EEPROM_CUSTOMER_ID_ADDR 70 // EEPROM address for customer ID
#define EEPROM_PHONE_NUMBER_MAX_LEN 18 // Max length for phone number string (+country code)
#define EEPROM_CUSTOMER_ID_MAX_LEN 8 // Max length for customer ID string

#define SMS_SEND_THRESHOLD 12 // Send SMS when counter reaches this value
#define NUM_PHONE_NUMBERS 3

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

enum SmsOperationState { 
    SMS_IDLE, 
    SMS_READING, 
    SMS_DELETING, 
    SMS_SENDING_NUMBER, 
    SMS_SENDING_MESSAGE, 
    SMS_SENDING_END 
};

#endif
