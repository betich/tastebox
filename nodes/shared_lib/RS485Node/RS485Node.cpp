#include "RS485Node.h"
#include <string.h>
#include <stdlib.h>

RS485Node::RS485Node(uint8_t addr, uint8_t rx_pin, uint8_t tx_pin,
                     uint8_t de_re_pin, long baud)
    : _addr(addr), _de_re_pin(de_re_pin),
      _serial(rx_pin, tx_pin),
      _default_read(nullptr), _default_write(nullptr),
      _read_count(0), _write_count(0), _buf_len(0)
{}

void RS485Node::begin() {
    pinMode(_de_re_pin, OUTPUT);
    digitalWrite(_de_re_pin, LOW);
    _serial.begin(9600);
    _serial.setTimeout(100);
}

void RS485Node::setDefaultReadHandler(DefaultReadHandler fn)  { _default_read  = fn; }
void RS485Node::setDefaultWriteHandler(DefaultWriteHandler fn){ _default_write = fn; }

void RS485Node::onRead(uint8_t reg, ReadHandler fn) {
    if (_read_count < RS485NODE_MAX_HANDLERS)
        _reads[_read_count++] = { reg, fn };
}

void RS485Node::onWrite(uint8_t reg, WriteHandler fn) {
    if (_write_count < RS485NODE_MAX_HANDLERS)
        _writes[_write_count++] = { reg, fn };
}

void RS485Node::poll() {
    while (_serial.available()) {
        char c = (char)_serial.read();
        if (c == '\n') {
            _buf[_buf_len] = '\0';
            if (_buf_len > 0 && _buf[0] == '@')
                _processFrame();
            _buf_len = 0;
        } else if (_buf_len < 63) {
            _buf[_buf_len++] = c;
        }
    }
}

// Frame: "@{ADDR:02X} R {REG:02X}"  or  "@{ADDR:02X} W {REG:02X} {B0} ..."
void RS485Node::_processFrame() {
    if (_buf_len < 8) return;

    char addr_str[3] = { _buf[1], _buf[2], '\0' };
    uint8_t frame_addr = (uint8_t)strtoul(addr_str, nullptr, 16);
    if (frame_addr != _addr) return;

    if (_buf[3] != ' ') return;
    char cmd = _buf[4];
    if (_buf[5] != ' ') return;

    char reg_str[3] = { _buf[6], _buf[7], '\0' };
    uint8_t reg = (uint8_t)strtoul(reg_str, nullptr, 16);

    if (cmd == 'R') {
        // Per-register handler takes priority
        for (uint8_t i = 0; i < _read_count; i++) {
            if (_reads[i].reg == reg) {
                uint8_t val = _reads[i].fn();
                char resp[16];
                snprintf(resp, sizeof(resp), "@%02X %02X\n", _addr, val);
                _send(resp);
                return;
            }
        }
        if (_default_read) {
            uint8_t val = _default_read(reg);
            char resp[16];
            snprintf(resp, sizeof(resp), "@%02X %02X\n", _addr, val);
            _send(resp);
        } else {
            char resp[16];
            snprintf(resp, sizeof(resp), "@%02X FF\n", _addr);
            _send(resp);
        }

    } else if (cmd == 'W') {
        uint8_t data[16];
        uint8_t data_len = 0;
        int pos = 8;
        while (pos < (int)_buf_len && data_len < 16) {
            if (_buf[pos] == ' ') { pos++; continue; }
            if (pos + 1 > (int)_buf_len) break;
            char byte_str[3] = { _buf[pos], _buf[pos + 1], '\0' };
            data[data_len++] = (uint8_t)strtoul(byte_str, nullptr, 16);
            pos += 2;
        }

        for (uint8_t i = 0; i < _write_count; i++) {
            if (_writes[i].reg == reg) {
                _writes[i].fn(data, data_len);
                char resp[16];
                snprintf(resp, sizeof(resp), "@%02X OK\n", _addr);
                _send(resp);
                return;
            }
        }
        if (_default_write) {
            _default_write(reg, data, data_len);
            char resp[16];
            snprintf(resp, sizeof(resp), "@%02X OK\n", _addr);
            _send(resp);
        } else {
            char resp[16];
            snprintf(resp, sizeof(resp), "@%02X ERR\n", _addr);
            _send(resp);
        }
    }
}

void RS485Node::_send(const char* msg) {
    digitalWrite(_de_re_pin, HIGH);
    _serial.print(msg);
    _serial.flush();
    delayMicroseconds(500);
    digitalWrite(_de_re_pin, LOW);
}
