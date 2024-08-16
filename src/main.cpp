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
StaticJsonBuffer<400> jsonBuffer;

enum RGConfigType {
	RGCONFIGTYPE_SWITCH,
    RGCONFIGTYPE_SEEKBAR,
    RGCONFIGTYPE_SPINNER,
    RGCONFIGTYPE_NUMBER,
    RGCONFIGTYPE_TEXT,
};

typedef struct RGConfigOption {
	char o[16];
} RGConfigOption;

class RGConfig {
public:
	RGConfigType type;
	int min;
	int max;
	char summary[32];
	RGConfigOption *options;
	int options_count;

	RGConfig(RGConfigType type, int min, int max, char summary[32], RGConfigOption *options, int options_count) {
		this->type = type;
		this->min = min;
		this->max = max;
		strcpy(this->summary, summary);
		this->options = options;
		this->options_count = options_count;
	}
};

class RGConfigInteger : public RGConfig {
public:
	int value;
	int defaultValue;

	RGConfigInteger(RGConfigType type, int value, int min, int max, int defaultValue, char summary[32], RGConfigOption *options, int options_count)
		: RGConfig(type, min, max, summary, options, options_count)
	{
		Serial.print("v ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.print(value);
		this->value = value;
		Serial.print(" tv ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.println(this->value);
		this->defaultValue = defaultValue;
	}
};

class RGConfigString : public RGConfig {
public:
	char value[8];
	char defaultValue[8];

	RGConfigString(RGConfigType type, char value[8], int min, int max, char defaultValue[8], char summary[32], RGConfigOption *options, int options_count)
		: RGConfig(type, min, max, summary, options, options_count)
	{
		strcpy(this->value, value);
		strcpy(this->defaultValue, defaultValue);
	}
};

String RGConfigTypeToString(RGConfigType type) {
	String s = "Unknown";
	switch(type) {
	case RGCONFIGTYPE_SWITCH: s = "Switch"; break;
    case RGCONFIGTYPE_SEEKBAR: s = "SeekBar"; break;
    case RGCONFIGTYPE_SPINNER: s = "Spinner"; break;
    case RGCONFIGTYPE_NUMBER: s = "Number"; break;
    case RGCONFIGTYPE_TEXT: s = "Text"; break;
	}
	return s;
}

RGConfigOption rgco_spinner[2] = {
	"Monday", "Tuesday"
};
const int rgco_spinner_count = sizeof(rgco_spinner) / sizeof(RGConfigOption);

RGConfigInteger rgci_array[] {
	RGConfigInteger(RGCONFIGTYPE_SWITCH, 0, 0, 1, 1, (char*)"switch Example", NULL, 0),
	RGConfigInteger(RGCONFIGTYPE_SEEKBAR, 50, 0, 100, 50, (char*)"seekBar Example", NULL, 0 ),
	RGConfigInteger(RGCONFIGTYPE_SPINNER, 1, 0, 2, 1, (char*)"spinner Example", rgco_spinner, rgco_spinner_count ),
	RGConfigInteger(RGCONFIGTYPE_NUMBER, 10, 0, 10, 1, (char*)"number Example", NULL, 0 ),
};

RGConfigString rgcs_array[] {
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"test", 0, 1, (char*)"default", (char*)"text Example", NULL, 0 ),
};

RGConfig* rgc_array[] = {
	&rgci_array[0],
	&rgci_array[1],
	&rgci_array[2],
	&rgci_array[3],
	&rgcs_array[0],
};
const int rgc_array_count = sizeof(rgc_array) / sizeof(RGConfig*);


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
uint8_t ble_read_buffer[BUFFER_SIZE];
uint8_t ble_write_buffer[BUFFER_SIZE];
uint16_t ble_read_count;
uint16_t ble_write_count;
uint8_t ble_state = 100;
uint8_t ble_state_timer100ms;
String ble_write_string;
String ble_read_string;
int ble_config_index;

void BleSerial_decode(uint8_t *value, uint32_t value_size)
{
    // Decode data    
	int keyIndex = 0;
    Serial.print("Received over BLESerial: ");
    for (int index = 0; index < value_size; index ++) {
        value[index] = (char) value[index] ^ (char) apName[keyIndex];
        if (index < 10) {
            Serial.print(value[index], HEX);
        }
        keyIndex++;
        if (keyIndex >= strlen(apName)) keyIndex = 0;
    }
    Serial.print(" size ");
    Serial.println(value_size);
}

void BleSerial_encode(uint8_t *value, uint32_t value_size)
{
    // encode the data
    int keyIndex = 0;
    Serial.print("Transmit over BLESerial: ");
    for (int index = 0; index < value_size; index ++) {
        if (index < 10) {
            Serial.print(value[index], HEX);
        }
        value[index] = (char) value[index] ^ (char) apName[keyIndex];
        keyIndex++;
        if (keyIndex >= strlen(apName)) keyIndex = 0;
    }
    Serial.print(" size ");
    Serial.println(value_size);
}

// Task for reading BLE Serial
void ReadBLESerialTask(void *e)
{
    while (true)
    {
        if (BleSerial_available())
        {
            size_t count = BleSerial_readBytes(ble_read_buffer, BUFFER_SIZE);
			if (count == 0) {
				continue;
			}
			BleSerial_decode(ble_read_buffer, count);
			ble_read_buffer[count] = '\0';
			ble_read_string = String((char*)ble_read_buffer);
			Serial.print("rs ");
			Serial.println(ble_read_string);
			/*
			char *value = (char*)ble_read_buffer;
			// Decode data
			int keyIndex = 0;
			for (int index = 0; index < count; index ++) {
				value[index] = (char) value[index] ^ (char) apName[keyIndex];
				keyIndex++;
				if (keyIndex >= strlen(apName)) keyIndex = 0;
			}
			
			// Json object for incoming data 
			JsonObject& jsonIn = jsonBuffer.parseObject((char *)&value[0]);
			if (jsonIn.success()) {
				bool write = false;
				if (jsonIn.containsKey("write")) {
					write = jsonIn["write"].as<bool>();
				}
				bool enumerate = false;
				if (jsonIn.containsKey("enumerate")) {
					enumerate = jsonIn["enumerate"].as<bool>();
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
					// Json object for outgoing data 
					JsonObject& jsonOut = jsonBuffer.createObject();
					if (enumerate) {
						//s = "{\"type\":\"Text\", \"value\": \"test\", \"min\": 0, \"max\": 10, \"defaultValue\" : \"default\", \"summary\":\"text Example\", \"options\": []}";
						jsonOut["write"] = false;
						jsonOut["enumerate"] = true;
						JsonArray& jaConfigs = jsonOut.createNestedArray("configs");
						JsonObject& joConfig = jaConfigs.createNestedObject();
						joConfig["type"] = "Text";
						joConfig["value"] = "";
						joConfig["min"] = 0;
						joConfig["max"] = 32;
						joConfig["defaultValue"] = "";
						joConfig["summary"] = "Primary AP";
					} else {

						jsonOut["write"] = false;
						jsonOut["ssidPrim"] = ssidPrim;
						jsonOut["pwPrim"] = pwPrim;
						jsonOut["ssidSec"] = ssidSec;
						jsonOut["pwSec"] = pwSec;
					}
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
			*/
        }
		switch(ble_state) {
		case 0:
			break;

		case 100:
		{
			if (ble_read_string == "")
				break;

			JsonObject& jo = jsonBuffer.parseObject(ble_read_string);
			ble_read_string = "";
			if (jo.success() == false) 
				break;

			if (jo.containsKey("read") == false)
				break;

			if (jo["read"].as<String>() == "config_count")
			{
				ble_state = 110;
				break;		
			}
			if (jo["read"].as<String>() == "config_index")
			{
				ble_config_index = jo["config_index"].as<int>();
				ble_state = 120;
				break;		
			}
			break;
		}

		case 110:
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "config_count";
			jo["config_count"] = rgc_array_count;

			ble_write_string = ""; jo.printTo(ble_write_string);
			jsonBuffer.clear();
			ble_write_count = ble_write_string.length();
			Serial.print("ws ");
			Serial.println(ble_write_string);			
			memcpy(ble_write_buffer, (void*)&ble_write_string[0], ble_write_string.length());
			BleSerial_encode(ble_write_buffer, ble_write_count);
			BleSerial_write(ble_write_buffer, ble_write_count);
			ble_state = 100;
			break;
		}

		case 120:
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "config_index";
			if (ble_config_index >= rgc_array_count) {
				jo["config_index"] = -1;
			} else {
				jo["config_index"] = ble_config_index;
				if (rgc_array[ble_config_index]->type == RGCONFIGTYPE_SWITCH ||
					rgc_array[ble_config_index]->type == RGCONFIGTYPE_SEEKBAR ||
					rgc_array[ble_config_index]->type == RGCONFIGTYPE_SPINNER ||
					rgc_array[ble_config_index]->type == RGCONFIGTYPE_NUMBER) {
					//RGConfigInteger* rgci = (RGConfigInteger*)&rgc[ble_config_index];
					RGConfigInteger &rgci = (RGConfigInteger &)(*rgc_array[ble_config_index]);
					Serial.print("v ");
					Serial.println(rgci.value);
					jo["type"] = RGConfigTypeToString(rgci.type);
					jo["value"] = rgci.value;
					jo["min"] = rgci.min;
					jo["max"] = rgci.max;
					jo["defaultValue"] = rgci.defaultValue;
					jo["summary"] = rgci.summary;
					JsonArray &ja = jo.createNestedArray("options");
					for(int i = 0; i < rgci.options_count; i++) {
						ja.add(rgci.options[i].o);
					}
				} else if (rgc_array[ble_config_index]->type == RGCONFIGTYPE_TEXT) {
					RGConfigString &rgcs = (RGConfigString &)(*rgc_array[ble_config_index]);
					jo["type"] = RGConfigTypeToString(rgcs.type);
					jo["value"] = rgcs.value;
					jo["min"] = rgcs.min;
					jo["max"] = rgcs.max;
					jo["defaultValue"] = rgcs.defaultValue;
					jo["summary"] = rgcs.summary;
					JsonArray &ja = jo.createNestedArray("options");
					for(int i = 0; i < rgcs.options_count; i++) {
						ja.add(rgcs.options[i].o);
					}
				}
			}

			ble_write_string = ""; jo.printTo(ble_write_string);
			jsonBuffer.clear();
			ble_write_count = ble_write_string.length();
			Serial.print("ws ");
			Serial.println(ble_write_string);			
			memcpy(ble_write_buffer, (void*)&ble_write_string[0], ble_write_string.length());
			BleSerial_encode(ble_write_buffer, ble_write_count);
			BleSerial_write(ble_write_buffer, ble_write_count);
			ble_state = 100;
			break;
		}
		break;
		}
        delay(10);
    }
}

void setup() {
	// Initialize Serial port
	//Serial.begin(115200);
	Serial.begin(115200, SERIAL_8N1, 18, 17);

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
	/*
	for(int i = 0; i < 4; i++)
	{
		RGConfigInteger *rgci = (RGConfigInteger *)(rgc_array[i]);
		Serial.print("v ");
		Serial.print((uint32_t)(&rgci->value), HEX);
		Serial.println(rgci->value);
	}
	
	for(int i = 4; i < 5; i++)
	{
		RGConfigString *rgcs = (RGConfigString *)(rgc_array[i]);
		Serial.print("v ");
		Serial.print((uint32_t)(&rgcs->value), HEX);
		Serial.println(rgcs->value);
	}
	*/
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