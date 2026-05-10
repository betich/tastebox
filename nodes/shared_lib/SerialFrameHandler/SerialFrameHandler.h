#pragma once
#include <Arduino.h>
#include <RS485Node.h>  // reuse DefaultReadHandler / DefaultWriteHandler typedefs

// Handles the same @{ADDR:02X} R/W frame protocol as RS485Node but over any
// Stream (e.g. Serial / HardwareSerial). No DE/RE pin — suitable for direct
// USB serial connections.
//
// Frame formats (same as RS485Node):
//   read:  "@{ADDR:02X} R {REG:02X}\n"            → "@{ADDR:02X} {VAL:02X}\n"
//   write: "@{ADDR:02X} W {REG:02X} {B0} ...\n"  → "@{ADDR:02X} OK\n"
//
// Usage:
//   SerialFrameHandler serial_handler(0x42);
//   // in setup():
//   serial_handler.setDefaultReadHandler(processRead);
//   serial_handler.setDefaultWriteHandler(processWrite);
//   // in loop():
//   serial_handler.poll(Serial);

typedef void (*PlainTextHandler)(const char* line, Stream& s);

class SerialFrameHandler {
public:
    explicit SerialFrameHandler(uint8_t addr);

    void setDefaultReadHandler(DefaultReadHandler fn);
    void setDefaultWriteHandler(DefaultWriteHandler fn);
    void setPlainTextHandler(PlainTextHandler fn);  // called for non-@ lines

    void poll(Stream& s);  // call from loop()

private:
    uint8_t          _addr;
    DefaultReadHandler  _read;
    DefaultWriteHandler _write;
    PlainTextHandler    _plain;
    char    _buf[64];
    uint8_t _buf_len;

    void _processFrame(Stream& s);
};
