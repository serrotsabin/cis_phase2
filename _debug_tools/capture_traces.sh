#!/bin/bash
# =============================================================================
# capture_traces.sh — Generate real trace logs for CIS Phase 2 submission
# =============================================================================
#
# WHAT THIS DOES:
#   Builds a debug version of the server (server_debug.c), runs 4 automated
#   test scenarios using scripted clients, and captures the server's internal
#   debug output into trace/*.log files.
#
#   The debug server is identical to server.final.c in logic — it just adds
#   timestamped dbg_log() calls to stderr so we can capture what's happening
#   internally. The production binary (server.final.c) is unchanged.
#
# HOW TO RUN:
#   1. Copy this folder (_debug_tools/) next to your src/ directory
#   2. Run from the cis_phase2/ root:
#        cd cis_phase2/
#        bash _debug_tools/capture_traces.sh
#   3. Check trace/ — you should have 4 .log files
#   4. Verify with the CHECK section at the bottom of this file
#
# PLATFORM:
#   Linux (including home.cs.siue.edu): works as-is
#   macOS: change <pty.h> back to <util.h> in server_debug.c before running
#
# AFTER YOU'RE DONE:
#   Delete _debug_tools/ before final zip — it's not part of the submission.
#   The trace/ logs it produced ARE part of the submission.
#
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/src"
TRACE_DIR="$ROOT/trace"
TOOLS_DIR="$SCRIPT_DIR"

SOCK=/tmp/cis.sock
SERVER="$TOOLS_DIR/server_debug"
CLIENT="$TOOLS_DIR/client_debug"

echo "============================================================"
echo "CIS Phase 2 — Trace Capture"
echo "Working from: $ROOT"
echo "============================================================"
echo ""

# ── STEP 1: Build ────────────────────────────────────────────────────────────
echo "[1/5] Building debug binaries..."

cp "$SRC/protocol.h" "$TOOLS_DIR/protocol.h"

# Build server_debug
gcc -Wall -Wextra -I"$TOOLS_DIR" \
    -o "$SERVER" "$TOOLS_DIR/server_debug.c" -lutil
echo "      server_debug: OK"

# Build client_debug (same client.final.c, just compiled for this run)
# On macOS, client.final.c may also use <util.h> — swap to <pty.h> if needed
gcc -Wall -Wextra -I"$SRC" \
    -o "$CLIENT" "$SRC/client.final.c"
echo "      client_debug: OK"
echo ""

# ── STEP 2: Setup ────────────────────────────────────────────────────────────
echo "[2/5] Setting up trace directory..."
mkdir -p "$TRACE_DIR"
rm -f "$SOCK"
echo "      trace/: ready"
echo ""

# ── HELPER ───────────────────────────────────────────────────────────────────
header() {
    echo "========================================================================"
    echo "CIS Phase 2 - Trace Log: $1"
    echo "Host: $(hostname -f 2>/dev/null || hostname) ($(uname -r))"
    echo "Test Date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "Tester: ${USER:-unknown}"
    echo "========================================================================"
    echo ""
}

# ── TRACE 1: session_start ───────────────────────────────────────────────────
echo "[3/5] Running scenarios..."
echo "      [1/4] session_start..."
{
    header "Session Initialization (host start + 3 clients join + ls + pwd)"

    rm -f "$SOCK"
    "$SERVER" 2>&1 &
    SERVER_PID=$!
    sleep 0.5

    # Client 1 — becomes controller, runs ls then pwd, then quits
    (sleep 0.3; printf "ls\n"; sleep 0.6; printf "pwd\n"; sleep 0.6; printf "\x18") \
        | "$CLIENT" >/dev/null 2>&1 &
    C1=$!
    sleep 0.2

    # Client 2 — observer, joins and quits
    (sleep 1.5; printf "\x18") | "$CLIENT" >/dev/null 2>&1 &
    C2=$!
    sleep 0.1

    # Client 3 — observer, joins and quits
    (sleep 1.5; printf "\x18") | "$CLIENT" >/dev/null 2>&1 &
    C3=$!

    wait $C1 $C2 $C3 2>/dev/null
    sleep 0.3
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
} > "$TRACE_DIR/session_start.log" 2>&1
rm -f "$SOCK"
sleep 0.3

# ── TRACE 2: floor_control ───────────────────────────────────────────────────
echo "      [2/4] floor_control..."
{
    header "Floor Control — FIFO Queue (user2 requests, user3 requests, user1 releases in order)"

    rm -f "$SOCK"
    "$SERVER" 2>&1 &
    SERVER_PID=$!
    sleep 0.5

    # user1 — controller, runs whoami, holds 1.8s, then releases (Ctrl+R), then quits
    (sleep 0.3
     printf "whoami\n"
     sleep 1.8
     printf "\x12"   # Ctrl+R = release
     sleep 0.5
     printf "\x18"   # Ctrl+X = quit
    ) | "$CLIENT" >/dev/null 2>&1 &
    C1=$!
    sleep 0.2

    # user2 — requests control (Ctrl+T) at ~t=0.5s, waits for grant,
    #          runs date, releases, quits
    (sleep 0.3
     printf "\x14"   # Ctrl+T = request
     sleep 1.8
     printf "date\n"
     sleep 0.4
     printf "\x12"   # Ctrl+R = release
     sleep 0.3
     printf "\x18"
    ) | "$CLIENT" >/dev/null 2>&1 &
    C2=$!
    sleep 0.15

    # user3 — requests control (Ctrl+T) at ~t=0.65s (after user2),
    #          waits for grant, runs echo, quits
    (sleep 0.3
     printf "\x14"   # Ctrl+T = request
     sleep 2.8
     printf "echo transfer-ok\n"
     sleep 0.4
     printf "\x18"
    ) | "$CLIENT" >/dev/null 2>&1 &
    C3=$!

    wait $C1 $C2 $C3 2>/dev/null
    sleep 0.3
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
} > "$TRACE_DIR/floor_control.log" 2>&1
rm -f "$SOCK"
sleep 0.3

# ── TRACE 3: disconnect ──────────────────────────────────────────────────────
echo "      [3/4] disconnect..."
{
    header "Controller Disconnect + Auto-Transfer to Queued Observer"

    rm -f "$SOCK"
    "$SERVER" 2>&1 &
    SERVER_PID=$!
    sleep 0.5

    # user1 — controller, runs yes|head (long-ish output), then disconnects after 1s
    (sleep 0.1
     printf "yes | head -5\n"
     sleep 1.0
     printf "\x18"   # abrupt quit while user2 is queued
    ) | "$CLIENT" >/dev/null 2>&1 &
    C1=$!
    sleep 0.2

    # user2 — joins, requests control (Ctrl+T) at ~t=0.4s,
    #          waits for auto-grant after user1 drops, runs uname -a
    (sleep 0.2
     printf "\x14"   # Ctrl+T = request (user1 still has control here)
     sleep 1.5
     printf "uname -a\n"
     sleep 0.5
     printf "\x18"
    ) | "$CLIENT" >/dev/null 2>&1 &
    C2=$!

    wait $C1 $C2 2>/dev/null
    sleep 0.3
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
} > "$TRACE_DIR/disconnect.log" 2>&1
rm -f "$SOCK"
sleep 0.3

# ── TRACE 4: observer_filter ─────────────────────────────────────────────────
echo "      [4/4] observer_filter..."
{
    header "Observer Input Filtering — Observer Keystrokes Not Forwarded to PTY"

    rm -f "$SOCK"
    "$SERVER" 2>&1 &
    SERVER_PID=$!
    sleep 0.5

    # user1 — controller, types ls at t=0.8s
    (sleep 0.8
     printf "ls\n"
     sleep 0.5
     printf "\x18"
    ) | "$CLIENT" >/dev/null 2>&1 &
    C1=$!
    sleep 0.2

    # user2 — observer, tries to type ls at t=0.4s (before user1)
    # Expected: server logs observer_drop, PTY gets NO input from user2
    (sleep 0.2
     printf "ls\n"   # should be dropped — user2 is observer
     sleep 0.9
     printf "\x18"
    ) | "$CLIENT" >/dev/null 2>&1 &
    C2=$!

    wait $C1 $C2 2>/dev/null
    sleep 0.3
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
} > "$TRACE_DIR/observer_filter.log" 2>&1
rm -f "$SOCK"

# ── STEP 3: Verify ───────────────────────────────────────────────────────────
echo ""
echo "[4/5] Verifying traces..."
echo ""

PASS=0
FAIL=0

check() {
    local label="$1"
    local file="$2"
    local pattern="$3"
    if grep -q "$pattern" "$file" 2>/dev/null; then
        echo "      PASS — $label"
        PASS=$((PASS+1))
    else
        echo "      FAIL — $label"
        echo "             (looked for: $pattern)"
        echo "             (in file:    $file)"
        FAIL=$((FAIL+1))
    fi
}

check "session_start: server started"       "$TRACE_DIR/session_start.log"  "server started:"
check "session_start: 3 clients joined"     "$TRACE_DIR/session_start.log"  "slot=2"
check "session_start: broadcast to 3 fds"  "$TRACE_DIR/session_start.log"  "broadcast:.*fd=7"
check "floor_control: user2 enqueued pos=1" "$TRACE_DIR/floor_control.log"  "enqueue.*pos=1"
check "floor_control: user3 enqueued pos=2" "$TRACE_DIR/floor_control.log"  "enqueue.*pos=2"
check "floor_control: release fires first"  "$TRACE_DIR/floor_control.log"  "release_control"
check "floor_control: grant to user2 first" "$TRACE_DIR/floor_control.log"  "grant_control.*user2"
check "disconnect: user2 enqueued"          "$TRACE_DIR/disconnect.log"     "enqueue.*user2"
check "disconnect: user1 was_controller=1"  "$TRACE_DIR/disconnect.log"     "was_controller=1"
check "disconnect: auto-grant to user2"     "$TRACE_DIR/disconnect.log"     "grant_control.*user2"
check "observer_filter: drop logged"        "$TRACE_DIR/observer_filter.log" "observer_drop"
check "observer_filter: controller wrote"   "$TRACE_DIR/observer_filter.log" "pty_write.*user1"

echo ""
echo "      Results: $PASS passed, $FAIL failed"
echo ""

# ── STEP 4: Cleanup debug binaries ───────────────────────────────────────────
echo "[5/5] Cleaning up build artifacts..."
rm -f "$SERVER" "$CLIENT" "$TOOLS_DIR/protocol.h"
echo "      server_debug, client_debug removed"
echo ""

# ── DONE ─────────────────────────────────────────────────────────────────────
echo "============================================================"
echo "Trace files written to: $TRACE_DIR/"
ls -lh "$TRACE_DIR/"
echo ""
echo "Next steps:"
echo "  1. Review each trace file — they should show your machine's"
echo "     hostname and kernel version, not 'runsc'."
echo "  2. If any checks FAILED, re-run the script (timing issues"
echo "     on loaded machines can cause occasional misses)."
echo "  3. Once happy, delete _debug_tools/ before final zip."
echo "============================================================"
