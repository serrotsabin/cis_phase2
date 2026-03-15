# CIS Phase 2 - Postmortem

**Team Members:** Abin Timilsina, Sabin Ghimire, Nuraj Rimal  
**Date:** February 13, 2026  
**Project:** Collaborative Interactive Shell (CIS) - Phase 2 Implementation

---

## 1. Hardest Bug Encountered

### Bug Description
The most challenging bug was **controller input not being forwarded to the PTY** when multiple clients were connected. The server accepted connections and broadcast PTY output correctly, but keystrokes from the controller were silently ignored — commands just disappeared with no error.

### Reproduction Steps
1. Start server: `./server`
2. Connect client 1 (becomes controller): `./client`
3. Connect client 2 (becomes observer): `./client`
4. In client 1, type: `ls`
5. **Expected:** Command executes, output appears in all terminals
6. **Actual:** Nothing happens, no output, no error

### Root Cause
The bug was in the `poll()` fd mapping logic. We built the poll array dynamically (skipping inactive client slots), but then used static arithmetic to map poll indices back to client indices:

```c
// BROKEN
for (int i = 2; i < nfds; i++) {
    if (fds[i].revents & POLLIN) {
        int client_idx = i - 2;  // WRONG
        // assumes poll array index == client array index
        // if clients[1] is inactive, clients[2] lands at poll index 2
        // but i-2 = 0, pointing at the wrong client entirely
    }
}
```

### Fix
We added a mapping array built alongside the poll array:

```c
int client_poll_map[MAX_CLIENTS];
for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active) {
        client_poll_map[nfds - 2] = i;   // poll_index -> client_index
        fds[nfds].fd = clients[i].fd;
        fds[nfds].events = POLLIN;
        nfds++;
    }
}

for (int i = 2; i < nfds; i++) {
    if (fds[i].revents & POLLIN) {
        int client_idx = client_poll_map[i - 2];  // correct
    }
}
```

### How We Found It
Added printfs showing which client index was being processed — immediately saw wrong values. Hit segfaults from out-of-bounds client array access. Drew the two arrays on paper and saw the misalignment: poll array is always dense, client array is sparse when slots have been freed. Once we saw that on paper the fix was obvious.

---

## 2. What We Would Redesign

### Linked list instead of fixed array for clients
The fixed array with `active` flags is what caused the poll mapping bug. A linked list of active clients only would eliminate it — iterating the list to build the poll array produces a natural 1-to-1 mapping with no gaps, no mapping array needed.

### State machine for control logic
`grant_control`, `release_control`, `request_control`, and `remove_client` all modify `current_controller` directly. It works but state is scattered across four functions. A proper FSM with explicit states (`NO_CONTROLLER`, `HAS_CONTROLLER`) and a single place owning transitions would make the code easier to reason about — adding a timeout would just be a new transition, not a change spread across multiple functions.

### Length-prefixed message protocol
The in-band escape sequence approach (Ctrl+T, Ctrl+R, Ctrl+X) caused real friction — Ctrl+Q conflicted with XON flow control and we had to switch keys mid-development. A length-prefixed binary protocol would cleanly separate control messages from PTY data at the framing level with no byte-value conflicts.

---

## 3. Fairness Policy Under Contention

Our policy is strict FIFO — requests are granted in exact order received, no exceptions.

**Example: Alice=controller, Bob/Carol/Dave=observers all request simultaneously**

| Time | Event | Queue | Controller |
|------|-------|-------|------------|
| T0 | Session starts | [] | Alice |
| T1 | Bob presses Ctrl+T | [Bob] | Alice |
| T2 | Carol presses Ctrl+T | [Bob, Carol] | Alice |
| T3 | Dave presses Ctrl+T | [Bob, Carol, Dave] | Alice |
| T4 | Alice presses Ctrl+R | [Carol, Dave] | Bob |
| T5 | Bob presses Ctrl+R | [Dave] | Carol |
| T6 | Carol presses Ctrl+R | [] | Dave |

Bob requested first so Bob gets control first. Carol and Dave follow in order. No user can jump the queue regardless of timing.

**Properties guaranteed:** no starvation (every queued request eventually served), no jumping (later requests never overtake earlier ones), deterministic (same event sequence always produces same grant sequence).

**Edge cases:**

*Controller disconnects:* `remove_client` detects `is_controller=1`, clears `current_controller`, immediately dequeues and calls `grant_control` on the next waiter. No manual intervention needed.

*Queued user disconnects before their turn:* queue is rebuilt skipping that client, remaining users shift forward preserving relative order.

*User requests control twice:* `enqueue_request` scans for duplicates first and sends "Already in queue at position N" without adding a second entry.

*No controller when someone requests:* `request_control` checks `current_controller < 0` and grants immediately, bypassing the queue.