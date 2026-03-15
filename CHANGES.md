# Changes from Phase 1 Proposal

This document describes differences between the Phase 1 proposal and the Phase 2 implementation.

## Implementation Matches Proposal

The following features were implemented exactly as proposed:

**Floor Control Protocol**
- FIFO queue for control requests
- Ctrl+T to request control
- Ctrl+R to release control
- Automatic grant when no controller
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

**User Interface**
- ./server and ./client commands
- Socket at /tmp/cis.sock
- Ctrl+L to list users
- Role notifications (controller/observer)

---

## Changes from Proposal

### 1. Quit Key: Ctrl+Q → Ctrl+X

**Proposal:** Ctrl+Q for quit  
**Implementation:** Ctrl+X for quit

**Reason:**
- Ctrl+Q conflicts with XON flow control in UNIX terminals
- Ctrl+Q (ASCII 17) is used for resuming output after Ctrl+S
- Testing revealed the conflict during development
- Ctrl+X has no standard terminal function, making it safe to use

**Impact:** None - functionality identical, just different key

---

### 2. Timeout Mechanism: Not Implemented in Phase 2

**Proposal:** 
> "If no input is received from the controller within a defined interval (e.g., 5 minutes), control is automatically revoked."

**Implementation:** Manual release only (Ctrl+R)

**Reason:**
- Phase 2 focuses on core floor control protocol
- Manual release proves the FIFO queue mechanism works
- Timeout adds complexity (timer management, idle detection)
- Planned for Phase 3 enhancement

**Impact:**
- Controllers must manually release control
- Other users can request and queue, but must wait for release
- System still fair (FIFO), just requires manual action

**Future Work:**
```c
// Phase 3 implementation sketch:
#include <time.h>
time_t control_start_time;
#define CONTROL_TIMEOUT 300  // 5 minutes

// In main loop:
if (current_controller >= 0) {
    if (time(NULL) - control_start_time > CONTROL_TIMEOUT) {
        release_control(current_controller);
    }
}
```

---

### 3. Host Kick (Ctrl+K): Not Implemented

**Proposal:** 
> "Host Kick (Design Only): The host can forcibly revoke control from a participant using an administrative command."

**Implementation:** Not included

**Reason:**
- Labeled "Design Only" in proposal (optional)
- Focus on peer-to-peer fairness (FIFO queue)
- No "host privilege" in Phase 2 - all users equal
- Would require identifying which client is host
- Adds complexity to permission model

**Impact:**
- All users have equal privileges
- No way to forcibly remove a controller
- Relies on manual release or disconnect

**Future Work:**
- Could track first client as "host"
- Add host-only commands (Ctrl+K for kick)
- Implement admin role separate from controller role

---

## Additional Features Not in Proposal

### Enhanced Error Messages

**Implementation includes:**
- "Already in queue" notification when requesting twice
- "Already have control" notification when controller requests
- "Queue position: N" feedback when requesting control
- Detailed server logs showing all events

**Benefit:** Better user experience, easier debugging

---

## Section 2.5 Clarification: Fairness Policy

**Proposal mentioned both:**
- "FIFO queue with optional time limits"
- Timeout mechanism

**Implementation clarifies:**
- **Phase 2:** Pure FIFO, no time limits
- **Phase 3:** FIFO + timeout (optional enhancement)

This separation allows Phase 2 to focus on proving FIFO correctness without timeout complexity.

---

## Summary

**Core functionality:** 100% match with proposal  
**Control protocol:** Exactly as specified  
**OS mechanisms:** Exactly as specified  
**Minor changes:** 
- Ctrl+Q → Ctrl+X (technical necessity)
- No timeout yet (phased approach)
- No host kick yet (optional feature)

**All changes are:**
- Documented in this file
- Explained in POSTMORTEM.md
- Listed in README.md
- Justified by technical or scope reasons

**Phase 2 successfully implements all required features for collaborative shell with fair floor control.**
