# STM32 Solar GSM Sensor System

A non-blocking, state-machine-based environmental monitoring system using STM32 Blue Pill with DHT11, DS18B20 sensors, and SIM800L GSM module for remote SMS notifications and configuration.

## 🌟 Features

- **Dual Temperature Sensors**: DHT11 (humidity + temperature) and DS18B20 (precision temperature)
- **SMS Notifications**: Automatic periodic sensor data reports via SMS
- **Remote Configuration**: Change phone numbers and customer ID via SMS commands
- **Non-blocking Architecture**: Fully asynchronous operation using state machines
- **EEPROM Storage**: Persistent configuration storage
- **Timestamp Integration**: GSM network time for accurate data logging
- **Multiple Recipients**: Send data to up to 3 phone numbers simultaneously

## 📱 SMS Commands

| Command | Description | Example |
|---------|-------------|---------|
| `STATUS` | Get current configuration | `STATUS` |
| `TEST` | Test system response | `TEST` |
| `NUMSETA +1234567890` | Update phone number A | `NUMSETA +94712345678` |
| `NUMSETB +1234567890` | Update phone number B | `NUMSETB +94712345678` |
| `NUMSETC +1234567890` | Update phone number C | `NUMSETC +94712345678` |
| `SETID CUSTOMER1` | Set customer ID (max 9 chars) | `SETID FARM01` |

### Example SMS Responses
```
Send: STATUS
Receive: 2025-08-02 14:30:15 - ID:00000 A:+94719593248 B:+94719751003 C:+94768378406

Send: NUMSETB +94716223703
Receive: 2025-08-02 14:30:25 - ID:00000 NUMSETB updated to +94716223703

Send: SETID FARM01
Receive: 2025-08-02 14:30:35 - ID:FARM01 Customer ID updated to FARM01
```

## 🔧 Hardware Configuration

### Pin Connections
```
STM32 Blue Pill Connections:
├── DHT11 Sensor
│   └── Data Pin: PB1
├── DS18B20 Sensor
│   └── Data Pin: PB0 (with 4.7kΩ pull-up resistor)
├── SIM800L GSM Module
│   ├── RX: PA3 (STM32 TX → SIM800L RX)
│   ├── TX: PA2 (STM32 RX ← SIM800L TX)
│   └── Baud Rate: 9600
└── Power: 3.3V/5V (depending on module requirements)
```

### Required Components
- STM32 Blue Pill (STM32F103C8T6)
- DHT11 Temperature & Humidity Sensor
- DS18B20 Digital Temperature Sensor
- 4.7kΩ resistor (for DS18B20 pull-up)
- SIM800L GSM/GPRS Module
- SIM card with SMS capability
- Power supply (suitable for GSM module)

## 📊 System Operation

### Automatic Data Collection
- **Reading Cycle**: Every 5 minutes (300,000ms)
- **SMS Threshold**: Sends SMS after 12 readings (60 minutes of data)
- **Data Averaging**: Calculates average values over the collection period
- **Auto Reset**: Counters and averages reset after SMS transmission

### Example Sensor Data SMS
```
2025-08-02 21:10:59 - ID:FARM01 60min Avg - DHT H:65.2% T:28.5C; DS18B20 T:27.8C
```

## 🏗️ Software Architecture

### State Machine Design
The system uses multiple state machines for non-blocking operation:

#### 1. Main Sensor State Machine
```
STATE_IDLE → STATE_WAIT_DHT → STATE_READ_DHT → 
STATE_WAIT_DS18B20 → STATE_READ_DS18B20 → 
STATE_PROCESS_DATA → STATE_IDLE
```

#### 2. GSM Initialization State Machine
```
GSM_INIT_START → GSM_INIT_BOOT_DELAY → 
GSM_INIT_ECHO_OFF_WAIT → GSM_INIT_AT_TEST → 
GSM_INIT_AT_TEST_WAIT → ... → GSM_INIT_COMPLETE
```

#### 3. SMS Operation State Machine
```
SMS_IDLE → SMS_READ_WAIT → SMS_DELETE_WAIT
SMS_IDLE → SMS_SEND_CMD_WAIT → SMS_SEND_DATA_WAIT → SMS_SEND_END_WAIT
```

### Key Functions

#### Core Loop Functions
- `loop()`: Main system loop with state machine coordination
- `updateGsmInitialization()`: Non-blocking GSM module setup
- `updateSmsOperations()`: Handles SMS read/send/delete operations
- `updateSmsProcessing()`: Processes incoming SMS commands
- `handleSim800lInput()`: Manages incoming GSM module data

#### SMS Command Processing
- `extractSmsContentAndSender()`: Parses SMS content and sender info
- `parseAndExecuteSmsCommand()`: Executes SMS commands and generates responses
- `sendSMS()`: Sends SMS with timestamp to specified number

#### Configuration Management
- `loadConfigFromEEPROM()`: Loads saved configuration on startup
- `savePhoneNumberToEEPROM()`: Saves phone numbers to EEPROM
- `saveCustomerIDToEEPROM()`: Saves customer ID to EEPROM

## 💾 EEPROM Memory Map

| Address | Size | Description |
|---------|------|-------------|
| 0 | 4 bytes | SMS Counter |
| 4-23 | 20 bytes | Phone Number A |
| 24-43 | 20 bytes | Phone Number B |
| 44-63 | 20 bytes | Phone Number C |
| 64-73 | 10 bytes | Customer ID |
| 74 | 1 byte | Initialization Flag |

## ⚙️ Configuration Parameters

### Timing Configuration
```cpp
#define MAIN_LOOP_CYCLE_DELAY 300000    // 5 minutes between readings
#define SMS_SEND_THRESHOLD 12           // Send SMS after 12 readings
#define DHT_READ_PRE_DELAY 2000         // 2 second DHT stabilization
#define DS18B20_READ_PRE_DELAY 100      // 100ms DS18B20 delay
```

### GSM Timeouts
```cpp
#define SIM800L_BOOTUP_DELAY 3000       // 3 second boot delay
#define SIM800L_SMS_SEND_END_DELAY 10000 // 10 second SMS send timeout
#define SIM800L_RESPONSE_READ_TIMEOUT 1000 // 1 second response timeout
```

### Default Configuration
```cpp
Phone Numbers: +94719593248, +94719751003, +94768378406
Customer ID: 00000
Baud Rates: Debug=9600, SIM800L=9600
```

## 🔄 Key Improvements Over Traditional Arduino Code

### 1. **Non-blocking Operation**
- No `delay()` calls that block execution
- Multiple operations can run simultaneously
- System remains responsive during long operations

### 2. **State Machine Architecture**
- Predictable state transitions
- Easy debugging and maintenance
- Robust error recovery

### 3. **Timeout Protection**
- 30-second timeouts prevent infinite loops
- Automatic recovery from stuck operations
- Circuit breaker pattern implementation

### 4. **Memory Management**
- Efficient EEPROM usage
- Persistent configuration storage
- String buffer management

## 🚀 Getting Started

### 1. Hardware Setup
1. Connect sensors and GSM module as per pin configuration
2. Insert activated SIM card into SIM800L module
3. Ensure adequate power supply for GSM module

### 2. Software Upload
1. Install required libraries:
   - DHT sensor library
   - OneWire library
   - DallasTemperature library
2. Configure pin assignments if different from defaults
3. Upload code to STM32 Blue Pill

### 3. Initial Configuration
1. Monitor serial output for system initialization
2. Send `STATUS` SMS to check current configuration
3. Update phone numbers using `NUMSETA/B/C` commands
4. Set customer ID using `SETID` command

### 4. Operation Verification
1. Send `TEST` SMS to verify system response
2. Wait for automatic sensor data SMS (60 minutes)
3. Monitor serial output for debugging information

## 🐛 Troubleshooting

### Common Issues

#### SMS Not Received
- Check SIM card balance and SMS capability
- Verify GSM network registration
- Check antenna connection
- Monitor serial output for GSM initialization errors

#### Sensor Reading Issues
- Verify sensor connections and power
- Check pull-up resistor for DS18B20 (4.7kΩ)
- Monitor serial output for sensor readings

#### SMS Commands Not Working
- Ensure exact command format (case-insensitive)
- Check SMS content extraction in serial monitor
- Verify sender number format

### Debug Output
The system provides comprehensive debug information:
```
SIM_RECV_URC: [GSM module responses]
=== Raw SMS Response === [SMS content parsing]
Extracted SMS from: [sender info]
Processing SMS command: [command processing]
```

## 📈 Performance Characteristics

- **Response Time**: SMS commands processed within 10-15 seconds
- **Data Accuracy**: ±0.5°C temperature, ±2% humidity
- **Power Consumption**: Optimized for battery operation
- **Memory Usage**: ~75% of STM32F103C8T6 flash memory
- **Reliability**: 30-second timeout protection on all operations

## 🔮 Future Enhancements

- Solar panel voltage monitoring
- Battery level reporting
- Data logging to SD card
- Web dashboard integration
- Multiple sensor support
- GPS location tracking

## 📄 License

This project is open source. Feel free to modify and distribute according to your needs.

---

**Note**: This system is designed for agricultural and environmental monitoring applications where reliable, remote data collection is essential.