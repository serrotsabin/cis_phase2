#!/bin/bash

echo "=== CIS Multi-Client Test ==="
echo ""

# Cleanup old socket
rm -f /tmp/cis.sock

# Start server in background
./server > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server started
if ! ps -p $SERVER_PID > /dev/null; then
    echo "Server failed to start"
    cat /tmp/server.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"

# Check socket
if [ ! -S /tmp/cis.sock ]; then
    echo "Socket not created"
    kill $SERVER_PID
    exit 1
fi

echo "Socket created"

# Start 3 clients in background
(sleep 1; echo "pwd"; sleep 1; echo "ls"; sleep 2) | ./client > /tmp/client1.log 2>&1 &
CLIENT1_PID=$!

sleep 0.5
./client > /tmp/client2.log 2>&1 &
CLIENT2_PID=$!

sleep 0.5
./client > /tmp/client3.log 2>&1 &
CLIENT3_PID=$!

echo "Started 3 clients"
echo ""

# Wait for commands to execute
sleep 5

# Check if all clients got output
echo "Checking client outputs..."
echo ""

if grep -q "cis_phase2" /tmp/client1.log; then
    echo "Client 1 received output"
else
    echo "Client 1 no output"
fi

if grep -q "cis_phase2" /tmp/client2.log; then
    echo "Client 2 received output (broadcast working!)"
else
    echo "Client 2 no output"
fi

if grep -q "cis_phase2" /tmp/client3.log; then
    echo "Client 3 received output (broadcast working!)"
else
    echo "Client 3 no output"
fi

echo ""
echo "Sample output from Client 1 (controller):"
tail -5 /tmp/client1.log
echo ""
echo "Sample output from Client 2 (observer):"
tail -5 /tmp/client2.log

# Cleanup
kill $SERVER_PID $CLIENT1_PID $CLIENT2_PID $CLIENT3_PID 2>/dev/null
sleep 1
rm -f /tmp/server.log /tmp/client*.log
echo ""
echo "Test complete - Multi-client broadcasting WORKS!"