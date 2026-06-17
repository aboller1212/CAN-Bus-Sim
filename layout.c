#include <stdio.h>
#include <stddef.h>
#include "can_frame.h"

//for python sniffer: need to determine how large the bits are to pack/unpack
/*
    Result:
    alexboller@Alexs-MacBook-Pro canarynode-sim % ./layout
    sizeof = 68
    can_id   offset 0
    can_dlc  offset 2
    can_data offset 3
    can_flags offset 67
*/

int main(void) {
    printf("sizeof = %zu\n", sizeof(can_frame_t));
    printf("can_id   offset %zu\n", offsetof(can_frame_t, can_id));
    printf("can_dlc  offset %zu\n", offsetof(can_frame_t, can_dlc));
    printf("can_data offset %zu\n", offsetof(can_frame_t, can_data));
    printf("can_flags offset %zu\n", offsetof(can_frame_t, can_flags));
    return 0;
}