# How to Generate Trace Logs
# ============================
# Do this on home.cs.siue.edu using 4 terminal windows (SSH in 4 times)
# Takes about 10 minutes total

## Setup (all terminals)
cd ~/cis_phase2
make

# ─────────────────────────────────────────────────────────────
# TRACE 1: session_start.log
# ─────────────────────────────────────────────────────────────
# Terminal 1: start server
./server

# Terminal 2, 3, 4: connect one at a time (wait 2s between each)
./client

# Terminal 2 (controller): type a couple commands
ls
pwd

# Copy EVERYTHING printed in Terminal 1 (server output)
# Paste into trace/session_start.log with this header at top:
#
#   CIS Phase 2 - Trace: Session Initialization
#   Host: home.cs.siue.edu
#   Date: [today's date]
#   ---
#   [paste server output here]

# Ctrl+C the server when done, then rm /tmp/cis.sock

# ─────────────────────────────────────────────────────────────
# TRACE 2: floor_control.log
# ─────────────────────────────────────────────────────────────
# Terminal 1: start server
./server

# Terminal 2, 3, 4: connect clients
./client

# Terminal 3: press Ctrl+T   (request control)
# Terminal 4: press Ctrl+T   (request control)
# Terminal 2: press Ctrl+R   (release control) -> should go to Terminal 3
# Terminal 3: type a command, then press Ctrl+R -> should go to Terminal 4
# Terminal 4: type a command, then press Ctrl+R

# Copy Terminal 1 output -> paste into trace/floor_control.log

# ─────────────────────────────────────────────────────────────
# TRACE 3: disconnect.log
# ─────────────────────────────────────────────────────────────
# Terminal 1: start server
./server

# Terminal 2, 3: connect clients (just 2 this time)
./client

# Terminal 3: press Ctrl+T   (request control, gets queued)
# Terminal 2: press Ctrl+X   (quit abruptly — still has control)
# Watch: server should auto-grant control to Terminal 3

# Copy Terminal 1 output -> paste into trace/disconnect.log

# ─────────────────────────────────────────────────────────────
# TRACE 4: observer_filter.log
# ─────────────────────────────────────────────────────────────
# Terminal 1: start server
./server

# Terminal 2, 3: connect clients (just 2)
./client

# Terminal 3 (observer): type "ls" and press Enter
# Nothing should happen — input ignored

# Terminal 2 (controller): type "ls" and press Enter
# Output appears in both terminals

# Copy Terminal 1 output -> paste into trace/observer_filter.log

# ─────────────────────────────────────────────────────────────
# DONE
# ─────────────────────────────────────────────────────────────
# Delete this file before zipping:
#   rm HOW_TO_TRACE.md
