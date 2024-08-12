// Default Arduino includes
#include <Arduino.h>
#include <WiFi.h>
#include <nvs.h>
#include <nvs_flash.h>

// Includes for JSON object handling
// Requires ArduinoJson library
// https://arduinojson.org
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

#include <Preferences.h>
#include "BleSerial.h"

/** Build time */
const char compileDate[] = __DATE__ " " __TIME__;

bool usePrimAP = true;
/** Flag if stored AP credentials are available */
bool hasCredentials = false;
/** Connection status */
volatile bool isConnected = false;
/** Connection change status */
bool connStatusChanged = false;

/** SSIDs of local WiFi networks */
String ssidPrim;
String ssidSec;
/** Password for local WiFi network */
String pwPrim;
String pwSec;

/** Buffer for JSON string */
// MAx size is 51 bytes for frame: 
// {"ssidPrim":"","pwPrim":"","ssidSec":"","pwSec":""}
// + 4 x 32 bytes for 2 SSID's and 2 passwords
StaticJsonBuffer<200> jsonBuffer;

/** Callback for receiving IP address from AP */
void gotIP(arduino_event_id_t event) {
	isConnected = true;
	connStatusChanged = true;
}

/** Callback for connection loss */
void lostCon(arduino_event_id_t event) {
	isConnected = false;
	connStatusChanged = true;
}

/**
	 scanWiFi
	 Scans for available networks 
	 and decides if a switch between
	 allowed networks makes sense

	 @return <code>bool</code>
	        True if at least one allowed network was found
*/
bool scanWiFi() {
	/** RSSI for primary network */
	int8_t rssiPrim;
	/** RSSI for secondary network */
	int8_t rssiSec;
	/** Result of this function */
	bool result = false;

	Serial.println("Start scanning for networks");

	WiFi.disconnect(true);
	WiFi.enableSTA(true);
	WiFi.mode(WIFI_STA);

	// Scan for AP
	int apNum = WiFi.scanNetworks(false,true,false,1000);
	if (apNum == 0) {
		Serial.println("Found no networks?????");
		return false;
	}
	
	byte foundAP = 0;
	bool foundPrim = false;

	for (int index=0; index<apNum; index++) {
		String ssid = WiFi.SSID(index);
		Serial.println("Found AP: " + ssid + " RSSI: " + WiFi.RSSI(index));
		if (!strcmp((const char*) &ssid[0], (const char*) &ssidPrim[0])) {
			Serial.println("Found primary AP");
			foundAP++;
			foundPrim = true;
			rssiPrim = WiFi.RSSI(index);
		}
		if (!strcmp((const char*) &ssid[0], (const char*) &ssidSec[0])) {
			Serial.println("Found secondary AP");
			foundAP++;
			rssiSec = WiFi.RSSI(index);
		}
	}

	switch (foundAP) {
		case 0:
			result = false;
			break;
		case 1:
			if (foundPrim) {
				usePrimAP = true;
			} else {
				usePrimAP = false;
			}
			result = true;
			break;
		default:
			Serial.printf("RSSI Prim: %d Sec: %d\n", rssiPrim, rssiSec);
			if (rssiPrim > rssiSec) {
				usePrimAP = true; // RSSI of primary network is better
			} else {
				usePrimAP = false; // RSSI of secondary network is better
			}
			result = true;
			break;
	}
	return result;
}

/**
 * Start connection to AP
 */
void connectWiFi() {
	// Setup callback function for successful connection
	WiFi.onEvent(gotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
	// Setup callback function for lost connection
	WiFi.onEvent(lostCon, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

	WiFi.disconnect(true);
	WiFi.enableSTA(true);
	WiFi.mode(WIFI_STA);

	Serial.println();
	Serial.print("Start connection to ");
	if (usePrimAP) {
		Serial.println(ssidPrim);
		WiFi.begin(ssidPrim.c_str(), pwPrim.c_str());
	} else {
		Serial.println(ssidSec);
		WiFi.begin(ssidSec.c_str(), pwSec.c_str());
	}
}

const int BUFFER_SIZE = 4096;
uint8_t bleReadBuffer[BUFFER_SIZE];

// Task for reading BLE Serial
void ReadBLESerialTask(void *e)
{
    while (true)
    {
        if (BleSerial_available())
        {
			// onWrite
            auto count = BleSerial_readBytes(bleReadBuffer, BUFFER_SIZE);
			if (count == 0) {
				continue;
			}
			Serial.println("Received over BLESerial: " + String((char *)&bleReadBuffer[0]));			
			char *value = (char*)bleReadBuffer;

			// Decode data
			int keyIndex = 0;
			for (int index = 0; index < count; index ++) {
				value[index] = (char) value[index] ^ (char) apName[keyIndex];
				keyIndex++;
				if (keyIndex >= strlen(apName)) keyIndex = 0;
			}

			/** Json object for incoming data */
			JsonObject& jsonIn = jsonBuffer.parseObject((char *)&value[0]);
			if (jsonIn.success()) {
				bool write = false;
				if (jsonIn.containsKey("write")) {
					write = jsonIn["write"].as<bool>();
				}
				if (write) {
					if (jsonIn.containsKey("ssidPrim") &&
							jsonIn.containsKey("pwPrim") && 
							jsonIn.containsKey("ssidSec") &&
							jsonIn.containsKey("pwSec")) {
						ssidPrim = jsonIn["ssidPrim"].as<String>();
						pwPrim = jsonIn["pwPrim"].as<String>();
						ssidSec = jsonIn["ssidSec"].as<String>();
						pwSec = jsonIn["pwSec"].as<String>();

						Preferences preferences;
						preferences.begin("WiFiCred", false);
						preferences.putString("ssidPrim", ssidPrim);
						preferences.putString("ssidSec", ssidSec);
						preferences.putString("pwPrim", pwPrim);
						preferences.putString("pwSec", pwSec);
						preferences.putBool("valid", true);
						preferences.end();

						Serial.println("Received over bluetooth:");
						Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
						Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
						connStatusChanged = true;
						hasCredentials = true;
					} else if (jsonIn.containsKey("erase")) {
						Serial.println("Received erase command");
						Preferences preferences;
						preferences.begin("WiFiCred", false);
						preferences.clear();
						preferences.end();
						connStatusChanged = true;
						hasCredentials = false;
						ssidPrim = "";
						pwPrim = "";
						ssidSec = "";
						pwSec = "";

						int err;
						err=nvs_flash_init();
						Serial.println("nvs_flash_init: " + err);
						err=nvs_flash_erase();
						Serial.println("nvs_flash_erase: " + err);
					} else if (jsonIn.containsKey("reset")) {
						WiFi.disconnect();
						esp_restart();
					}
				} else {
					Serial.println("BLESerial onRead request");
					String wifiCredentials;

					/** Json object for outgoing data */
					JsonObject& jsonOut = jsonBuffer.createObject();
					jsonOut["write"] = false;
					jsonOut["ssidPrim"] = ssidPrim;
					jsonOut["pwPrim"] = pwPrim;
					jsonOut["ssidSec"] = ssidSec;
					jsonOut["pwSec"] = pwSec;
					// Convert JSON object into a string
					jsonOut.printTo(wifiCredentials);

					// encode the data
					int keyIndex = 0;
					Serial.println("Stored settings: " + wifiCredentials);
					for (int index = 0; index < wifiCredentials.length(); index ++) {
						wifiCredentials[index] = (char) wifiCredentials[index] ^ (char) apName[keyIndex];
						keyIndex++;
						if (keyIndex >= strlen(apName)) keyIndex = 0;
					}
					//pCharacteristicWiFi->setValue((uint8_t*)&wifiCredentials[0],wifiCredentials.length());
					BleSerial_write((uint8_t*)&wifiCredentials[0],wifiCredentials.length());
				}
			} else {
				Serial.println("Received invalid JSON");
			}
			jsonBuffer.clear();
        }
        delay(20);
    }
}

void setup() {
	// Initialize Serial port
	Serial.begin(115200);

	// Send some device info
	Serial.print("Build: ");
	Serial.println(compileDate);

	Preferences preferences;
	preferences.begin("WiFiCred", false);
	bool hasPref = preferences.getBool("valid", false);
	if (hasPref) {
		ssidPrim = preferences.getString("ssidPrim","");
		ssidSec = preferences.getString("ssidSec","");
		pwPrim = preferences.getString("pwPrim","");
		pwSec = preferences.getString("pwSec","");

		if (ssidPrim.equals("") 
				|| pwPrim.equals("")
				|| ssidSec.equals("")
				|| pwPrim.equals("")) {
			Serial.println("Found preferences but credentials are invalid");
		} else {
			Serial.println("Read from preferences:");
			Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
			Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
			hasCredentials = true;
		}
	} else {
		Serial.println("Could not find preferences, need send data over BLE");
	}
	preferences.end();

	// Start BLE server
	initBLE();

	if (hasCredentials) {
		// Check for available AP's
		if (!scanWiFi) {
			Serial.println("Could not find any AP");
		} else {
			// If AP was found, start connection
			connectWiFi();
		}
	}

    // Start tasks
    xTaskCreate(ReadBLESerialTask, "ReadBLESerialTask", 10240, NULL, 1, NULL);
}

void loop() {
	if (connStatusChanged) {
		if (isConnected) {
			Serial.print("Connected to AP: ");
			Serial.print(WiFi.SSID());
			Serial.print(" with IP: ");
			Serial.print(WiFi.localIP());
			Serial.print(" RSSI: ");
			Serial.println(WiFi.RSSI());
		} else {
			if (hasCredentials) {
				Serial.println("Lost WiFi connection");
				// Received WiFi credentials
				if (!scanWiFi) { // Check for available AP's
					Serial.println("Could not find any AP");
				} else { // If AP was found, start connection
					connectWiFi();
				}
			} 
		}
		connStatusChanged = false;
	}
}