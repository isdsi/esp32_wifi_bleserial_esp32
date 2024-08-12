#ifndef BLESERIAL_H
#define BLESERIAL_H

int BleSerial_read();
size_t BleSerial_readBytes(uint8_t *buffer, size_t bufferSize);
int BleSerial_peek();
int BleSerial_available();
size_t BleSerial_write(const uint8_t *buffer, size_t bufferSize);
size_t BleSerial_write(uint8_t byte);
void BleSerial_flush();
void initBLE();

extern char apName[];

#endif // BLESERIAL_H