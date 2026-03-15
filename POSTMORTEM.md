# CIS Phase 2 - Postmortem

**Team Members:** Abin Timilsina, Sabin Ghimire, Nuraj Rimal  
**Date:** February 13, 2026  
**Project:** Collaborative Interactive Shell (CIS) - Phase 2 Implementation

---

## 1. Hardest Bug Encountered

### Bug Description
The most challenging bug we encountered was **controller input not being forwarded to the PTY** when multiple clients were connected. The server would accept client connections and broadcast PTY output correctly, but keystrokes from the controller client were silently ignored. Commands typed by the controller would simply disappear without execution or error messages.

### Reproduction Steps
1. Start server: `./server`
2. Connect client 1 (becomes controller): `./client`
3. Connect client 2 (becomes observer): `./client`
4. In client 1, type command: `ls`
5. **Expected:** Command executes, output appears in all terminals
6. **Actual:** Nothing happens, no output, no error

### Root Cause
The issue was in the `poll()` file descriptor mapping logic. We were using incorrect array index arithmetic to map poll array positions to client indices. The bug occurred because we built the poll array dynamically (skipping inactive clients), but then used static arithmetic to determine which client corresponded to each poll event.

**Broken code:**
```c
// Build poll array
for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active) {
        fds[nfds].fd = clients[i].fd;
        fds[nfds].events = POLLIN;
        nfds++;
    }
}

// Process events - BUG HERE!
for (int i = 2; i < nfds; i++) {
    if (fds[i].revents & POLLIN) {
        int client_idx = i - 2;  // WRONG!
        // This assumes poll array index maps directly to client array index
        // But if clients[1] is inactive, client[2] is at poll index 2,
        // and we'd calculate client_idx = 0 (wrong!)
    }
}
```

The problem: When we had active clients in slots 0, 2, and 4 (with 1 and 3 inactive), the poll array would contain indices [0, 2, 4] but our calculation would produce [0, 1, 2], causing complete mismatch.

### Fix
We introduced a **dynamic mapping array** built alongside the poll array that explicitly tracked which client corresponded to each poll file descriptor:

```c
// Build mapping as we construct poll array
int client_poll_map[MAX_CLIENTS];
for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active) {
        client_poll_map[nfds - 2] = i;  // Map: poll_index → client_index
        fds[nfds].fd = clients[i].fd;
        fds[nfds].events = POLLIN;
        nfds++;
    }
}

// Use mapping when processing events
for (int i = 2; i < nfds; i++) {
    if (fds[i].revents & POLLIN) {
        int client_idx = client_poll_map[i - 2];  // ✅ Correct!
        // Now we always get the right client
    }
}
```

### Debugging Process
1. **Added debug prints** showing which client was being processed:
   ```c
   printf("Processing poll index %d, client_idx=%d, fd=%d\n", i, client_idx, clients[client_idx].fd);
   ```

2. **Noticed segmentation faults** when accessing `clients[client_idx]` because client_idx was out of bounds

3. **Traced through array construction** and realized poll array was sparse but our indexing assumed it was dense

4. **Drew diagrams** on paper showing the two array structures and their misalignment

5. **Implemented mapping array** solution after understanding the core issue

6. **Verified with stress test:** Connected/disconnected clients randomly to create sparse arrays, confirmed mapping stayed correct

### Lessons Learned
- **Never assume array indices match across different data structures**
- **Poll array indices ≠ application array indices** when using sparse/filtered arrays
- **Debug prints showing actual indices are invaluable** for catching off-by-one errors
- **Test with dynamic connection/disconnection patterns**, not just static scenarios
- **Draw diagrams of data structures** when debugging complex indexing issues
- **Single-step through event loop logic** rather than assuming it works

This bug taught us the importance of explicit mappings when working with multiple related but independently-indexed data structures.

---

## 2. What We Would Redesign

If we were to start the project over from scratch, we would make the following architectural changes:

### 2.1 Use a Linked List for Active Clients

**Current approach:** Fixed-size array with `active` flags
```c
client_t clients[MAX_CLIENTS];
clients[i].active = 1;  // Mark as active
clients[i].active = 0;  // Mark as free (but still in array)
```

**Better approach:** Linked list of active clients only
```c
typedef struct client_node {
    int fd;
    char username[32];
    int is_controller;
    struct client_node *next;
} client_node_t;

client_node_t *clients_head = NULL;
```

**Why this is better:**
- **No wasted memory** for inactive slots
- **No poll index mapping needed** - just iterate the list
- **Easier iteration** over active clients (no checking `active` flag)
- **More natural queue implementation** using the same structure
- **Dynamic sizing** - no arbitrary MAX_CLIENTS limit
- **Clearer code** - if it's in the list, it's active

**Trade-off:** Slightly slower random access, but we don't need that in an event loop

### 2.2 Separate Control Logic into a State Machine Module

**Current approach:** Control logic scattered across multiple functions in main server file

**Better approach:** Dedicated state machine module
```c
// control_fsm.h
typedef enum {
    STATE_NO_CONTROLLER,
    STATE_HAS_CONTROLLER,
    STATE_TRANSFERRING
} control_state_t;

typedef struct {
    control_state_t state;
    int current_controller_id;
    queue_t *request_queue;
} control_manager_t;

// Clear interface
void control_init(control_manager_t *mgr);
int control_request(control_manager_t *mgr, int client_id);
int control_release(control_manager_t *mgr, int client_id);
int control_handle_disconnect(control_manager_t *mgr, int client_id);
```

**Why this is better:**
- **Centralized state management** - all control logic in one place
- **Easier to reason about** - clear state transitions
- **More testable** - can unit test control logic independently
- **Simpler to extend** - adding timeouts or priorities becomes easier
- **Better documentation** - state diagram maps directly to code

### 2.3 Use Length-Prefixed Message Protocol

**Current approach:** Raw byte streaming with in-band escape sequences (Ctrl+T, Ctrl+R, etc.)

**Better approach:** Structured message protocol with length prefixes
```c
struct message {
    uint8_t type;       // MSG_CONTROL_REQUEST, MSG_PTY_OUTPUT, etc.
    uint16_t length;    // Payload size
    char payload[];     // Variable-length data
};

enum message_type {
    MSG_CONTROL_REQUEST = 1,
    MSG_CONTROL_RELEASE = 2,
    MSG_GRANT_CONTROL = 3,
    MSG_PTY_OUTPUT = 4,
    MSG_USER_LIST = 5
};
```

**Why this is better:**
- **No ambiguity** between control commands and PTY data
- **Can send binary data** safely (no special character conflicts)
- **Easier to add new message types** without finding unused ASCII codes
- **Clearer protocol boundaries** - each message is self-contained
- **Better error detection** - length field enables validation
- **Simpler parsing** - no byte-by-byte scanning for escape sequences

**Current problem:** Ctrl+Q conflicts with XON flow control, Ctrl+D conflicts with EOF - we had to switch to Ctrl+X. With structured messages, no conflicts.

### 2.4 Add Structured Logging System

**Current approach:** `printf()` statements scattered everywhere

**Better approach:** Log levels and consistent formatting
```c
enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

void log_msg(enum log_level level, const char *fmt, ...);

// Usage:
log_msg(LOG_INFO, "Client '%s' connected (fd=%d)", username, fd);
log_msg(LOG_DEBUG, "Queue state: head=%d tail=%d size=%d", head, tail, size);
log_msg(LOG_ERROR, "Failed to grant control: client %d inactive", client_id);
```

**Why this is better:**
- **Can enable/disable debug output** at runtime
- **Consistent timestamp and format** across all logs
- **Easier to generate trace logs** for submission
- **Better for automated testing** - can parse structured logs
- **Production-ready** - can redirect to syslog

### 2.5 Implement Proper Error Handling

**Current approach:** Some functions return -1 on error, others just print and continue

**Better approach:** Consistent error handling pattern
```c
typedef enum {
    ERR_OK = 0,
    ERR_CLIENT_NOT_FOUND,
    ERR_QUEUE_FULL,
    ERR_INVALID_STATE,
    ERR_PERMISSION_DENIED
} error_code_t;

error_code_t request_control(int client_id, char *error_msg, size_t msg_len);

// Usage:
char err[256];
error_code_t result = request_control(client_id, err, sizeof(err));
if (result != ERR_OK) {
    send_error_to_client(client_id, err);
    log_msg(LOG_WARN, "Control request failed: %s", err);
}
```

**Why this is better:**
- **Caller can decide how to handle errors**
- **Error messages can be sent to clients**
- **Easier to debug** - know exactly what went wrong
- **More robust** - explicit error paths

---

## 3. Fairness Policy Under Contention

### Scenario: Multiple Users Request Control Simultaneously

**Setup:** 4 users connected to CIS session
- Alice (current controller)
- Bob (observer)
- Carol (observer)
- Dave (observer)

**Timeline of Events:**

| Time | Event | Queue State | Controller | Action Taken |
|------|-------|-------------|------------|--------------|
| T0 | Session starts | [] | Alice | Alice is first client, auto-granted |
| T1 | Bob presses Ctrl+T | [Bob] | Alice | Bob enqueued at position 1 |
| T2 | Carol presses Ctrl+T | [Bob, Carol] | Alice | Carol enqueued at position 2 |
| T3 | Dave presses Ctrl+T | [Bob, Carol, Dave] | Alice | Dave enqueued at position 3 |
| T4 | Alice presses Ctrl+R | [Carol, Dave] | Bob | Bob dequeued and granted control |
| T5 | Bob types for 2 minutes | [Carol, Dave] | Bob | Carol and Dave wait |
| T6 | Bob presses Ctrl+R | [Dave] | Carol | Carol dequeued and granted |
| T7 | Carol types for 1 minute | [Dave] | Carol | Dave continues waiting |
| T8 | Carol presses Ctrl+R | [] | Dave | Dave dequeued and granted |
| T9 | Dave presses Ctrl+R | [] | None | Queue empty, no controller |

**Notification Messages Sent:**

- **T1:** Bob sees: *"Control request sent. Position in queue: 1"*
- **T2:** Carol sees: *"Control request sent. Position in queue: 2"*
- **T3:** Dave sees: *"Control request sent. Position in queue: 3"*
- **T4:** 
  - Alice sees: *"Control released. Control granted to Bob"*
  - Bob sees: *"You now have control. Press Ctrl+R to release."*
  - Carol sees: *"Control granted to Bob"*
  - Dave sees: *"Control granted to Bob"*
- **T6:** Similar notifications for Carol
- **T8:** Similar notifications for Dave

### Fairness Properties Guaranteed

#### 1. **Strict FIFO Ordering**
- Requests are granted in **exact order received**
- First-come, first-served (no exceptions)
- No priority users (all clients treated equally)
- Order preserved even across multiple releases

#### 2. **No Starvation**
- Every queued request **eventually gets served**
- Position in queue only moves forward (never backwards)
- Cannot be "jumped" by later requests
- Guaranteed progress as long as controllers eventually release

#### 3. **Bounded Waiting** (under normal operation)
- Maximum wait time = `(queue_position - 1) × average_session_length`
- Example: If you're 5th in queue and average session is 3 minutes, you wait ≤ 12 minutes
- Note: Without timeout (Phase 1), waiting is unbounded if controller never releases

#### 4. **Deterministic Behavior**
- Same sequence of events → same sequence of grants
- Single-threaded event loop eliminates race conditions
- No randomness in scheduling algorithm
- Predictable, repeatable results

### Implementation Details

**Queue Data Structure:**
```c
request_t request_queue[MAX_CLIENTS];
int queue_head = 0;  // Next position to dequeue (read pointer)
int queue_tail = 0;  // Next position to enqueue (write pointer)

// Enqueue operation (O(1))
request_queue[queue_tail % MAX_CLIENTS].client_index = client_idx;
queue_tail++;
int position = queue_tail - queue_head;  // User's position

// Dequeue operation (O(1))
if (queue_head < queue_tail) {  // Queue not empty
    int next_client = request_queue[queue_head % MAX_CLIENTS].client_index;
    queue_head++;
    return next_client;
}
return -1;  // Queue empty
```

**Circular buffer** using modulo arithmetic prevents array overflow and enables efficient O(1) enqueue/dequeue operations.

### Edge Cases and Handling

#### Case 1: Controller Disconnects While Queue Has Waiters
**Scenario:** Alice has control, Bob and Carol waiting in queue. Alice's connection drops.

**System response:**
1. Server detects disconnect via `poll(POLLHUP)`
2. `remove_client(alice)` called
3. Function checks: `if (clients[alice].is_controller)`
4. Dequeues next client: `Bob`
5. `grant_control(bob)` called automatically
6. Bob receives notification, Carol stays in queue

**Result:** No manual intervention needed, control transfers seamlessly

#### Case 2: Queued User Disconnects Before Their Turn
**Scenario:** Queue is [Bob, Carol, Dave]. Carol disconnects before Bob releases.

**System response:**
1. `remove_client(carol)` called
2. Function rebuilds queue, removing Carol:
   ```c
   int new_tail = queue_head;
   for (int i = queue_head; i < queue_tail; i++) {
       if (queue[i].client_index != carol && clients[queue[i].client_index].active) {
           queue[new_tail++] = queue[i];
       }
   }
   queue_tail = new_tail;
   ```
3. New queue: [Bob, Dave]
4. Dave's position decreases from 3 to 2

**Result:** Queue compacted, no gaps, remaining users keep relative order

#### Case 3: User Requests Control Twice
**Scenario:** Bob already in queue at position 2, presses Ctrl+T again.

**System response:**
1. `request_control(bob)` called
2. Function checks if Bob already in queue
3. Finds Bob at position 2
4. Sends message: *"Already in queue at position 2"*
5. Does NOT add duplicate entry

**Result:** Prevents queue pollution, maintains single entry per user

#### Case 4: No Controller, User Requests Control
**Scenario:** All users released control. Carol presses Ctrl+T.

**System response:**
1. `request_control(carol)` called
2. Function checks: `if (current_controller < 0)`  // No controller
3. **Bypasses queue entirely**
4. Calls `grant_control(carol)` immediately
5. Carol granted control in ~1ms

**Result:** No waiting when system is idle, instant grant

### Comparison to Alternative Policies

| Policy | Pros | Cons | Fairness Score |
|--------|------|------|----------------|
| **FIFO (ours)** | Simple, predictable, fair | No priority for urgent tasks | ⭐⭐⭐⭐⭐ Excellent |
| **Round-robin with time slices** | Prevents monopolization | Interrupts long-running tasks, complex state | ⭐⭐⭐⭐ Good |
| **Priority queue** | Important tasks first | Low-priority users can starve | ⭐⭐ Poor |
| **Random selection** | Very simple | Completely unpredictable, unfair | ⭐ Very poor |
| **Auction/bidding** | Market-based allocation | Favors wealthy users, complex | ⭐ Very poor |

### Why FIFO is Optimal for CIS

1. **Collaborative environment** - no inherent task priority
2. **Educational use case** - all students equal
3. **Predictability valued** - users can estimate wait time
4. **Simplicity** - easy to implement correctly
5. **Transparency** - users understand the policy

### Potential Enhancements (Future Work)

**Timeout mechanism (Phase 3):**
```c
time_t control_granted_time;
#define CONTROL_TIMEOUT 300  // 5 minutes

// In event loop:
if (current_controller >= 0 && time(NULL) - control_granted_time > CONTROL_TIMEOUT) {
    release_control(current_controller);  // Auto-release
}
```

**Priority boost for long waiters:**
```c
typedef struct {
    int client_index;
    time_t request_time;
} timed_request_t;

// After 10 minutes, boost to front of queue
```

---

## 4. Additional Reflections

### What Went Well

**PTY integration:** `forkpty()` worked perfectly on first try, bash shell behaved as expected

**Event loop design:** `poll()` provided clean multiplexing without threading complexity

**UNIX sockets:** Reliable, fast local IPC with simple API

**Team collaboration:** Code reviews caught bugs early, pair programming on tough sections

**Incremental development:** Built multi-client first, then added control protocol - good strategy

### What Was Challenging

**Terminal raw mode:** Getting echo and line buffering right took multiple iterations

**Escape sequence conflicts:** Ctrl+Q/Ctrl+D had terminal control conflicts

**Multi-terminal debugging:** Hard to trace execution across 4 terminals simultaneously

**Index mapping bug:** Took 3 hours to identify and fix the poll/client array mismatch

**Process cleanup:** Ensuring no orphan processes or zombie children required careful signal handling

### Skills and Knowledge Gained

**Technical Skills:**
- Deep understanding of PTY (pseudo-terminal) mechanics and master/slave relationship
- I/O multiplexing with `poll()` and event-driven architecture patterns
- UNIX domain socket programming and local IPC
- Terminal control with `termios` (raw mode, echo, line buffering)
- Signal handling (`SIGCHLD`, `SIGPIPE`, `SIGHUP`, `SIGINT`)
- Process management (`fork()`, `exec()`, `waitpid()`, process groups)

**Design Skills:**
- Event-driven architecture vs. threaded models (trade-offs)
- Queue-based fairness policies (FIFO vs. priority vs. round-robin)
- State machine design for control flow
- Protocol design (in-band vs. out-of-band signaling)

**Debugging Skills:**
- Multi-process debugging with print statements
- Index arithmetic verification with diagrams
- Stress testing with random connect/disconnect patterns
- Memory debugging with valgrind

**Professional Skills:**
- Technical writing (documentation, postmortems)
- Code review and pair programming
- Time management under deadline pressure
- Design trade-off analysis

### Project Timeline

- **Day 1 (4 hours):** Multi-client support, broadcasting
- **Day 2 (6 hours):** Floor control protocol, FIFO queue, escape sequences
- **Day 3 (2 hours):** Bug fixes, testing, edge cases
- **Day 4 (4 hours):** Documentation, demo preparation

**Total:** ~16 hours of focused development time

### If We Had More Time

We would have implemented:
1. **Automatic timeout** for idle controllers (with configurable duration)
2. **Session logging** to file for replay/audit
3. **User authentication** (PAM integration or simple password)
4. **Multiple sessions** (different socket paths, session IDs)
5. **Window resize propagation** (`SIGWINCH` handling)
6. **Better error messages** sent to clients (not just server logs)
7. **Graceful degradation** if PTY creation fails
8. **Configuration file** for settings (timeout, max clients, etc.)

---

## 5. Proposal Compliance
   
   Our Phase 2 implementation matches the Phase 1 proposal for all core features:
   - FIFO queue control allocation ✅
   - Ctrl+T/R floor control ✅  
   - Multi-client broadcasting ✅
   - Auto-transfer on disconnect ✅
   
   Minor changes from proposal:
   - Ctrl+Q → Ctrl+X (XON flow control conflict)
   - Timeout deferred to Phase 3 (scope management)
   - Host kick not implemented (was optional)
   
   See CHANGES.md for detailed explanation.

## Conclusion

This project provided hands-on experience with core operating system concepts: process management, inter-process communication, I/O multiplexing, and terminal control. The most valuable lesson was learning to debug complex multi-component systems through systematic investigation and explicit data structure mapping.

The FIFO fairness policy proved to be the right choice for a collaborative shell environment, providing predictable and equitable access to all users. While we encountered challenging bugs (particularly the poll index mapping issue), each bug taught us to be more careful about assumptions and to verify index arithmetic explicitly.

If we were to rebuild this system, we would adopt a linked-list client structure, separate control logic into a state machine module, and use a structured message protocol instead of in-band escape sequences. These changes would make the codebase more maintainable, testable, and extensible.

Overall, the project was a success. We delivered a working system that meets all Phase 2 requirements and demonstrates solid understanding of operating system primitives.

---

**End of Postmortem**
