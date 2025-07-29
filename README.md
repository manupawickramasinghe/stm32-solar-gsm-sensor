# stm32-solar-gsm-sensor
Firmware and hardware guide for an open-source, solar-powered agricultural sensor system based on the STM32 "Blue Pill" and a SIM800L GSM module.

This project is designed for remote environmental monitoring. It periodically reads data from temperature and humidity sensors, aggregates the data, and sends a summary report via SMS to a predefined list of phone numbers. It is optimized for low-power operation, making it suitable for solar or battery-powered deployments.

## Features
- **Dual Temperature Sensing**: Reads ambient temperature and humidity from a DHT11 sensor and precise temperature from a waterproof DS18B20 sensor.

- **GSM Connectivity**: Uses a SIM800L module to connect to the cellular network for sending and receiving SMS messages.

- **Periodic Data Reporting**: The device wakes up every 5 minutes to take a sensor reading.

- **Averaged Data SMS**: After a set number of readings (e.g., 12 readings over 60 minutes), it calculates the average sensor values and sends a consolidated report via SMS.

- **Multi-Recipient SMS**: Sends the report to a configurable list of multiple phone numbers.

- **Network Timestamp**: Fetches the current date and time from the cellular network and includes it in every SMS report for accurate logging.

- **Persistent Counter**: Uses Flash memory emulation (since the STM32F103C8T6 does not have built-in EEPROM) to save its reading counter, ensuring it can resume its cycle even after a power loss or reset.

- **Remote Control (Basic)**: Capable of receiving and processing incoming SMS messages.

## Hardware Requirements
- **Microcontroller**: STM32F103C8T6 "Blue Pill" board.

- **GSM Module**: SIM800L module (ensure it comes with a spring antenna and a valid 2G SIM card).

- **Sensors**:

  - DHT11 Temperature and Humidity Sensor.

  - DS18B20 Waterproof Temperature Sensor.

  - A 4.7kΩ pull-up resistor for the DS18B20 data line.

- **Programmer**: FTDI FT232RL USB-to-Serial adapter for flashing the firmware.

- **Power Supply**:

  - A stable power source for the SIM800L is critical. A 3.7V LiPo battery or a dedicated buck converter capable of supplying at least 2A peak current is highly recommended. Powering the SIM800L directly from the Blue Pill's 3.3V pin will cause instability.

- **Miscellaneous**: Jumper wires, breadboard.

## Software & Setup (Arduino IDE)
This project is built using the Arduino IDE with the official STM32 core.

1. **Install Arduino IDE**
   If you don't have it, download and install the latest version of the Arduino IDE.

2. **Install STM32 Board Support**
   Open the Arduino IDE.

   Go to `File > Preferences`.

   In the "Additional boards manager URLs" field, paste the following URL:

   ```
   https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
   ```

   Go to `Tools > Board > Boards Manager...`.

   Search for "STM32 MCU based boards" and install the package by STMicroelectronics.

3. **Install Required Libraries**
   You need to install three libraries for the sensors.

   Go to `Tools > Manage Libraries...`.

   Search for and install each of the following libraries:

   - DHT sensor library by Adafruit

   - OneWire by Paul Stoffregen

   - DallasTemperature by Miles Burton

4. **Configure the Code**
   Open the `.ino` sketch file in the Arduino IDE.

   Modify the `phoneNumbers` array to include the recipient phone numbers you want to send the SMS reports to. Make sure to use the international format (e.g., +94...).

   ```cpp
   String phoneNumbers[] = {"+94719593248", "+94719751003"};
   ```

## Flashing the Firmware (Manual Upload)
Since this project uses an FTDI adapter, you cannot use the standard "Upload" button in the Arduino IDE. You must compile the binary and flash it manually using a command-line tool.

### Step 1: Compile and Export the Binary
In the Arduino IDE, select the correct board settings:

- `Tools > Board: "STM32 MCU based boards" > "Generic STM32F1 series"`

- `Tools > Board part number: "Bluepill F103C8"`

- `Tools > Upload method: "STM32CubeProgrammer (Serial)"`

Go to `Sketch > Export compiled Binary`. This will compile your code and create a `.hex` and `.bin` file in the sketch folder.

### Step 2: Install stm32flash
This is a lightweight, reliable tool for flashing STM32 devices via UART.

Open a terminal on your Linux machine and run:

```bash
sudo apt update
sudo apt install stm32flash
```

### Step 3: Set Up Hardware for Flashing
This is the most critical part. You must put the STM32 into its hardware bootloader mode.

- **Set Jumpers**:

  - Set the `BOOT0` jumper to the '1' position (HIGH).

  - Set the `BOOT1` jumper to the '0' position (LOW).

- **Wire the FTDI Adapter**:

  - FTDI GND -> STM32 GND

  - FTDI TX -> STM32 PA10 (RX1)

  - FTDI RX -> STM32 PA9 (TX1)

- **Connect and Reset**: Connect the FTDI adapter to your computer. Now, press the RESET button on the Blue Pill board. The board is now waiting for programming commands.

### Step 4: Fix Serial Port Permissions (One-Time Setup)
On Linux, your user needs permission to access serial devices.

Add your user to the dialout group:

```bash
sudo usermod -a -G dialout $USER
```

IMPORTANT: For this change to take effect, you must log out and log back in or reboot your computer.

### Step 5: Flash the Firmware
Open a terminal and navigate to your Arduino sketch directory where the `.bin` file was exported.

Run the `stm32flash` command:

```bash
stm32flash -b 115200 -w YourSketchName.ino.bin -v -g 0x0 /dev/ttyUSB0
```

- `-b 115200`: Sets the baud rate for reliable communication.

- `-w YourSketchName.ino.bin`: Specifies the binary file to write.

- `-v`: Verifies the flash after writing.

- `-g 0x0`: Starts execution from the beginning of the flash memory after programming.

- `/dev/ttyUSB0`: The serial port of your FTDI adapter.

### Step 6: Run the Application
Disconnect the power.

Set the `BOOT0` jumper back to the '0' position.

Reconnect power or press the RESET button.

The board will now boot into your application. You can open the Arduino IDE's Serial Monitor at 9600 baud to see the debug output.

## Code Explanation
- **Configuration (`#define`)**: All hardware pins, timing delays, and operational thresholds are defined at the top of the file for easy configuration.

- **setup()**: This function runs once on startup. It initializes the serial ports for debugging, starts the DHT and DS18B20 sensors, and crucially, initializes the SIM800L module with a series of AT commands to prepare it for sending SMS. It also reads the last known `smsCounter` value from the EEPROM.

- **loop()**: This is the main operational cycle.

  - It first ensures the GSM module is initialized and ready.

  - It reads the current temperature and humidity from both sensors.

  - The readings are added to summing variables (`dhtHumSum`, `dhtTempSum`, etc.).

  - A counter (`smsCounter`) is incremented and saved to the EEPROM.

  - It checks if `smsCounter` has reached the `SMS_SEND_THRESHOLD` (e.g., 12).

  - If the threshold is met, it calculates the averages, fetches a network timestamp, formats the SMS message, and sends it to all phone numbers in the `phoneNumbers` array.

  - Finally, it resets the summing variables and the `smsCounter` back to zero and waits for 5 minutes (`MAIN_LOOP_CYCLE_DELAY`) before starting the next cycle.

- **GSM Helper Functions**: Functions like `sendSMS()`, `getGsmTimestamp()`, and `handleSim800lInput()` abstract the complexity of communicating with the SIM800L module.
