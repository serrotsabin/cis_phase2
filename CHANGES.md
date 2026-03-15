# Changes from Phase 1 Proposal

## What Matches the Proposal

**Floor Control Protocol**
- FIFO queue for control requests
- Ctrl+T to request control, Ctrl+R to release
- Automatic grant when no controller exists
- Auto-transfer on controller disconnect

**Multi-Client Support**
- Multiple observers (N ≥ 1, max 10)
- Real-time output broadcasting
- Observer input filtering

**OS Mechanisms**
- PTY creation with forkpty()
- UNIX domain sockets
- poll() for I/O multiplexing
- Raw terminal mode (termios)
- Single-threaded event loop

---

## Changes from Proposal

### 1. Quit Key: Ctrl+Q → Ctrl+X

Ctrl+Q (ASCII 17) conflicts with XON flow control — it resumes output after Ctrl+S and cannot be reliably intercepted. We discovered this during testing and switched to Ctrl+X which has no standard terminal function. Functionality is identical, just a different key.

### 2. Timeout Mechanism: Not Implemented

The proposal described auto-revoking control after 5 minutes of idle. We deferred this — manual release with Ctrl+R is sufficient to prove the FIFO queue works correctly, and adding timer management would have complicated the event loop without changing the core behavior. Controllers must manually release control.

### 3. Host Kick: Not Implemented

The proposal marked this as "Design Only" (optional). We kept all users equal — no host privilege, no forcible removal. The system relies on voluntary release or disconnect.

---

## Additional Features Not in Proposal

- "Already in queue at position N" notification when a user requests control twice
- "You already have control" notification when controller presses Ctrl+T
- Ctrl+L to list connected users and queue status

---

## Summary

| Item | Status |
|------|--------|
| Core floor control (FIFO queue) | Matches proposal |
| Multi-client broadcasting | Matches proposal |
| OS mechanisms (PTY, poll, sockets) | Matches proposal |
| Ctrl+Q → Ctrl+X | Changed (XON conflict) |
| Timeout mechanism | Deferred (scope) |
| Host kick | Not implemented (was optional) |
