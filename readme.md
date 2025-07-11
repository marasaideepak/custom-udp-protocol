# KTP (KGP Transport Protocol) Implementation

Implementation of a reliable transport protocol (KTP) over unreliable UDP.

## Files Overview

- `ksocket.h`           - Header file containing data structures and function declarations
- `ksocket.c`           - Implementation of the KTP socket
- `initksocket.c`       - Program to initialize the KTP system (Thread R, Thread S, Thread G)
- `user1.c`             - Test sender program 
- `user2.c`             - Test receiver program
- `Makefile`            - Main makefile
- `Makefile.lib`        - Makefile for building the libksocket.a library
- `Makefile.init`       - Makefile for building the initksocket executable
- `Makefile.user`       - Makefile for building the user1 and user2 executables
- `documentation.txt`   - Documentation of data structures, functions, and performance results

## Running Process

To build all components, run:

```bash
make
```

This will compile:
1. The KTP library (libksocket.a)
2. The initialization program (initksocket)
3. The test programs (user1 and user2)

## Running the Tests

### Step 1: Initialize the KTP system

First, start the KTP system:

```bash
./initksocket
```

Keep this running in a terminal.

### Step 2: Start the receiver

In a new terminal, start the receiver:

```bash
./user2 127.0.0.1 8001 127.0.0.1 8000
```

This command binds the receiver to:
- Local IP: 127.0.0.1, Port: 8001
- Remote IP: 127.0.0.1, Port: 8000

### Step 3: Start the sender

In another terminal, start the sender:

```bash
./user1 127.0.0.1 8000 127.0.0.1 8001
```

This command binds the sender to:
- Local IP: 127.0.0.1, Port: 8000
- Remote IP: 127.0.0.1, Port: 8001

The sender will transfer a 100KB file (input.txt) and transfer it to the receiver using the KTP protocol.

## Testing Multiple Socket Pairs

You can run multiple instances of user1 and user2 with different port numbers to test multiple KTP socket pairs:

```bash
# Terminal 1 (receiver 1)
./user2 127.0.0.1 8001 127.0.0.1 8000

# Terminal 2 (sender 1)
./user1 127.0.0.1 8000 127.0.0.1 8001

# Terminal 3 (receiver 2)
./user2 127.0.0.1 8003 127.0.0.1 8002

# Terminal 4 (sender 2)
./user1 127.0.0.1 8002 127.0.0.1 8003
```

## Adjusting Parameters

You can modify the following parameters in `ksocket.h`:
- `N`: Maximum number of active KTP sockets (default: 10)
- `T`: Timeout value in seconds (default: 5)
- `P`: Default message drop probability (default: 0.05)

## Shutting Down

To stop the KTP system, press Ctrl + C in the terminal running initksocket. This will clean up all IPC resources at exits...