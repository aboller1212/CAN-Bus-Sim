import socket
import struct
import sys

CAN_ID_CMD = 0x010
CMD_STOP_TX = 1
CMD_START_TX = 2
CMD_SET_ERROR = 3


result = {"stop": CMD_STOP_TX, "start": CMD_START_TX, "error": CMD_SET_ERROR}
#no command word given <2
#word typed that isnt one of our keys

if len(sys.argv) < 2 or sys.argv[1] not in result:
    print("usage: python3 injector.py [stop|start|error]")
    sys.exit(1)
#look up the word to get the command code
code = result[sys.argv[1]]

#connect to the hub
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect("/tmp/canbus.sock")
print("injector connected")

#the data field: 64 bytes with command code in byte 0
data = bytes([code]) + bytes(63)

#pack the command little endian
frame_bytes = struct.pack("<HB64sB", CAN_ID_CMD, 1, data, 0)
#send it to the hub
sock.send(frame_bytes)
