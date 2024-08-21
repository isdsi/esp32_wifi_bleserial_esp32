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
#include <FS.h>
#include <SPIFFS.h>
#include <CRC32.h>
#include "BleSerial.h"
#include <esp_task_wdt.h>

/** Build time */
const char compileDate[] = __DATE__ " " __TIME__;

bool usePrimAP = true;
/** Flag if stored AP credentials are available */
bool hasCredentials = false;
/** Connection status */
volatile bool isConnected = false;
/** Connection change status */
bool connStatusChanged = false;

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

String RGConfigTypeToString(RGConfigType type);

class RGConfig {
public:
	RGConfigType type;
	char name[8];
	int min;
	int max;
	char summary[32];
	RGConfigOption *options;
	int options_count;

	RGConfig(RGConfigType type, char name[8], int min, int max, char summary[32], RGConfigOption *options, int options_count) {
		this->type = type;
		strcpy(this->name, name);
		this->min = min;
		this->max = max;
		strcpy(this->summary, summary);
		this->options = options;
		this->options_count = options_count;
	}

	virtual void Get(Preferences *p) = 0;

	virtual void Put(Preferences *p) = 0;

	virtual void ToJson(JsonObject &jo) = 0;

	virtual void ToJsonArrayValue(JsonArray &jo) = 0;

	virtual void FromJson(JsonObject &jo) = 0;

	virtual void FromJsonArrayValue(JsonArray &ja)  = 0;

protected:	
	void ToJsonInternal(JsonObject &jo) {
		jo["name"] = this->name;
		jo["type"] = RGConfigTypeToString(this->type);
		jo["min"] = this->min;
		jo["max"] = this->max;
		jo["summary"] = this->summary;
		JsonArray &ja = jo.createNestedArray("options");
		for(int i = 0; i < this->options_count; i++) {
			ja.add(this->options[i].o);
		}
	}
};

class RGConfigInteger : public RGConfig {
public:
	int value;
	int defaultValue;

	RGConfigInteger(RGConfigType type, char name[8], int value, int min, int max, int defaultValue, char summary[32], RGConfigOption *options, int options_count)
		: RGConfig(type, name, min, max, summary, options, options_count)
	{
		this->value = value;
		this->defaultValue = defaultValue;
	}

	virtual void Get(Preferences *p) override {
		this->value = p->getInt(this->name, this->defaultValue);
	}

	virtual void Put(Preferences *p) override {
		p->putInt(this->name, this->value);
	}

	virtual void ToJson(JsonObject &jo) override {
		RGConfig::ToJsonInternal(jo);
		jo["value"].set<int>(this->value);
		jo["defaultValue"].set<int>(this->defaultValue);
	}

	virtual void ToJsonArrayValue(JsonArray &ja) override {
		/*
		Serial.print("v ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.print(" ");
		Serial.print(this->value);
		Serial.print(" ");
		Serial.println(String(this->value));
		*/
		ja.add(String(this->value));
	}

	virtual void FromJson(JsonObject &jo) override {
		// Is it useful?
	}

	virtual void FromJsonArrayValue(JsonArray &ja) override {
		String s = ja.get<String>(0);
		this->value = s.toInt();
		ja.remove(0);
		/*
		Serial.print("v ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.print(" ");
		Serial.print(this->value);
		Serial.print(" ");
		Serial.println(String(this->value));
		*/
	}
};

class RGConfigString : public RGConfig {
public:
	char value[32];
	char defaultValue[32];

	RGConfigString(RGConfigType type, char name[8], char value[8], int min, int max, char defaultValue[8], char summary[32], RGConfigOption *options, int options_count)
		: RGConfig(type, name, min, max, summary, options, options_count)
	{
		strcpy(this->value, value);
		strcpy(this->defaultValue, defaultValue);
	}

	virtual void Get(Preferences *p) override {
		String s = p->getString(this->name, this->defaultValue);
		strcpy(this->value, (char*)&s[0]);
	}
	
	virtual void Put(Preferences *p) override {
		p->putString(this->name, this->value);
	}

	virtual void ToJson(JsonObject &jo) override {
		RGConfig::ToJsonInternal(jo);
		jo["value"].set<String>(this->value);
		jo["defaultValue"].set<String>(this->defaultValue);
	}

	virtual void ToJsonArrayValue(JsonArray &ja) override {
		/*
		Serial.print("v ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.print(" ");
		Serial.print(this->value);
		Serial.print(" ");
		*/
		Serial.println(String(this->value));
		ja.add(this->value);
	}
	
	virtual void FromJson(JsonObject &jo) override {
		// Is it useful?
	}

	virtual void FromJsonArrayValue(JsonArray &ja) override {
		String s = ja.get<String>(0);
		strcpy(this->value, (char*)&s[0]);
		ja.remove(0);
		/*
		Serial.print("v ");
		Serial.print((uint32_t)(&this->value), HEX);
		Serial.print(" ");
		Serial.print(this->value);
		Serial.print(" ");
		Serial.println(String(this->value));
		*/
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

RGConfigInteger rgci_array[] = {
	RGConfigInteger(RGCONFIGTYPE_SWITCH, (char*)"sw1", 0, 0, 1, 1, (char*)"switch Example", NULL, 0),
	RGConfigInteger(RGCONFIGTYPE_SEEKBAR, (char*)"sb1", 50, 0, 100, 50, (char*)"seekBar Example", NULL, 0 ),
	RGConfigInteger(RGCONFIGTYPE_SPINNER, (char*)"sp1", 1, 0, 2, 1, (char*)"spinner Example", rgco_spinner, rgco_spinner_count ),
	RGConfigInteger(RGCONFIGTYPE_NUMBER, (char*)"n1", 10, 0, 100, 1, (char*)"number Example", NULL, 0 ),
};

RGConfigString rgcs_array[] = {
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"t1", (char*)"test", 0, 32, (char*)"default", (char*)"text Example", NULL, 0 ),
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"ssidPrim", (char*)"test", 0, 32, (char*)"", (char*)"1차 SSID", NULL, 0 ),
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"pwPrim", (char*)"test", 0, 32, (char*)"", (char*)"1차 비밀번호", NULL, 0 ),
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"ssidSec", (char*)"test", 0, 32, (char*)"", (char*)"2차 SSID", NULL, 0 ),
	RGConfigString(RGCONFIGTYPE_TEXT, (char*)"pwSec", (char*)"test", 0, 32, (char*)"", (char*)"2차 비밀번호", NULL, 0 ),
};

RGConfig* rgc_array[] = {
	&rgci_array[0],
	&rgci_array[1],
	&rgci_array[2],
	&rgci_array[3],
	&rgcs_array[0],
	&rgcs_array[1],
	&rgcs_array[2],
	&rgcs_array[3],
	&rgcs_array[4],
};
const int rgc_array_count = sizeof(rgc_array) / sizeof(RGConfig*);

// Accessor for Configurations
#define RGCI_VALUE(a) (((RGConfigInteger*)rgc_array[a])->value)
#define RGCS_VALUE(a) (((RGConfigString*)rgc_array[a])->value)

// Indexes of Configurations
#define E_SW1 0
#define E_SB1 E_SW1 + 1
#define E_SP1 E_SB1 + 1
#define E_N1 E_SP1 + 1
#define E_T1 E_N1 + 1
#define E_SSID_PRIM E_T1 + 1
#define E_PW_PRIM E_SSID_PRIM + 1
#define E_SSID_SEC E_PW_PRIM + 1
#define E_PW_SEC E_SSID_SEC + 1

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
		if (!strcmp((const char*) &ssid[0], RGCS_VALUE(E_SSID_PRIM))) {
			Serial.println("Found primary AP");
			foundAP++;
			foundPrim = true;
			rssiPrim = WiFi.RSSI(index);
		}
		if (!strcmp((const char*) &ssid[0], RGCS_VALUE(E_SSID_SEC))) {
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
		Serial.println(RGCS_VALUE(E_SSID_PRIM));
		WiFi.begin(RGCS_VALUE(E_SSID_PRIM), RGCS_VALUE(E_PW_PRIM));
	} else {
		Serial.println(RGCS_VALUE(E_SSID_SEC));
		WiFi.begin(RGCS_VALUE(E_SSID_SEC), RGCS_VALUE(E_PW_SEC));
	}
}

const int BUFFER_SIZE = 4096;
uint8_t ble_read_buffer[BUFFER_SIZE];
uint8_t ble_write_buffer[BUFFER_SIZE];
uint16_t ble_read_count;
uint16_t ble_write_count;
uint8_t ble_timer10ms;
uint16_t ble_state = 100;
uint8_t ble_state_timer100ms;
String ble_write_string;
String ble_read_string;
String ble_file_name;
size_t ble_file_size;
uint32_t ble_file_crc = 0;
const uint8_t ble_file_timeout_100ms = 30;
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


/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED true

bool spiffs_mount = false;

extern size_t transmitBufferLength;

CRC32 crc;

void listDirToJson(fs::FS &fs, const char * dirname, uint8_t levels, JsonArray &jaFileName, JsonArray &jaFileSize){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
			/*
            // support only 1 level.
			if(levels){
                listDir(fs, file.path(), levels -1);
            }
			*/
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
		jaFileName.add<String>(file.name());
		jaFileSize.add<size_t>(file.size());
        file = root.openNextFile();
    }
}

bool listDirSize(fs::FS &fs, const char * dirname, const char *filename_to_except, size_t *size){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return false;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return false;
    }

	size_t s = 0;
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
			/*
            // support only 1 level.
			if(levels){
                listDir(fs, file.path(), levels -1);
            }
			*/
        } else {			
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
		if (filename_to_except != NULL && strcmp(file.name(), filename_to_except) == 0) {
	        file = root.openNextFile();
			continue;		
		}
		s += file.size();
        file = root.openNextFile();
    }
	if (size != NULL) 
		*size = s;
	return true;
}

bool getFileSize(fs::FS &fs, const char * path, size_t *size){
    Serial.printf("Getting file size: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return false;
    }
	if (size != NULL) {
		*size = file.size();
	}
    file.close();
	return true;
}

bool getFileCRC(fs::FS &fs, const char * path, uint32_t *checksum){
    Serial.printf("Getting file crc: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return false;
    }

	char c;
	crc.reset();
    while(file.available()){
		c = file.read();
		crc.update(c);
    }
    file.close();
	if (checksum != NULL)
		*checksum = crc.finalize();
	return true;
}

bool readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return false;
    }
	uint32_t timer100ms = millis() / 100;
	int size = file.available();
	while(size > 0){
		timer100ms = millis() / 100;		
		int payload = 256;
		uint32_t bytes_to_read;
		if (size - payload >= 0) {
			bytes_to_read = payload;
		} else {
			bytes_to_read = size;
		}
		Serial.print("r");
		Serial.println(bytes_to_read);
		file.read(ble_write_buffer, bytes_to_read);
		BleSerial_write(ble_write_buffer, bytes_to_read); // BleSerial_flush inside
		size -= bytes_to_read;
		delay(1);
		if ((millis() / 100) - timer100ms > ble_file_timeout_100ms) {
			break;
		}
	}
    file.close();
	if ((millis() / 100) - timer100ms > ble_file_timeout_100ms) {
		Serial.println("timeout");
        return false;
	}
	return true;
}

bool writeFile(fs::FS &fs, const char * path, int size){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return false;
    }

	uint32_t timer100ms = millis() / 100;
    while(size > 0) {
		if (BleSerial_available() > 0) { 
			timer100ms = millis() / 100;		
			uint32_t bytes_to_write;
			if (size - BleSerial_available() >= 0) {
				bytes_to_write = BleSerial_available();
			} else {
				bytes_to_write = size;
			}
			BleSerial_readBytes(ble_read_buffer, bytes_to_write);
			Serial.print("w");
			Serial.println(bytes_to_write);
			file.write(ble_read_buffer, bytes_to_write);
			size -= bytes_to_write;
		}
		if ((millis() / 100) - timer100ms > ble_file_timeout_100ms) {
			break;
		}
		esp_task_wdt_reset();
    }
    file.close();
	if ((millis() / 100) - timer100ms > ble_file_timeout_100ms) {
		Serial.println("timeout");
        return false;
	}
	return true;
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("- file renamed");
    } else {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\r\n", path);
    if(fs.remove(path)){
        Serial.println("- file deleted");
    } else {
        Serial.println("- delete failed");
    }
}


// Task for reading BLE Serial
void ReadBLESerialTask(void *e)
{
    while (true)
    {
		if (ble_timer10ms > 0)
            ble_timer10ms--;
        if (ble_timer10ms == 0) {
            ble_timer10ms = 10;

            // 10ms tick
            if (ble_state_timer100ms > 0)
                ble_state_timer100ms--;
        }
        // 100ms tick
        /*
        if (ble_state_timer100ms > 0)
            ble_state_timer100ms--;
        */
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
			
        }
		switch(ble_state) {
		case 0:
			break;

		case 100: // ready
		{
			if (ble_read_string == "")
				break;

			JsonObject& jo = jsonBuffer.parseObject(ble_read_string);
			if (jo.success() == false) 
				break;

			if (jo.containsKey("read"))
			{
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
				if (jo["read"].as<String>() == "value")
				{
					ble_state = 130;
					break;		
				}
				if (jo["read"].as<String>() == "filesystem")
				{
					ble_state = 140;
					break;		
				}
				if (jo["read"].as<String>() == "listDir")
				{
					ble_state = 150;
					break;		
				}
				if (jo["read"].as<String>() == "file")
				{
					ble_state = 160;
					break;		
				}
			}
			if (jo.containsKey("write"))
			{
				if (jo["write"].as<String>() == "value")
				{
					ble_state = 230;
					break;		
				}
				if (jo["write"].as<String>() == "file")
				{
					ble_state = 260;
					break;		
				}
			}
			if (jo.containsKey("erase"))
			{
				ble_state = 300;
				break;		
			}
			if (jo.containsKey("reset"))
			{
				ble_state = 310;
				break;		
			}
			
			break;
		}

		case 110: // read config_count
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "config_count";
			jo["config_count"] = rgc_array_count;

			ble_read_string = "";
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
				RGConfig* rgc = rgc_array[ble_config_index];
				rgc->ToJson(jo);
			}

			ble_read_string = "";
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
		
		case 130: // read value
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "value";
			JsonArray& ja = jo.createNestedArray("value");
			for(int i = 0; i < rgc_array_count; i++) {
				RGConfig* rgc = rgc_array[i];
				rgc->ToJsonArrayValue(ja);
			}

			ble_read_string = "";
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
		
		case 140: // read filesystem
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "filesystem";
			if (spiffs_mount) {
				jo["result"] = "ok";
				size_t totalBytes;
				totalBytes = SPIFFS.totalBytes() * 0.80; // filesystem use at least 20% of partition
				jo["totalBytes"] = totalBytes;
				size_t usedBytes; // it means entire size of all files
				listDirSize(SPIFFS, "/", NULL, &usedBytes); 
				jo["usedBytes"] = usedBytes;
			} else {
				jo["result"] = "failed not mount";
			}

			ble_read_string = "";
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
				
		case 150: // read listDir
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["read"] = "listDir";
			if (spiffs_mount) {
				jo["result"] = "ok";
				JsonArray& jaFileName = jo.createNestedArray("listDirFileName");
				JsonArray& jaFileSize = jo.createNestedArray("listDirFileSize");
				listDirToJson(SPIFFS, "/", 0, jaFileName, jaFileSize);
			} else {
				jo["result"] = "failed not mount";
			}

			ble_read_string = "";
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
				
		case 160: // read file
		{
			jsonBuffer.clear();

			JsonObject& joRead = jsonBuffer.parseObject(ble_read_string);
			if (joRead.success() == false) {
				ble_state = 100;
				break;
			}

			JsonObject& joWrite = jsonBuffer.createObject();
			joWrite["read"] = "file";
			ble_file_name = "";
			if (spiffs_mount) {
				if (joRead.containsKey("fileName")) {
					ble_file_name = joRead["fileName"].as<String>();
					if (getFileSize(SPIFFS, (char*)&ble_file_name[0], &ble_file_size) && 
						getFileCRC(SPIFFS, (char*)&ble_file_name[0], &ble_file_crc)) {
						joWrite["result"] = "ok";
						joWrite["fileSize"] = ble_file_size;
						joWrite["fileCRC"] = ble_file_crc;
					} else {
						joWrite["result"] = "failed file not exist";
					}
				} else {
					joWrite["result"] = "failed argument invalid";
				}
			} else {
				joWrite["result"] = "failed not mount";
			}

			ble_read_string = "";
			ble_write_string = ""; joWrite.printTo(ble_write_string);
			jsonBuffer.clear();
			ble_write_count = ble_write_string.length();
			Serial.print("ws ");
			Serial.println(ble_write_string);			
			memcpy(ble_write_buffer, (void*)&ble_write_string[0], ble_write_string.length());
			BleSerial_encode(ble_write_buffer, ble_write_count);
			BleSerial_write(ble_write_buffer, ble_write_count);
			if (joWrite["result"] != "ok") {
				ble_state = 100;
				break;
			}
			ble_state_timer100ms = 1; // give time android to get ready
			ble_state++;
			break;
		}
		break;
				
		case 161:
		{
			if (ble_state_timer100ms != 0)
				break;

			if (readFile(SPIFFS, (char*)&ble_file_name[0]) == false)
			{
				ble_state = 100;
				break;
			}
			ble_state = 100;
			break;
		}
		break;

		case 230: // write value
		{
			jsonBuffer.clear();

			JsonObject& jo = jsonBuffer.parseObject(ble_read_string);
			if (jo.success() == false) {
				ble_state = 100;
				break;
			}

			JsonArray& ja = jo["value"];
			Preferences p;
			p.begin("configs", false);
			for(int i = 0; i < rgc_array_count; i++) {
				RGConfig* rgc = rgc_array[i];
				rgc->FromJsonArrayValue(ja);				
				rgc->Put(&p);
			}
			jsonBuffer.clear();
			p.end();
		}
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["write"] = "value";

			ble_read_string = "";
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
								
		case 260: // write file
		{
			jsonBuffer.clear();
			Serial.println("1");
			JsonObject& joRead = jsonBuffer.parseObject(ble_read_string);
			if (joRead.success() == false) {
				Serial.println("json parse failed");
				ble_state = 100;
				break;
			}

			Serial.println("2");
			JsonObject& joWrite = jsonBuffer.createObject();
			joWrite["write"] = "file";
			ble_file_name = "";
			ble_file_size = 0;
			ble_file_crc = 0;
			if (spiffs_mount) {
				if (joRead.containsKey("fileName") &&
					joRead.containsKey("fileSize") &&
					joRead.containsKey("fileCRC") ) {
					ble_file_name = joRead["fileName"].as<String>();
					ble_file_size = joRead["fileSize"].as<size_t>();
					ble_file_crc = joRead["fileCRC"].as<uint32_t>();
					size_t totalBytes;
					totalBytes = SPIFFS.totalBytes() * 0.80; // // filesystem use at least 20% of partition
					size_t usedBytes; // it means entire size of all files
					listDirSize(SPIFFS, "/", 
						&ble_file_name[1], // without '/'
						&usedBytes); // 
					if (totalBytes - usedBytes >= ble_file_size) {
						joWrite["result"] = "ok";
					} else {
						Serial.print(totalBytes);
						Serial.print("-");
						Serial.print(usedBytes);
						Serial.print(">");
						Serial.println(ble_file_size);
						joWrite["result"] = "failed too large size";
					}
				} else {
					joWrite["result"] = "failed argument invalid";
				}
			} else {
				joWrite["result"] = "failed not mount";
			}
			Serial.println("3");
			ble_read_string = "";
			ble_write_string = ""; joWrite.printTo(ble_write_string);
			jsonBuffer.clear();
			ble_write_count = ble_write_string.length();
			Serial.print("ws ");
			Serial.println(ble_write_string);			
			memcpy(ble_write_buffer, (void*)&ble_write_string[0], ble_write_string.length());
			BleSerial_encode(ble_write_buffer, ble_write_count);
			BleSerial_write(ble_write_buffer, ble_write_count);
			if (joWrite["result"] != "ok") {
				ble_state = 100;
				break;
			}
			ble_state_timer100ms = 0; 
			ble_state++;
			break;
		}
		break;
				
		case 261:
		{
			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["write"] = "file";

			if (writeFile(SPIFFS, (char*)&ble_file_name[0], ble_file_size)) {
				uint32_t crc_value = 0;
				if (getFileCRC(SPIFFS, (char*)&ble_file_name[0], &crc_value)) {
					if (ble_file_crc == crc_value) {
						jo["result"] = "ok";		
					} else {
						jo["result"] = "failed crc";
					}
				} else {
					jo["result"] = "failed get file crc";
				}
			} else {
				jo["result"] = "failed write file";
			} 
			// cannot know if ble_file_size == 0 because of error during file transferring
			/*
			ble_read_string = "";
			ble_write_string = ""; jo.printTo(ble_write_string);
			jsonBuffer.clear();
			ble_write_count = ble_write_string.length();
			Serial.print("ws ");
			Serial.println(ble_write_string);			
			memcpy(ble_write_buffer, (void*)&ble_write_string[0], ble_write_string.length());
			BleSerial_encode(ble_write_buffer, ble_write_count);
			BleSerial_write(ble_write_buffer, ble_write_count);
			ble_state_timer100ms = 0; 
			*/
			ble_state = 100;
			break;
		}
		break;

		case 300: // erase
		{
			int err;
			err = nvs_flash_init();
			Serial.print("nvs_flash_init: ");
			Serial.println(err);
			err = nvs_flash_erase();
			Serial.print("nvs_flash_erase: ");
			Serial.println(err);

			Preferences p;
			p.begin("configs", false);
			for(int i = 0; i < rgc_array_count; i++) {
				RGConfig* rgc = rgc_array[i];
				rgc->Get(&p);
			}
			jsonBuffer.clear();
			p.end();

			// Json object for outgoing data 
			JsonObject& jo = jsonBuffer.createObject();
			jo["erase"] = "";

			ble_read_string = "";
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
		
		case 310: // reset
		{			
			ESP.restart();
			break;
		}

		}
        delay(10);
    }
}

void setup() {
	// Initialize Serial port
	Serial.begin(115200);
	
	// Send some device info
	Serial.print("Build: ");
	Serial.println(compileDate);

	Preferences p;
	p.begin("configs", false);
	for(int i = 0; i < rgc_array_count; i++) {
		RGConfig* rgc = rgc_array[i];
		rgc->Get(&p);
	}
	p.end();

	RGConfigString* rgcs;
	int defaultCount = 0;
	for(int i = 5; i < 9; i++) {
		rgcs = (RGConfigString*)rgc_array[i];
		if (strcmp(rgcs->value, "") == 0) {
			defaultCount++;
		}
	}
	if (defaultCount == 4) {
		Serial.println("Found preferences but credentials are invalid");
		hasCredentials = false;
	} else {
		Serial.println("Read from preferences:");
		hasCredentials = true;
		for(int i = 5; i < 9; i++) {
			rgcs = (RGConfigString*)rgc_array[i];
			Serial.print(rgcs->name);
			Serial.print(" ");
			Serial.println(rgcs->value);
		}
	}

	/*	
	// for debug
	for(int i = 0; i < 4; i++)
	{
		RGConfigInteger *rgci = (RGConfigInteger *)(rgc_array[i]);
		Serial.print("v ");
		Serial.print((uint32_t)(&rgci->value), HEX);
		Serial.println(rgci->value);
	}
	
	for(int i = 4; i < 9; i++)
	{
		RGConfigString *rgcs = (RGConfigString *)(rgc_array[i]);
		Serial.print("v ");
		Serial.print((uint32_t)(&rgcs->value), HEX);
		Serial.println(rgcs->value);
	}
	*/

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

	if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
		spiffs_mount = false;
        return;
    }
	spiffs_mount = true;

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