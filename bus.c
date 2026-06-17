#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/select.h>
#include "can_frame.h"
#include <unistd.h>

//Helper function for priority sorting IDs
//count is how many frames currently in the batch
//batch is a pointer to an array of frames
void sort_batch_by_id(can_frame_t *batch, int count) {

    for(int i=1; i<=count-1; i++) {
        can_frame_t key = batch[i];
        int j = i-1;
        while(j>=0 && batch[j].can_id > key.can_id){
            batch[j+1] = batch[j];
            j--;
        }
        batch[j+1] = key;
    }
}

/*
    bus.c is the central 'hub' for our nodes (in an attempt to model a CAN BUS)
    - create the listening socket
    - accept connections
    - relay frames
*/

int main(void) {
    /* 
        socket(domain, type, protocol) 
        domain: AF_UNIX -> local machine, named by filesystem path
        type: SOCK_STREAM -> reliable, ordered, connection-based byte stream protocol: 0 -> For unix domain sockets there's only one protocol for a given type, pass 0

        listen_fd returns a file descriptor - small integer the OS hands us to refer to this socket
    */
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    //listen_fd is -1 if the OS couldn't make the socket
    if(listen_fd == -1) {
        perror("socket"); //perror == "print error"
        exit(EXIT_FAILURE); //immediately end the entire program
    }


    struct sockaddr_un addr; // address struct from sys/un.h
    memset(&addr, 0, sizeof(addr)); // set the entire struct to 0s, prevents any garbage
    addr.sun_family = AF_UNIX; // "interpret this address as a filesystem path not an IP"
    strcpy(addr.sun_path, "/tmp/canbus.sock"); //writes the path string into the structs name field 

    //delets any stale socket file so bind gets a clean anme
    unlink("/tmp/canbus.sock");
    /*
        bind() staples the name /tmp/canbus.sock onto the socket listen_fd
        ^this is so nodes can find it. socket() made an unnamed endpoint; bind() gives it the address
        (struct sockaddr *)&addr: pointer to your filled address struct
        &addr = pointer to my struct; (struct sockaddr *) casts it to the generic type bind expects
    */
    //need to store the bind_result to catch for errors
    int bind_result = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));

    if(bind_result == -1) {
    perror("bind"); 
    exit(EXIT_FAILURE); 
    }

    /*
        listen(): doesn't accept connections - just flips the socket into passive/accepting mode
        accept(): this actually accepts connections
        backlog: how many pending connections the kernel will hold in the waiting line before refusing new ones
    */
    int listen_result = listen(listen_fd, 8);
    if(listen_result == -1) {
    perror("listen"); 
    exit(EXIT_FAILURE); 
    }


    //permanent bag initalization
    fd_set master;
    //zeros out the bag referred to by master
    FD_ZERO(&master);
    //adds listen_fd(front door) to the bag
    FD_SET(listen_fd, &master);
    //track the highest fd: at THIS POINT - listen_fd i the only and highest fd in the bag
    //as we add fds we must update this max_fd variable - this is so select knows how to scan
    int max_fd = listen_fd;

    //maximum amount of frames to be held into a buffer at once
    #define MAX_BATCH 16
    //array of can frames max 16 frames
    can_frame_t batch[MAX_BATCH];
    //current frames in batch
    int batch_count = 0;
    

    while(1) {
        //initialize a new bag
        fd_set read_fds;
        //copy the master into the new bag
        read_fds = master;

        //select timeouts
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;


        /*
            We use the select() function to basically implement this
            system such that the hub will sleep at all times that there is
            not an event on one of the fds.
            There must be a permanent master bag that gets recopied at each loop
            because the select will destroy all non-ready fds

            max_fd+1: how far to scan (highest_fd + 1)
            &read_fds: the scratch bag, watched for readability
            two NULLs: we dont watch write-ready or errors
            last &timeout: points to a 5ms timeout to select
        */
        int sel_result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if(sel_result == -1) {
        perror("select"); 
        exit(EXIT_FAILURE); 
        }
        if(sel_result == 0) {
            if(batch_count > 0) {
                //sorts the batch
                sort_batch_by_id(batch, batch_count);
                //debugging
                printf("--- flush: %d frame(s) ---\n", batch_count);
                for(int i = 0; i < batch_count; i++){
                    printf("  relaying id=0x%X\n", batch[i].can_id);
                    //writes all of the frames in order
                    for(int out_fd = 0; out_fd <= max_fd; out_fd++){
                        if(FD_ISSET(out_fd, &master) && out_fd != listen_fd) {
                            //write moves raw bytes, not size dependent
                            write(out_fd, &batch[i], sizeof(batch[i]));
                        }
                    }
                }
                batch_count=0;
            }
        }

        /*
            for each fd in the range, check each fd is set or not
            check to make sure that the id set is not a new connection
        */
        for (int fd = 0; fd <= max_fd; fd++) {
            if(FD_ISSET(fd, &read_fds)) {
                //checks "is someone knocking at the door"
                //if they are then you give the new node a conn_fd, check for error, adds it to the bag, and checks max_id
                if (fd == listen_fd) {
                    /*
                        -accept(): pulls one connection off the queue and gives you
                        a new fd dedicated to talking only to that specific node.
                        -listen_fd is used to accept more connections, while accept hands a seperate fd (private channel)
                        -The 2 NULLs: accept can optionally fill in who connected
                        for a unix domain socket the client has no meaningful address and we 
                        can pass NULL because we dont care about who connected 
                        returns a new fd for the accepted connection
                    */ 
                    int conn_fd = accept(listen_fd, NULL, NULL);
                    if(conn_fd == -1) {
                    perror("accept"); 
                    exit(EXIT_FAILURE); 
                    }
                    //add new nodes conn_fd to the master set
                    FD_SET(conn_fd, &master);
                    //must update max_fd if the new conn_fd is higher
                    if (conn_fd > max_fd) max_fd = conn_fd;
                }
                else {
                    //initialize the can frame data type as we will be "reading" messages
                    can_frame_t frame;
                    /*
                        ssize_t garunteed to hold enough for any read size
                        must be signed for the 3 different possibilities:
                        >0: run the relay loop
                        0: node hung up, stop watching
                        -1: read error
                    */
                    ssize_t n = read(fd, &frame, sizeof(frame)); 
                    //n>0 case we run the relay loop
                    if (n > 0) {

                        if(batch_count < MAX_BATCH) {
                            batch[batch_count] = frame;
                            batch_count++;
                        }
                    }
                    else if (n==0) {
                        //remove this fd from the master bag so select stops watching it
                        FD_CLR(fd, &master);
                        //release the fd back to the kernel
                        close(fd);
                    }
                    else {
                        perror("read issue"); 
                        exit(EXIT_FAILURE); 
                    }
                }
            }
        }
    }
    
    return 0; 
}