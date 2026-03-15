# How to Record Demo Transcript
# ================================
# Do this on home.cs.siue.edu using 4 terminal windows (SSH in 4 times)
# Takes about 15 minutes total
# After done, delete this file before final submission

## Setup (all terminals)
cd ~/cis_phase2
make

## Tips before you start
# - Make one small typo somewhere (e.g. type "lss" then "ls") — keeps it natural
# - Don't rush between steps, let server output settle before moving on
# - Copy EVERYTHING you see including the shell prompt lines like "sghimire@..."
# - The date command will give you the correct day automatically — don't type it manually

# =============================================================================
# SESSION 1: Basic Multi-Client Collaboration
# =============================================================================
# Terminal 1: start server
./server
# (leave running, copy everything it prints for the whole session)

# Terminal 2: connect first client (auto-becomes controller)
./client
# type these commands:
ls
pwd

# Terminal 3: connect second client (becomes observer)
./client
# just watch — you should see ls and pwd output appear

# Terminal 4: connect third client (becomes observer)
./client
# just watch

# Copy output from all 4 terminals into SESSION 1 section of demo_transcript.txt

# =============================================================================
# SESSION 2: Floor Control Transfer
# =============================================================================
# (keep all 4 terminals from Session 1 open)

# Terminal 3: press Ctrl+T   -> should say "Position in queue: 1"
# Terminal 4: press Ctrl+T   -> should say "Position in queue: 2"
# Terminal 2: press Ctrl+R   -> control releases, transfers to Terminal 3

# Terminal 3 (now controller): type these commands:
whoami
date
echo "Control transferred successfully!"

# Copy output from all 4 terminals into SESSION 2 section

# =============================================================================
# SESSION 3: List Users
# =============================================================================
# (keep all terminals open)

# Terminal 2: press Ctrl+L   -> should show user list with controller marked
# Terminal 4: press Ctrl+L   -> should show same list

# Copy output into SESSION 3 section

# =============================================================================
# SESSION 4: Controller Disconnect & Auto-Transfer
# =============================================================================
# (Terminal 3 still has control, Terminal 4 still waiting in queue)

# Terminal 3: press Ctrl+X   -> quits abruptly as controller
# Watch Terminal 4: should auto-receive control
# Terminal 4 (now controller): type:
uname -a
echo "Auto-transfer works!"

# Copy output from Terminal 1 (server) and Terminal 4 into SESSION 4 section

# =============================================================================
# SESSION 5: Observer Cannot Type Commands
# =============================================================================
# (Terminal 2 is observer, Terminal 4 is controller)

# Terminal 2: type "ls" and press Enter
# Nothing should happen — input ignored

# Copy Terminal 1 server output (should show ignored message)
# Copy Terminal 2 output (should show nothing happened)

# =============================================================================
# SESSION 6: Clean Shutdown
# =============================================================================
# Terminal 4: press Ctrl+X
# Terminal 2: press Ctrl+X  (may auto-get control first, then quit)
# Terminal 1: press Ctrl+C to stop server

# Copy Terminal 1 server output showing clean shutdown

# =============================================================================
# AFTER YOU'RE DONE
# =============================================================================
# Open demo_transcript.txt and replace the content under each SESSION header
# with what you copied from your terminals.
# Keep the existing section headers and [TERMINAL X] labels exactly as they are.
# Just replace the output lines below each label.
#
# Then delete this file:
#   rm HOW_TO_DEMO.md
