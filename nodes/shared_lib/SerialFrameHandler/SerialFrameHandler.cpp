#include "SerialFrameHandler.h"
#include <string.h>
#include <stdlib.h>

SerialFrameHandler::SerialFrameHandler(uint8_t addr)
    : _addr(addr), _read(nullptr), _write(nullptr), _plain(nullptr), _buf_len(0)
{}

void SerialFrameHandler::setDefaultReadHandler(DefaultReadHandler fn)  { _read  = fn; }
void SerialFrameHandler::setDefaultWriteHandler(DefaultWriteHandler fn) { _write = fn; }
void SerialFrameHandler::setPlainTextHandler(PlainTextHandler fn)       { _plain = fn; }

void SerialFrameHandler::poll(Stream& s) {
    while (s.available()) {
        char c = (char)s.read();
        if (c == '\n' || c == '\r') {
            _buf[_buf_len] = '\0';
            if (_buf_len > 0 && _buf[0] == '@')
                _processFrame(s);
            else if (_buf_len > 0 && _plain)
                _plain(_buf, s);
            _buf_len = 0;
        } else if (_buf_len < 63) {
            _buf[_buf_len++] = c;
        }
    }
}

// Frame: "@{ADDR:02X} R {REG:02X}"  or  "@{ADDR:02X} W {REG:02X} {B0} ..."
void SerialFrameHandler::_processFrame(Stream& s) {
    if (_buf_len < 8) return;

    char addr_str[3] = { _buf[1], _buf[2], '\0' };
    if ((uint8_t)strtoul(addr_str, nullptr, 16) != _addr) return;

    if (_buf[3] != ' ') return;
    char cmd = _buf[4];
    if (_buf[5] != ' ') return;

    char reg_str[3] = { _buf[6], _buf[7], '\0' };
    uint8_t reg = (uint8_t)strtoul(reg_str, nullptr, 16);

    if (cmd == 'R') {
        uint8_t val = _read ? _read(reg) : 0xFF;
        char resp[16];
        snprintf(resp, sizeof(resp), "@%02X %02X\n", _addr, val);
        s.print(resp);

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
        if (_write) _write(reg, data, data_len);
        char resp[16];
        snprintf(resp, sizeof(resp), "@%02X OK\n", _addr);
        s.print(resp);
    }
}
