#include "can_protocol.h"

void pack_int16_be(uint8_t *buf, int16_t value) {
    uint16_t u = (uint16_t) value;

    //this u>>8 turns for example 0x1234 -> 0x12 - 34 is shifted right 
    buf[0] = (u >> 8) & 0xFF; //high byte FIRST
    buf[1] = u & 0xFF; //low byte
}

//we need to unpack the big endian packing so we can read and output it
//WE ARE ONLY READING BUFFER NOT WRITING
int16_t unpack_int16_be(const uint8_t *buf) {
    //buf[0] is the high byte (0x00 for my x) shifts it back to the high position -> 0x0000
    // | buf[1] ORs in the low byte 0x64 -> 0x0064
    uint16_t u = ((uint16_t) buf[0] << 8 | buf[1]);
    //reinterprets as signed value
    return (int16_t) u;
}
