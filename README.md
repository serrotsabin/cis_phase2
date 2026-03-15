# Collaborative Interactive Shell (CIS) - Phase 2

**Team:** Abin Timilsina, Sabin Ghimire, Nuraj Rimal  
**Course:** Operating Systems  
**Institution:** Southern Illinois University Edwardsville  
**Date:** February 2026

---

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Architecture](#architecture)
4. [Building](#building)
5. [Usage](#usage)
6. [Controls](#controls)
7. [File Structure](#file-structure)
8. [Design Decisions](#design-decisions)
9. [Testing](#testing)
10. [Known Limitations](#known-limitations)
11. [Platform](#platform)
12. [References](#references)

---

## Overview

CIS is a collaborative terminal sharing system that allows multiple users to observe and control a shared bash session. The system uses pseudo-terminals (PTYs), UNIX domain sockets, and event-driven I/O multiplexing to enable real-time collaboration with fair floor control.

**Key Concept:** Multiple users connect to a single server hosting a bash shell. One user (the controller) can execute commands, while others (observers) watch the output in real-time. Users can request control, and the system manages transfers using a FIFO queue.

---

## Features

### Core Functionality
**Multi-client support** - Up to 10 simultaneous users  
**Real-time broadcasting** - All users see output instantly  
**PTY-based shell** - Full bash compatibility (pipelines, job control, etc.)  
**Observer mode** - Watch sessions without interfering

### Floor Control
**FIFO request queue** - Fair, first-come-first-served control requests  
**Auto-transfer on disconnect** - Seamless control handoff when controller leaves  
**Manual control release** - Controller can voluntarily give up control  
**Queue position feedback** - Users see their place in line

### User Experience
**User management** - List connected users and queue status  
**Clean disconnect** - Proper cleanup on exit (Ctrl+X)  
**Role notifications** - Clear messages about controller/observer status  
**Input filtering** - Observers' keystrokes ignored (except control commands)

---

## Architecture

```
┌──────────────────┐
│   CIS Server     │
│   ┌──────────┐   │
│   │   bash   │   │  ← Shell running in PTY
│   │  (PTY)   │   │
│   └──────────┘   │
│        ▲         │
│        │ poll()  │
│   ┌────┴─────┐   │
│   │  Socket  │   │  ← UNIX domain socket
│   └────┬─────┘   │
└────────┼─────────┘
         │
    ┌────┴────┬─────────┬─────────┐
    │         │         │         │
┌───▼───┐ ┌──▼───┐ ┌──▼───┐ ┌──▼───┐
│Client1│ │Client2│ │Client3│ │Client4│
│ (Ctrl)│ │ (Obs) │ │ (Obs) │ │ (Obs) │
└───────┘ └───────┘ └───────┘ └───────┘
```

### Components

**Server (`server_v2.c`):**
- Creates PTY and launches bash shell via `forkpty()`
- Listens for client connections on UNIX socket
- Manages control queue (FIFO)
- Broadcasts output to all connected clients
- Handles control requests/releases
- Manages client lifecycle (connect/disconnect)

**Client (`client.c`):**
- Connects to server via UNIX socket
- Displays PTY output in real-time
- Captures user keystrokes in raw mode
- Sends control commands to server
- Provides escape sequence interface (Ctrl+T, Ctrl+R, etc.)

**Protocol (`protocol.h`):**
- Data structure definitions
- Message type constants
- Configuration parameters (MAX_CLIENTS, socket path, etc.)

### Key Technologies

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Shell Hosting** | PTY (pseudo-terminal) via `forkpty()` | Run bash in controlled environment |
| **IPC** | UNIX domain sockets (`AF_UNIX`) | Fast local client-server communication |
| **I/O Multiplexing** | `poll()` system call | Handle multiple file descriptors efficiently |
| **Terminal Control** | `termios` (raw mode) | Immediate keystroke capture without buffering |
| **Event Loop** | Single-threaded, non-blocking | Simple, deterministic concurrency model |

---

## Building

### Prerequisites
- **OS:** Linux (tested on Fedora 43)
- **Compiler:** GCC
- **Libraries:** Standard C library, `libutil` (for PTY functions)

### Compilation

```bash
# Clean build
make clean

# Compile server and client
make

# Or compile individually
gcc -Wall -Wextra -o server server_v2.c -lutil
gcc -Wall -Wextra -o client client.c
```

### Output
- `server` - Server executable
- `client` - Client executable

---

## Usage

### Starting a Session

**Terminal 1 - Start Server:**
```bash
./server
```

Output:
```
[Server] CIS server started
[Server] Socket: /tmp/cis.sock
[Server] Waiting for clients...
```

**Terminal 2 - First Client (Becomes Controller):**
```bash
./client
```

Output:
```
[Client] Connected to CIS session
[Client] Controls:
  Ctrl+T : Request control
  Ctrl+R : Release control
  Ctrl+L : List users
  Ctrl+X : Quit

[CIS] You are the controller. Type commands.
```

Now you can type shell commands:
```bash
$ ls
file1  file2  file3

$ pwd
/home/user/cis_phase2

$ whoami
user
```

**Terminal 3 - Second Client (Observer):**
```bash
./client
```

Output:
```
[CIS] Connected as observer. Controller: user1
```

Client 2 sees all output from Client 1's commands but cannot type commands themselves.

---

## Controls

### Keyboard Shortcuts

| Key | Function | Who Can Use | Effect |
|-----|----------|-------------|--------|
| **Ctrl+T** | Request control | Observers only | Adds user to FIFO control queue |
| **Ctrl+R** | Release control | Controller only | Gives up control, next in queue granted |
| **Ctrl+L** | List users | Anyone | Shows connected users and queue status |
| **Ctrl+X** | Quit session | Anyone | Cleanly disconnects from server |

### Control Flow Example

```
1. Alice is controller, Bob and Carol are observers
2. Bob presses Ctrl+T → "Position in queue: 1"
3. Carol presses Ctrl+T → "Position in queue: 2"
4. Alice presses Ctrl+R → Bob auto-granted control
5. Bob can now type commands
6. Carol is still waiting (position 1 in queue)
7. Bob presses Ctrl+R → Carol auto-granted control
8. Carol can now type commands
```

---

## File Structure

```
cis_phase2/
├── server_v2.c           # Server implementation (PTY + socket + control)
├── client.c              # Client implementation (terminal interface)
├── protocol.h            # Shared constants and data structures
├── Makefile              # Build system
├── README.md             # This file
├── demo_transcript.txt   # Demonstration session transcript
├── POSTMORTEM.md         # Design reflections and bug analysis
├── logs/
│   └── trace_session1.log  # Example trace log
└── tests/
    ├── test_automated.sh     # Automated test suite
    └── test_manual.sh        # Manual testing guide
```

---

## Design Decisions

### 1. Floor Control: FIFO Queue

**Choice:** First-In-First-Out (FIFO) queue for control requests

**Rationale:**
- **Fairness:** All users treated equally, no priority
- **Predictability:** Users can estimate wait time based on position
- **Simplicity:** Easy to implement correctly, easy to understand
- **No starvation:** Every request eventually served

**Alternative considered:** Round-robin with time slices
- Rejected because it adds complexity and interrupts long-running tasks

### 2. Single-Threaded Event Loop

**Choice:** Use `poll()` in a single thread instead of multi-threading

**Rationale:**
- **Simpler:** No mutexes, race conditions, or deadlocks
- **Deterministic:** Same inputs → same outputs (easier to debug)
- **Sufficient performance:** For 10 clients, single thread is fast enough
- **Easier state management:** No shared state between threads

**Trade-off:** Cannot use all CPU cores, but not needed for this workload

### 3. In-Band Control Sequences

**Choice:** Use special keypresses (Ctrl+T, Ctrl+R, etc.) for control commands

**Rationale:**
- **No separate channel needed:** Control and data on same socket
- **Simple protocol:** No message framing required
- **Natural user interface:** Familiar Ctrl+key shortcuts

**Limitation:** Some Ctrl+keys conflict with terminal functions (had to avoid Ctrl+Q, Ctrl+D)

### 4. UNIX Domain Sockets

**Choice:** Use UNIX domain sockets instead of TCP sockets

**Rationale:**
- **Performance:** Faster than TCP for local communication
- **Security:** Filesystem permissions control access
- **Simplicity:** No need for IP address/port management
- **Appropriate scope:** CIS is designed for local collaboration

**Limitation:** Cannot connect from remote machines (acceptable for Phase 2)

---

## Testing

### Automated Tests

Run basic functionality tests:

```bash
./tests/test_automated.sh
```

Tests:
- Server startup and socket creation
- Multiple client connections
- First client becomes controller
- Subsequent clients become observers
- Broadcasting works correctly

Expected output:
```
Server started (PID: 12345)
Socket created
Client 1 became controller
Client 2 connected as observer
Client 3 connected as observer
Commands executed and output received
```

### Manual Tests

Interactive testing guide:

```bash
./tests/test_manual.sh
```

Follow on-screen instructions to test:
- Floor control requests (Ctrl+T)
- Control release (Ctrl+R)
- User listing (Ctrl+L)
- Auto-transfer on disconnect
- Observer input filtering
- Clean quit (Ctrl+X)

### Test Scenarios

**Scenario 1: Basic Collaboration**
1. Start server, connect 3 clients
2. Controller types commands
3. Verify all clients see output

**Scenario 2: Control Transfer**
1. Observer presses Ctrl+T
2. Controller presses Ctrl+R
3. Verify observer becomes controller
4. Verify new controller can type commands

**Scenario 3: Disconnect Handling**
1. 3 clients connected, user2 is controller
2. user3 requests control (in queue)
3. Kill user2 (Ctrl+X or kill process)
4. Verify user3 auto-granted control

**Scenario 4: Observer Input Filtering**
1. Observer tries to type commands
2. Verify server ignores input
3. Verify only controller input goes to shell

---

## Known Limitations

### Phase 2 Limitations
- **Maximum 10 clients** (`MAX_CLIENTS` constant)
- **No authentication** - anyone with filesystem access can connect
- **No encryption** - data sent in plaintext (acceptable for local sockets)
- **Local only** - UNIX sockets don't work over network
- **No session persistence** - disconnecting loses history
- **No timeout** - controller can hold control indefinitely (manual release only)

### Platform-Specific Notes
- **macOS:** Uses `#include <util.h>` for PTY functions
- **Linux:** Uses `#include <pty.h>` for PTY functions
- **Deployment:** Tested on home.cs.siue.edu (Fedora Linux 43)

### Future Enhancements (Phase 3 Candidates)
- Automatic timeout for idle controllers
- Session recording and replay
- TCP sockets for network access
- User authentication (PAM or simple password)
- Multiple concurrent sessions
- Window resize propagation (SIGWINCH)
- Better error recovery

---

## Platform

### Development Environment
- **OS:** macOS (development), Fedora Linux 43 (deployment)
- **Shell:** GNU bash 5.3.0
- **Compiler:** GCC 15.2.1 (Red Hat)
- **Architecture:** x86_64

### Deployment Platform
- **Server:** home.cs.siue.edu
- **OS:** Fedora Linux 43 (Workstation Edition)
- **Kernel:** 6.17.12-300.fc43.x86_64
- **Hardware:** Intel Xeon Gold 5118 @ 2.30GHz, 125GB RAM

### Tested Configurations
Fedora Linux 43 (primary platform)  
macOS (development only)  
Ubuntu 24.04 (confirmed compatible)

---

## References

### Man Pages
- `man 3 forkpty` - Create pseudo-terminal and fork
- `man 2 poll` - I/O multiplexing with file descriptors
- `man 3 termios` - Terminal I/O control
- `man 7 unix` - UNIX domain sockets
- `man 2 socket` - Socket creation
- `man 2 bind` - Bind socket to address
- `man 2 accept` - Accept incoming connections

### Documentation
- [Advanced Programming in the UNIX Environment (APUE)](https://www.apuebook.com/) - Stevens & Rago
- [The Linux Programming Interface](https://man7.org/tlpi/) - Michael Kerrisk
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)

### Related Projects
- **tmux** - Terminal multiplexer (different approach: multiplexes terminals, not shells)
- **screen** - Terminal session manager (similar goals, different architecture)
- **tmate** - Terminal sharing via SSH (network-based alternative)

---

## Authors

### Team Members
- **Abin Timilsina** - Core server implementation, PTY integration
- **Sabin Ghimire** - Floor control protocol, queue management
- **Nuraj Rimal** - Client implementation, testing, documentation

### Contact
For questions about this project, contact the development team through the course instructor.

---

## License

Educational project for CS coursework at Southern Illinois University Edwardsville.  
Not intended for production use or commercial distribution.

---

## Acknowledgments

- Professor and TAs for project guidance
- APUE and TLPI books for PTY and socket examples
- Stack Overflow community for debugging assistance

---

**Last Updated:** February 13, 2026  
**Version:** Phase 2 Submission  
**Status:** Complete and tested
