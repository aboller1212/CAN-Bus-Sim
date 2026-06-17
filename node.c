#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "can_frame.h"
#include <sys/select.h>
#include <sys/time.h>
#include "can_protocol.h"
#include <math.h>

//helper function which asks what time is it now in milliseconds
uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
    - a node is a seperate program (its own process), run in its own terminal
    - launch several copies to simulate multiple ECUs on the bus
    - node connects to the hub rather than accepting connections
*/

//argc: arg count - how many arguments, including the program name itself
//argv: array of strings. argv[0] is always the program name
int main(int argc, char **argv) {
    //need at least one program name + one arg
    if(argc < 2) {
        //usage error messages go to stderr not stdout
        fprintf(stderr, "usage: %s <can_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    //strtol returns a long - we are only storing 11 bits we need to narrow it
    uint16_t my_id = (uint16_t)strtol(argv[1], NULL, 0);
    if(my_id > 0x7FF) {
        fprintf(stderr, "id 0x%X out of range (max 0x7FF)\n", my_id);
        exit(EXIT_FAILURE);
    }

    /*
        create a new socket, for the can node, initialize it and but it in the canbus path
    */
    int can_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(can_fd == -1) {
    perror("node_socket"); 
    exit(EXIT_FAILURE); 
    }
    struct sockaddr_un addr; // address struct from sys/un.h
    memset(&addr, 0, sizeof(addr)); // set the entire struct to 0s, prevents any garbage
    addr.sun_family = AF_UNIX; // "interpret this address as a filesystem path not an IP"
    strcpy(addr.sun_path, "/tmp/canbus.sock"); //writes the path string into the structs name field 
    
    /*
        connect() connects each node to the path whereas bind() makes the bus reachable by that path name
    */
    int conn_result = connect(can_fd, (struct sockaddr *)&addr, sizeof(addr));
    if(conn_result == -1) {
    perror("node_connect"); 
    exit(EXIT_FAILURE); 
    }
    
    //records the time of the last transmit, so the loop can measure how long until the next one
    uint64_t last_tx = now_ms();
    //heartbeat timer
    uint64_t last_hb = now_ms();

    // 1 = transmitting, 0 = stopped
    int tx_enabled =1;
    // 1 = error flag should be reported
    int error_set = 0;

    while (1) {
        //declares the 'bag' of fds select will watch (the node only watches 1 : can_fd)
        fd_set read_fds;
        //empties the bag before use
        FD_ZERO(&read_fds);
        //puts the can_fd (hub connection) into the bag, telling watch for incoming frames
        FD_SET(can_fd, &read_fds);

        //set the fields of timeout so that it is equal to 100ms
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; //100ms
        
        //sel>0: read frame
        //sel==-1: broke and bail
        //sel==0: nothing
        int sel = select(can_fd+1, &read_fds, NULL, NULL, &timeout); // waits up to 100ms for a frame
        if (sel == -1) { 
            perror("node_select"); 
            exit(EXIT_FAILURE); 
        }
        if (sel > 0) {
            can_frame_t in = {0};
            //read the bytes to the hub, also gives in its 'id'
            ssize_t r = read(can_fd, &in, sizeof(in));
            //r>0 means 
            if (r > 0) {
                //if the id received is the imu one then we do this
                if (in.can_id == CAN_ID_IMU) {
                    int16_t x = unpack_int16_be(&in.can_data[0]);
                    int16_t y = unpack_int16_be(&in.can_data[2]);
                    int16_t z = unpack_int16_be(&in.can_data[4]);
                    printf("IMU id=0x%X  x=%.2f y=%.2f z=%.2f (g)\n",
                        in.can_id,
                        x / (double)IMU_ACCEL_SCALE,
                        y / (double)IMU_ACCEL_SCALE,
                        z / (double)IMU_ACCEL_SCALE);
                } else if (in.can_id == CAN_ID_HEARTBEAT) {
                    uint8_t flags = in.can_data[0];
                    printf("HB id=0x%X  imu=%d bme=%d err=%d\n",
                        in.can_id,
                        (flags & HB_FLAG_IMU_OK) != 0,
                        (flags & HB_FLAG_BME_OK) != 0,
                        (flags & HB_FLAG_ERROR)  != 0);
                } else if (in.can_id == CAN_ID_CMD) { //check for the command id
                    uint8_t code = in.can_data[0]; //read the command code
                    printf("CMD received: code=%d\n", code);
                    switch (code) {
                        case CMD_STOP_TX:
                            tx_enabled = 0;
                            break;
                        case CMD_START_TX:
                            tx_enabled = 1;
                            break;
                        case CMD_SET_ERROR:
                            error_set = 1;
                            break;
                    }
                }
                else {
                    //not an IMU message, prints the "frame id =___ dlc = ___"
                    printf("frame id=0x%X dlc=%d\n", in.can_id, in.can_dlc);
                }
            } else if (r == 0) {
                printf("hub closed connection\n");
                break;
            }
        }

        //transmit: indepdent of receive - fire every 100ms
        if ((now_ms() - last_tx >= 100) && tx_enabled) {
            can_frame_t out = {0};
            //assign the frame to the IMU
            out.can_id = my_id;
            //3 values at 2 bytes each (int16_t is 2 bytes) - 6 byte
            out.can_dlc = 6;
            //alternating example values
            double t = now_ms() / 1000.0;
            int16_t x = (int16_t)(sin(t) * IMU_ACCEL_SCALE);
            int16_t y = (int16_t)(sin(t * 1.5) * IMU_ACCEL_SCALE);
            int16_t z = (int16_t)((1.0 + sin(t * 0.5)) * IMU_ACCEL_SCALE);
            //pack the data big endian style
            pack_int16_be(&out.can_data[0], x);
            pack_int16_be(&out.can_data[2], y);
            pack_int16_be(&out.can_data[4], z);
            //send can message to hub
            ssize_t n = write(can_fd, &out, sizeof(out));
            if (n== -1) {
                perror("node_write"); 
                exit(EXIT_FAILURE); 
            }
            
            last_tx = now_ms(); //reset the timer
        }

        if(now_ms() - last_hb >= 1000) {
            //8-byte flags variable
            uint8_t flags = 0;

            if(error_set){
                flags |= HB_FLAG_ERROR;
            }

            //set the flags
            flags |= HB_FLAG_IMU_OK;
            flags |= HB_FLAG_BME_OK;
            //flags |= HB_FLAG_ERROR;

            //the frame for the heartbeat
            can_frame_t out_hb = {0};
            out_hb.can_id = CAN_ID_HEARTBEAT;
            out_hb.can_dlc = 1;
            //each space in can_data is an 8-bit space, flags fits perfectly
            //different than the 16-bit packing we had to do 
            out_hb.can_data[0] = flags;

            ssize_t n = write(can_fd, &out_hb, sizeof(out_hb));
            if (n == -1) { 
                perror("node_write_hb"); exit(EXIT_FAILURE); 
            }

            last_hb = now_ms();

        }



    }

    return 0;
}
