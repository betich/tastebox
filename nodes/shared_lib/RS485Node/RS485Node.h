#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

#define RS485NODE_MAX_HANDLERS 24

// Per-register handlers (reg is implicit — handler knows which reg it serves)
typedef uint8_t (*ReadHandler)();
typedef void    (*WriteHandler)(uint8_t* data, uint8_t len);

// Default (catch-all) handlers — reg is passed explicitly, matching the
// existing processRead(uint8_t) / processWrite(uint8_t, uint8_t*, uint8_t)
// signatures already present in every node.
typedef uint8_t (*DefaultReadHandler)(uint8_t reg);
typedef void    (*DefaultWriteHandler)(uint8_t reg, uint8_t* data, uint8_t len);

class RS485Node {
public:
    RS485Node(uint8_t addr, uint8_t rx_pin, uint8_t tx_pin,
              uint8_t de_re_pin, long baud = 9600);

    void begin();
    void poll();

    // Drop-in migration: wire existing processRead / processWrite as defaults
    void setDefaultReadHandler(DefaultReadHandler fn);
    void setDefaultWriteHandler(DefaultWriteHandler fn);

    // Optional per-register overrides
    void onRead(uint8_t reg, ReadHandler fn);
    void onWrite(uint8_t reg, WriteHandler fn);

private:
    uint8_t        _addr;
    uint8_t        _de_re_pin;
    SoftwareSerial _serial;

    DefaultReadHandler  _default_read;
    DefaultWriteHandler _default_write;

    struct ReadEntry  { uint8_t reg; ReadHandler  fn; };
    struct WriteEntry { uint8_t reg; WriteHandler fn; };
    ReadEntry  _reads[RS485NODE_MAX_HANDLERS];
    WriteEntry _writes[RS485NODE_MAX_HANDLERS];
    uint8_t    _read_count;
    uint8_t    _write_count;

    char    _buf[64];
    uint8_t _buf_len;

    void _processFrame();
    void _send(const char* msg);
};
