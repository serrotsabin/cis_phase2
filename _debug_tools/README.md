# _debug_tools/ — Trace Generation Kit

This folder is NOT part of the final submission.
Delete it before zipping.

## What's in here

| File | Purpose |
|------|---------|
| `server_debug.c` | Copy of `src/server.final.c` with `dbg_log()` calls added to stderr. Logic is identical — only difference is timestamped debug output. |
| `capture_traces.sh` | Builds the debug server + client, runs 4 scripted scenarios, writes output to `trace/`. |

## How to run

```bash
# From the cis_phase2/ root directory:
bash _debug_tools/capture_traces.sh
```

That's it. The script handles building, running, verifying, and cleanup.

## What to check after running

```bash
# disconnect.log — user2 must be enqueued BEFORE user1 removes as controller
grep -E "enqueue|was_controller=1" trace/disconnect.log

# floor_control.log — pos=1 before pos=2, user2 granted before user3
grep -E "enqueue|grant_control" trace/floor_control.log

# observer_filter.log — observer_drop line exists, no pty_write before it
grep -E "observer_drop|pty_write" trace/observer_filter.log
```

## Platform notes

- **Linux / home.cs.siue.edu**: works as-is
- **macOS**: edit `server_debug.c` line 23, change `<pty.h>` back to `<util.h>`

## What dbg_log() does

```c
static void dbg_log(const char *fmt, ...) {
    // gets current time with millisecond precision
    // prints to stderr as:  [HH:MM:SS.mmm] your message here
}
```

All debug output goes to **stderr**. The script redirects both stdout and stderr
into the trace files (`2>&1`), so you get the normal server printf output AND
the debug lines interleaved with real timestamps.
