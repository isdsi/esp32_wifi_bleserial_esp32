
# This project is forked from beegee1962/esp32_wifi_ble_esp32.
Link : https://bitbucket.org/beegee1962/esp32_wifi_ble_esp32

Uses BLE Serial (avinabmalla/ESP32_BleSerial) instead of Bluetooth Serial
Link : https://github.com/avinabmalla/ESP32_BleSerial

# Installation
Add BleSerial.cpp, BleSerial.h, ByteRingBuffer.h to the project.

# Function
The WiFi settings of esp32 are implemented using serial communication using BLE.
Before serial transmission, the Bluetooth Mac address is encrypted by XorCoding as a key and transmitted.

# Test board
Tested on esp32-s3, this model supports Bluetooth Low Energy but does not support Bluetooth Classic. Bluetooth Serial Port Profile (SPP) in Bluetooth Classic cannot be used. Serial communication is used using the Notify function among the characteristics of BLE GATT. The maximum packet size is 509.

# Future features
Use JSON to iterate over the settings of esp32 in Android and use the description of the settings. The description uses the type of the settings (boolean, integer, decimal, string, optional) and the limit value (maximum, minimum, default, read-only). In the case of optional, the selection item is set as a string. You can know how many settings there are at the stage of iterating over the settings. After the iteration, according to the description of the settings, the Android app is dynamically generated based on what the ESP32 responds to on the screen, such as switches, edittext, combo boxes, spinners, etc.


# This project is outdated and no longer supported. Please check out my new code on [Github](https://github.com/beegee-tokyo/RAK4631-LoRa-BLE-Config)

# ESP32 WiFi credential setup over BLE
Setup your ESP32 WiFi credentials over BLE from an Android phone or tablet.
Sometimes you do not want to have your WiFi credentials in the source code, specially if it is open source and maybe accessible as a repository on Github or Bitbucket.

There are already solution like [WiFiManager-ESP32](https://github.com/zhouhan0126/WIFIMANAGER-ESP32) that give you the possibility to setup the WiFi credentials over a captive portal.    
But I wanted to test the possibility to setup the ESP32's WiFi over Bluetooth Low Energy.    
This repository covers the source code for the ESP32. The source code for the Android application are in the [ESP32_WiFi_BLE_Android](https://bitbucket.org/beegee1962/esp32_wifi_ble_android) repository.    

Detailed informations about this project are on my [website](https://desire.giesecke.tk/index.php/2018/04/06/esp32-wifi-setup-over-ble/) 

## Development platform
PlatformIO, but as the whole code is in a single file it can be easily copied into a .ino file and used with the Arduino IDE

## Used hardware
- [Elecrow ESP32 WIFI BLE BOARD WROOM](https://circuit.rocks/esp32-wifi-ble-board-wroom.html?search=ESP32)		
- Any Android phone or tablet that is capable of BLE.		

## SW practices used
- Use of BLE for sending and receiving data

## Library dependencies		
PlatformIO library ID - Library name / Github link    
- ID64 [ArduinoJson by Benoit Blanchon](https://github.com/bblanchon/ArduinoJson)		
