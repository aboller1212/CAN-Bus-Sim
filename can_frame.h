// "if NOT already defined CAN_FRAME_H, keep reading"
#ifndef CAN_FRAME_H
// "now define CAN_FRAME_H"
#define CAN_FRAME_H

// gives you (uint16_t, etc.)
#include <stdint.h>

/*  
    CAN struct to mirror what firmware will see in CAN exchange
    Real CAN frames have fields the hardware manages (ACK, CRC, ...)
    We will NOT model these as the firmware won't see them
    We will model, identifier, DLC, Data, and Flags
*/

/*
    Identifier — the ID we've been obsessing over. Doubles as priority and message identity.
    DLC — data length code. How many data bytes are valid in this frame.
    Data — the payload bytes themselves.
    Flags — a little metadata we need that real hardware encodes structurally: is this an extended (29-bit) or standard (11-bit) ID? Is it a CAN FD frame? We'll fold these into a flags field. 
*/

typedef struct {
    //ID range is (0x000-0x7FF), id is only 11-bits must write constraint in .c file
    uint16_t can_id;
    
    //DLC range is 0-8, we will use uint8_t (0-255) so constraint will be needed in .c file
    uint8_t can_dlc;

    //data: byte type is uint8_t, FDCAN uses 64 bytes, CAN uses 8 bytes
    uint8_t can_data[64];

    /* 
       flags: metadata real hardware encodes structurally but our struct will hae to carry explicitly
       bitfield of frame flags (FD, extended ID, ...) one bit per flag
    */
    uint8_t can_flags;

} can_frame_t;

//closes the #ifndef
#endif