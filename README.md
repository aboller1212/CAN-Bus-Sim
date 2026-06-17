# CANaryNode Sim

A software CAN bus network that runs entirely on a laptop, no hardware required. It's a set of C programs that pretend to be electronic control units talking over a CAN bus, plus Python tools that sniff and inject traffic the way you would with a real bus analyzer.

I built this to get a head start on the firmware half of a real project (a custom STM32G474 board called CANaryNode that reads an IMU and an environmental sensor and broadcasts the readings over CAN FD) before the hardware arrived, and to drill the concepts that come up in embedded interviews. Everything here is meant to port onto the real firmware later, so the protocol and packing logic are written the way they'd be written on the MCU.

## What it actually emulates

Real CAN is a two wire bus where every node is electrically tied to the same pair of wires. There are no addresses. A node doesn't send "to" anyone, it broadcasts a frame stamped with an ID, and every other node hears it and decides whether it cares. The ID also doubles as priority: when two nodes transmit at once, the one with the lower ID wins, and it wins without corrupting anyone's data because each node listens to the wire while it drives it and backs off the instant it loses.

This project reproduces that behavior in software:

- **The bus** (`bus.c`) is a hub process that every node connects to over a Unix domain socket. When a node sends a frame, the hub copies it out to every connected node. Physically it's a star, but logically it behaves like the shared wire: everyone hears everything, including the sender hearing its own transmission.
- **The nodes** (`node.c`) are separate processes you launch in their own terminals. Each one runs a superloop, transmits IMU data every 100ms and a heartbeat every second on independent software timers, and listens for incoming frames the whole time without blocking.
- **The protocol** (`can_protocol.h` / `can_protocol.c`) defines message IDs by priority, packs fake sensor readings into the payload with fixed point scaling and a chosen byte order, and packs status flags into individual bits.
- **The tooling** is Python. `sniffer.py` connects like any other node and decodes every frame on the bus. `injector.py` sends command frames that change a node's behavior live (stop transmitting, start again, raise an error flag).

## Where the abstractions are

This is a simulation, not the real thing, and it's worth being upfront about what is faithful and what is faked.

**Faithful:**
- Broadcast semantics. Every node sees every frame, the sender included.
- Content addressing. Frames carry a message ID, not a destination, and receivers filter by ID.
- The application protocol. Scaling floats to integers, fixing the byte order on the wire, bit packing flags, and decoding by message ID are all done exactly the way they'd be done on the STM32. These files are meant to compile and run on the real board with little or no change.
- The firmware structure. The node is a non blocking superloop with software timers, which is the shape almost all bare metal firmware takes.

**Faked or abstracted away:**
- **The physical layer.** There are no real CAN_H / CAN_L lines, no dominant and recessive voltages, no transceiver. The "wire" is a relay process passing whole frames over sockets.
- **Arbitration.** Real arbitration happens bit by bit, in real time, as nodes read the bus back while transmitting. You cannot reproduce that when you're passing complete frames over a socket, so this models the outcome instead of the mechanism. The hub collects frames that arrive within a short window, sorts them by ID, and relays them lowest ID first. That gives you the priority ordering you'd see on a real bus, but it's a discrete window approximation, not a cycle accurate one.
- **The hardware managed fields.** Real frames carry a CRC, an ACK slot, start and stop bits, and bit stuffing, all handled by the CAN controller silicon. The firmware never touches those, so the frame struct here doesn't model them either.
- **The transport reliability.** Unix domain stream sockets are reliable and ordered, so frames never get corrupted or lost in this sim. A real noisy bus would, and would rely on the CRC and error frames to catch it.
- **The sensors.** The IMU and environmental readings are simulated. The IMU axes are sine waves so the data looks like a board being moved around. On the real board these come from a BMI270 over SPI and a BME280 over I2C.

One detail worth calling out because it surprised me: a single frame ends up with two different byte orders in it. The frame header rides over the socket as raw struct memory, which is little endian on this machine, but the payload is hand packed big endian on purpose. The sniffer has to read the header one way and the payload the other, which is a good reminder that the transport layer and the application layer don't have to agree on endianness.

## Files

| File | What it is |
|------|-----------|
| `can_frame.h` | The frame struct. The transport shape everything agrees on. |
| `can_protocol.h` / `can_protocol.c` | Message IDs, scaling, flag masks, and the pack/unpack helpers. The part that ports to firmware. |
| `bus.c` | The hub. Accepts connections, batches frames in a window, arbitrates by ID, relays. |
| `node.c` | A node. Superloop, periodic transmit, non blocking receive, reacts to commands. |
| `sniffer.py` | Connects to the bus and decodes all traffic. |
| `injector.py` | Sends command frames to change a node's behavior. |

## Building and running

You need clang and Python 3. macOS or Linux.

```
clang -Wall bus.c -o bus
clang -Wall node.c can_protocol.c -o node -lm
```

Open a few terminals. Start the hub first:

```
./bus
```

Then one or more nodes, each with its own ID:

```
./node 0x100
./node 0x300
```

Watch the traffic with the sniffer:

```
python3 sniffer.py
```

Send commands to change a node's behavior:

```
python3 injector.py stop     # the IMU stream halts, heartbeat keeps going
python3 injector.py start    # the IMU stream resumes
python3 injector.py error    # the heartbeat's error flag flips to true
```

## What's not done yet

- The environmental message (0x300) has an ID assigned but isn't packed or decoded yet. Only IMU, heartbeat, and command messages are wired up end to end.
- The socket path and message IDs are duplicated by hand between the C and Python sides. A real project would generate both from one source so they can't drift.
- Reads and writes assume a whole frame arrives at once. That's safe for small frames on a local socket but not robust in general, since stream sockets don't preserve message boundaries.
- No CRC, bit stuffing, or error frames. That's the planned stretch goal.

## Why it's built this way

The point was never to make a polished CAN library. It was to understand every piece by writing it, so the choices lean toward "do it by hand and see how it works" over "pull in a library." The protocol and packing code is the part that's meant to survive onto the real STM32 board. The bus and the Python tooling are scaffolding that let me exercise and watch that code without waiting for hardware.
