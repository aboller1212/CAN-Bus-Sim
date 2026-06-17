#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdint.h>


#define CAN_ID_CMD    0x010    //three codes below get inserted into, making it the highest priority
#define CMD_STOP_TX   1   // value that means "stop"
#define CMD_START_TX  2   // value that means "start"
#define CMD_SET_ERROR 3   // value that means "set error"


//Three 'fake' IDs mocking the IMU the Enviornmental Sensor and heartbeat
//Lower ID means higher priority
#define CAN_ID_IMU     0x100
#define CAN_ID_ENV     0x300
#define CAN_ID_HEARTBEAT 0x500

//real value (g) x 100 -> int16_t; the reciver divides by 100
#define IMU_ACCEL_SCALE 100 

//heartbeat flags, defining masks to be used, imu bit0, bme bit1, error bit2
#define HB_FLAG_IMU_OK   (1 << 0)
#define HB_FLAG_BME_OK   (1 << 1)
#define HB_FLAG_ERROR    (1 << 2)

// multi-byte values are big-endian: high byte first
//buf: where to write the two bytes, pointer to a spot in the can_data array
//value: the number you want to pack
void pack_int16_be(uint8_t *buf, int16_t value);

//here is the unpacking function
int16_t unpack_int16_be(const uint8_t *buf);

#endif
