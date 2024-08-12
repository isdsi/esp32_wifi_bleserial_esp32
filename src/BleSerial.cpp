// Includes for BLE
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLE2902.h>
#include "ByteRingBuffer.h"
#include "BleSerial.h"



// List of Service and Characteristic UUIDs
#define BLE_SERIAL_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define BLE_BUFFER_SIZE ESP_GATT_MAX_ATTR_LEN // must be greater than MTU, less than ESP_GATT_MAX_ATTR_LEN
#define MIN_MTU 50
#define RX_BUFFER_SIZE 4096


////////////////////////////////////////////////////////////////////////////////
// BLERxHandler
////////////////////////////////////////////////////////////////////////////////

/**
 * BLERxHandler
 * Callbacks for BLE client read/write requests
 */
class BLERxHandler : public BLECharacteristicCallbacks
{
    virtual void onWrite(BLECharacteristic *pCharacteristic) override;

    //virtual void onRead(BLECharacteristic *pCharacteristic) override;
};


/** Unique device name */
char apName[] = "ESP32-xxxxxxxxxxxx";
/** Selected network 
    true = use primary network
		false = use secondary network
*/

/** BLE Advertiser */
BLEAdvertising* pAdvertising;
/** BLE Service */
BLEService *pService;
/** BLE Server */
BLEServer *pServer;

BLECharacteristic *pCharacteristicRx = NULL;
BLECharacteristic *pCharacteristicTx = NULL;
BLE2902 *pDescRx;
BLE2902 *pDescTx;

BLERxHandler *pRxCallback = NULL;

size_t transmitBufferLength = 0;

ByteRingBuffer<RX_BUFFER_SIZE> receiveBuffer;
size_t numAvailableLines;

unsigned long long lastFlushTime;
uint8_t transmitBuffer[BLE_BUFFER_SIZE] = {0};

uint16_t peerMTU;
uint16_t maxTransferSize = BLE_BUFFER_SIZE;

/**
 * MyServerCallbacks
 * Callbacks for client connection and disconnection
 */
class MyServerCallbacks: public BLEServerCallbacks {
	// TODO this doesn't take into account several clients being connected
	void onConnect(BLEServer* pServer) {
		Serial.println("BLE client connected");
	};

	void onDisconnect(BLEServer* pServer) {
		Serial.println("BLE client disconnected");
		pAdvertising->start();
	}
};

void BLERxHandler::onWrite(BLECharacteristic *pCharacteristic)
{
    if (pCharacteristic->getUUID().toString() == BLE_RX_UUID)
    {
        std::string value = pCharacteristic->getValue();

        for (int i = 0; i < value.length(); i++)
            receiveBuffer.add(value[i]);
    }
}

/**
 * Create unique device name from MAC address
 **/
void createName() {
	uint8_t baseMac[6];
	// Get MAC address for WiFi station
	esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
	// Write unique name into apName
	sprintf(apName, "ESP32-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
}

int BleSerial_read()
{
    uint8_t result = receiveBuffer.pop();
    if (result == (uint8_t)'\n')
    {
        numAvailableLines--;
    }
    return result;
}

size_t BleSerial_readBytes(uint8_t *buffer, size_t bufferSize)
{
    int i = 0;
    while (i < bufferSize && BleSerial_available())
    {
        buffer[i] = receiveBuffer.pop();
        i++;
    }
    return i;
}

int BleSerial_peek()
{
    if (receiveBuffer.getLength() == 0)
        return -1;
    return receiveBuffer.get(0);
}

int BleSerial_available()
{
    return receiveBuffer.getLength();
}

size_t BleSerial_write(const uint8_t *buffer, size_t bufferSize)
{
    if (maxTransferSize < MIN_MTU)
    {
        int oldTransferSize = maxTransferSize;
        peerMTU = pServer->getPeerMTU(pServer->getConnId()) - 5;
        maxTransferSize = peerMTU > BLE_BUFFER_SIZE ? BLE_BUFFER_SIZE : peerMTU;

        if (maxTransferSize != oldTransferSize)
        {
            log_e("Max BLE transfer size set to %u", maxTransferSize);
        }
    }

    if (maxTransferSize < MIN_MTU)
    {
        return 0;
    }

    size_t written = 0;
    for (int i = 0; i < bufferSize; i++)
    {
        written += BleSerial_write(buffer[i]);
    }
    BleSerial_flush();
    return written;
}

size_t BleSerial_write(uint8_t byte)
{
    if (pServer->getConnectedCount() == 0)
    {
        return 0;
    }
    transmitBuffer[transmitBufferLength] = byte;
    transmitBufferLength++;
    if (transmitBufferLength == maxTransferSize)
    {
        BleSerial_flush();
    }
    return 1;
}

void BleSerial_flush()
{
    if (transmitBufferLength > 0)
    {
        pCharacteristicTx->setValue(transmitBuffer, transmitBufferLength);
        transmitBufferLength = 0;
    }
    lastFlushTime = millis();
    pCharacteristicTx->notify(true);
}


/**
 * initBLE
 * Initialize BLE service and characteristic
 * Start BLE server and service advertising
 */
void initBLE() {
    // Create unique device name
	createName();
    
	// Initialize BLE and set output power
	BLEDevice::init(apName);
	BLEDevice::setPower(ESP_PWR_LVL_P6);

	// Create BLE Server
	pServer = BLEDevice::createServer();

	// Set server callbacks
	pServer->setCallbacks(new MyServerCallbacks());

    // Create BLE Service
    pService = pServer->createService(BLE_SERIAL_SERVICE_UUID);

    pCharacteristicRx = pService->createCharacteristic(
        BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE);

    pCharacteristicTx = pService->createCharacteristic(
        BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);

    // esp32-s3 GATT_INSUF_AUTHENTICATION failed
    // pCharacteristicRx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
    pCharacteristicRx->setAccessPermissions(ESP_GATT_PERM_WRITE);

    // esp32-s3 GATT_INSUF_AUTHENTICATION failed
    // pCharacteristicTx->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
    pCharacteristicTx->setAccessPermissions(ESP_GATT_PERM_READ);

    pDescRx = new BLE2902();
    pDescTx = new BLE2902();
    pCharacteristicRx->addDescriptor(pDescRx);
    pCharacteristicTx->addDescriptor(pDescTx);

    pCharacteristicRx->setWriteProperty(true);
    pRxCallback = new BLERxHandler();
    pCharacteristicRx->setCallbacks(pRxCallback);
    pCharacteristicTx->setReadProperty(true);
    pCharacteristicTx->setNotifyProperty(true);

	// Start the service
	pService->start();

	// Start advertising
	pAdvertising = pServer->getAdvertising();
	pAdvertising->start();
}
