# Notes on setting STM32 up

## Code Structure

The SMS-STM32 project has been modularized into the following files for better organization and maintainability:

- `sms-stm32.ino` - Main Arduino sketch file with setup(), loop(), and global variable declarations
- `config.h` - Configuration defines, constants, enums, and EEPROM addresses
- `gsm_module.h/.cpp` - GSM module function declarations and implementation (non-blocking AT commands)
- `sensor_manager.h/.cpp` - Sensor management declarations and implementation (DHT, DS18B20 reading and averaging)
- `eeprom_utils.h/.cpp` - EEPROM utility function declarations and implementation 
- `sms_handler.h/.cpp` - SMS handling declarations and implementation (parsing, sending, receiving)

## Global Variables

The main .ino file declares global variables that are shared across modules using `extern` declarations in header files:
- `phoneNumbers[]` - Array of recipient phone numbers
- `customerID` - Device identifier string
- `sim800l` - Hardware serial instance for GSM communication
- `simResponseBuffer` - Buffer for GSM module responses
- `gsmIsInitializedAndReady` - GSM module status flag
- `currentState` - Main state machine state
- Timing variables for non-blocking operations

## Compilation

Make sure all .cpp and .h files are in the same directory as the .ino file for Arduino IDE to compile them together. The modular structure helps avoid multiple definition errors by keeping function implementations in their respective .cpp files while declaring them in header files.

## Module Dependencies

- All modules depend on `config.h` for constants and type definitions
- GSM and SMS modules share the `sim800l` serial instance and response buffer
- Sensor module uses state variables for averaging and timing
- EEPROM module provides utilities for persistent storage of configuration

