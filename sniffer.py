import socket
import struct

#same args as C's socket()
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
#path goes straight in as a string
sock.connect("/tmp/canbus.sock")
print("sniffer connected")

#we are receving 68 bytes as determined in layout.c
while True:
    data = sock.recv(68)
    #checks if data is empty
    if not data:
        print("hub closed")
        break
    #<: little endian
    #H: can_id
    #B: dlc
    #64s: can_data
    #B: flags
    can_id, dlc, payload, flags = struct.unpack("<HB64sB", data)
    if can_id == 0x100:
        #> is big-endian, h is an signed bit
        #0:6 gets indices 0-5, imu axes are 2 bytes each, first 6 bytes one for each axis
        x, y, z = struct.unpack(">hhh", payload[0:6])
        print(f"IMU x={x/100:.2f} y={y/100:.2f} z={z/100:.2f}")
    elif can_id == 0x500:
        #payload[0] is where the flag byte is on the heartbeat loop
        fb = payload[0]
        #masks result in true or false
        #bit masks in python use decimal conversion bit 2 = 4
        print(f"HB imu={bool(fb & 1)} bme={bool(fb & 2)} err={bool(fb & 4)}")
    else:
        print(f"frame id=0x{can_id:X} dlc={dlc}")
