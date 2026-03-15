# Collaborative Interactive Shell (CIS) - Phase 2

**Team:** Abin Timilsina, Sabin Ghimire, Nuraj Rimal  
**Course:** Operating Systems — Southern Illinois University Edwardsville  
**Date:** February 2026

---

## Overview

CIS is a collaborative terminal sharing system. Multiple users connect to a single server hosting a bash shell. One user (the controller) executes commands while others (observers) watch output in real-time. Users can request control and the system manages transfers using a FIFO queue.

Built with: PTYs (`forkpty`), UNIX domain sockets, `poll()` for I/O multiplexing, `termios` for raw terminal mode.

---

## Architecture

```
┌──────────────────┐
│   CIS Server     │
│   ┌──────────┐   │
│   │   bash   │   │  ← shell running in PTY
│   │  (PTY)   │   │
│   └──────────┘   │
│        ▲         │
│        │ poll()  │
│   ┌────┴─────┐   │
│   │  Socket  │   │  ← UNIX domain socket /tmp/cis.sock
│   └────┬─────┘   │
└────────┼─────────┘
         │
    ┌────┴────┬──────────┬──────────┐
    │         │          │          │
┌───▼───┐ ┌──▼────┐ ┌──▼────┐ ┌──▼────┐
│Client1│ │Client2│ │Client3│ │Client4│
│ (Ctrl)│ │ (Obs) │ │ (Obs) │ │ (Obs) │
└───────┘ └───────┘ └───────┘ └───────┘
```

The server runs a single-threaded `poll()` event loop. It holds the PTY master fd, the listening socket fd, and all client fds in one poll array. PTY output is broadcast to all clients. Only the current controller's input is forwarded to the PTY — observer input is silently dropped.

---

## Building

```bash
# requires gcc and libutil (Linux) or util (macOS)
make

# or manually:
# Linux:
gcc -Wall -Wextra -Isrc -o server src/server.final.c -lutil
gcc -Wall -Wextra -Isrc -o client src/client.final.c

# macOS:
gcc -Wall -Wextra -Isrc -o server src/server.final.c
gcc -Wall -Wextra -Isrc -o client src/client.final.c
```

---

## Usage

**Terminal 1 — start the server:**
```bash
./server
```
```
[Server] CIS server started
[Server] Socket: /tmp/cis.sock
[Server] Waiting for clients...
```

**Terminal 2 — first client (auto-becomes controller):**
```bash
./client
```
```
[CIS] You are the controller. Type commands.
```

**Terminal 3, 4 — additional clients (become observers):**
```bash
./client
```
```
[CIS] Connected as observer. Controller: user1
```

Observers see all output from the controller's commands in real-time.

---

## Controls

| Key | Who | Effect |
|-----|-----|--------|
| Ctrl+T | Observer | Request control — added to FIFO queue |
| Ctrl+R | Controller | Release control — next in queue gets it |
| Ctrl+L | Anyone | List connected users and queue status |
| Ctrl+X | Anyone | Disconnect cleanly |

---

## Known Limitations

- Max 10 clients (`MAX_CLIENTS` in `src/protocol.h`)
- Local only — UNIX sockets do not work over a network
- No authentication or encryption
- No automatic timeout — controller holds until manual Ctrl+R or disconnect
- No window resize propagation (SIGWINCH not handled)
