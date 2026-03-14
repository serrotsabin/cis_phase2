#!/bin/bash

echo "=== Floor Control Test ==="
echo ""
echo "This test requires manual interaction"
echo ""

# Cleanup
rm -f /tmp/cis.sock

# Start server
echo "Starting server..."
./server &
SERVER_PID=$!
sleep 2

echo ""
echo "Server started (PID: $SERVER_PID)"
echo ""
echo "Instructions:"
echo "============="
echo ""
echo "Terminal 2: Run './client' (will be controller)"
echo "Terminal 3: Run './client' (will be observer)"
echo "Terminal 4: Run './client' (will be observer)"
echo ""
echo "Then test:"
echo "  1. Controller types 'ls' - all should see output"
echo "  2. Observer presses Ctrl+T - should see queue position"
echo "  3. Controller presses Ctrl+R - control transfers"
echo "  4. New controller types 'pwd' - all should see output"
echo "  5. Press Ctrl+L in any client - see user list"
echo ""
echo "Press Enter when done to cleanup..."
read

# Cleanup
kill $SERVER_PID 2>/dev/null
rm -f /tmp/cis.sock
echo ""
echo "Cleanup complete"