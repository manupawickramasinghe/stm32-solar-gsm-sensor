\# \*\*STM32 Low-Power Soil Moisture \& GSM Sensor\*\*



This document provides a complete guide to the hardware, software, and operation of the STM32-based low-power soil moisture sensor.



\## \*\*Project Overview\*\*



This project turns an STM32 "Blue Pill" board into a remote, battery-powered sensor node. Its primary function is to wake up periodically (e.g., once an hour), measure soil moisture, report the data to a Google Sheet via GPRS, send a status update via SMS, and then return to a deep sleep mode to conserve energy.



The system is designed for long-term, unattended deployment in agricultural or environmental monitoring scenarios.



\## \*\*Required Libraries \& Installation\*\*



This project depends on two key libraries from STMicroelectronics that are not always available in the Arduino Library Manager. You must install them manually from their GitHub repositories.



\### \*\*1\\. STM32RTC Library\*\*



\* \*\*Purpose:\*\* This library provides access to the STM32's built-in Real-Time Clock (RTC). The STM32LowPower library \*\*requires\*\* this to schedule wake-up alarms that pull the microcontroller out of deep sleep.  

\* \*\*GitHub Repository:\*\* \[https://github.com/stm32duino/STM32RTC](https://github.com/stm32duino/STM32RTC)



\### \*\*2\\. STM32LowPower Library\*\*



\* \*\*Purpose:\*\* This is the core library for power management. It allows you to put the STM32 into various low-power states, such as deepSleep(), which dramatically reduces energy consumption.  

\* \*\*GitHub Repository:\*\* \[https://github.com/stm32duino/STM32LowPower](https://github.com/stm32duino/STM32LowPower)



\### \*\*Installation Instructions (for both libraries)\*\*



1\. \*\*Download the Library:\*\*  

&nbsp;  \* Go to the GitHub repository page for the library.  

&nbsp;  \* Click the green \*\*\\< \\> Code\*\* button.  

&nbsp;  \* In the dropdown menu, click \*\*Download ZIP\*\*. This will save a file like STM32RTC-main.zip or STM32LowPower-main.zip to your computer.  

2\. \*\*Install in Arduino IDE:\*\*  

&nbsp;  \* Open the Arduino IDE.  

&nbsp;  \* Go to the menu \*\*Sketch \\> Include Library \\> Add .ZIP Library...\*\*.  

&nbsp;  \* Navigate to your Downloads folder and select the .zip file you just downloaded.  

&nbsp;  \* Click \*\*Open\*\*.



Repeat this process for both libraries.



\## \*\*Code Explanation\*\*



The firmware is designed around a single-shot, sleep-centric execution model. The device wakes up, runs the setup() function once, and then goes back into a deep sleep. The loop() function is intentionally empty.



\### \*\*Key Sections of the Code:\*\*



\* \*\*Configuration (\\#define and globals):\*\*  

&nbsp; \* DEBUG\\\_MODE: A critical flag. Set to 1 to enable Serial.print() messages for testing. Set to 0 for deployment to reduce the program size and save power.  

&nbsp; \* \*\*Pin Definitions:\*\* Sets which STM32 pins are used for the sensor and for controlling power to the GSM module.  

&nbsp; \* \*\*HardwareSerial:\*\* HardwareSerial Serial2(PA3, PA2); explicitly creates the Serial2 object for the SIM800L module, which is more robust than using software serial on an STM32.  

&nbsp; \* \*\*Network \& API:\*\* Stores the recipient phone numbers and the Google Apps Script URL for data upload.  

&nbsp; \* \*\*EEPROM Addresses:\*\* Defines where the last successful activation timestamp is stored in the microcontroller's non-volatile memory.  

\* \*\*setup() function:\*\*  

&nbsp; \* This acts as the main function every time the device wakes up from a reset or deep sleep.  

&nbsp; \* It initializes the LowPower library and configures the GPIO pins.  

&nbsp; \* It calls readLastActivation() to load the timestamp of the last successful run from EEPROM.  

&nbsp; \* It calls shouldPerformCycle() to decide if it's time to take a new reading.  

&nbsp; \* If the conditions are met, it calls performCycle() to do the main work.  

&nbsp; \* Finally, it calls enterSleepMode() to power down for the next cycle.  

\* \*\*shouldPerformCycle() function:\*\*  

&nbsp; \* This is the decision-making core.  

&nbsp; \* It briefly powers up the GSM module to get the current network time.  

&nbsp; \* It checks if the current time is within the first 5 minutes of a new hour.  

&nbsp; \* It compares the current hour and day to the timestamp stored in EEPROM to ensure it hasn't already performed a cycle for the current hour.  

&nbsp; \* It returns true (run the cycle) or false (go back to sleep).  

\* \*\*performCycle() function:\*\*  

&nbsp; \* Powers on the sensor and GSM module.  

&nbsp; \* Reads the analog value from the soil moisture sensor.  

&nbsp; \* Connects to the GPRS network.  

&nbsp; \* Formats a status message containing the timestamp, soil moisture percentage, and internet status.  

&nbsp; \* Sends the status message via SMS and uploads the data to the Google Sheet.  

&nbsp; \* Powers down the GSM module.  

\* \*\*enterSleepMode() function:\*\*  

&nbsp; \* Ensures all peripherals are powered down.  

&nbsp; \* Prints a final debug message if enabled.  

&nbsp; \* Calls LowPower.deepSleep(3600000); to put the STM32 into its lowest power state for 1 hour. The device will reset upon waking up, starting the setup() function again.  

\* \*\*EEPROM Functions (saveLastActivation, readLastActivation):\*\*  

&nbsp; \* These functions handle saving and retrieving the timestamp to the STM32's emulated EEPROM. This prevents the device from sending duplicate data if it unexpectedly resets.  

\* \*\*GSM Helper Functions (powerCycleGSM, connectToGPRS, sendSMS, sendGET, etc.):\*\*  

&nbsp; \* These are utility functions that manage the low-level AT command communication with the SIM800L module to perform tasks like connecting to the network, sending an SMS, or making an HTTP GET request.

