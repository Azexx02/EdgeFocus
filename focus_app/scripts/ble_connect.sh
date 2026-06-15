#!/bin/bash

# Usage: ble_connect.sh <MAC>
# Connects to the given MAC via bluetoothctl, sets up GATT, then listens on FIFO.
# Called from C++ via popen() - coproc works in this context.

MAC="$1"
FIFO="/tmp/ble_cmd_fifo"
STATUS_FILE="/tmp/ble_status"

if [ -z "$MAC" ]; then
    echo "Usage: ble_connect.sh <MAC>"
    exit 1
fi

rfkill unblock bluetooth 2>/dev/null
hciconfig hci0 up 2>/dev/null

set_status() { echo "$1" > "$STATUS_FILE"; }
set_status "disconnected"

# Start bluetoothctl via coproc
coproc btc { stdbuf -i0 -o0 -e0 bluetoothctl; }
sleep 1

echo "power on" >&${btc[1]}
sleep 1
echo "agent on" >&${btc[1]}
sleep 1
echo "connect $MAC" >&${btc[1]}
sleep 5

# Check connection
CONNECTED=0
# Read pending output
while IFS= read -r -t 2 -u ${btc[0]} line 2>/dev/null; do
    if echo "$line" | grep -q "Connection successful\|Connected: yes"; then
        CONNECTED=1
    fi
    if echo "$line" | grep -q "not available\|disconnected with reason"; then
        CONNECTED=0
        break
    fi
done

if [ $CONNECTED -eq 0 ]; then
    echo "Connection failed"
    set_status "disconnected"
    echo "quit" >&${btc[1]} 2>/dev/null
    exit 1
fi

# GATT setup
echo "menu gatt" >&${btc[1]}
sleep 1
echo "select-attribute 00002a56-0000-1000-8000-00805f9b34fb" >&${btc[1]}
sleep 1

set_status "connected $MAC"
echo "Connected"

# Command loop - listen on FIFO for commands from C++
while true; do
    if read -t 1 line <"$FIFO" 2>/dev/null; then
        if [ "$line" == "buzz" ]; then
            echo "write \"0x01\"" >&${btc[1]}
        elif [ "$line" == "quit" ]; then
            break
        fi
    fi

    # Check for disconnection
    while IFS= read -r -t 0 -u ${btc[0]} line 2>/dev/null; do
        if echo "$line" | grep -q "disconnected with reason\|Connected: no"; then
            echo "Disconnected"
            break 2
        fi
    done

    # Check if bluetoothctl is alive
    if ! kill -0 ${btc_PID} 2>/dev/null; then
        echo "bluetoothctl died"
        break
    fi
done

set_status "disconnected"
echo "quit" >&${btc[1]} 2>/dev/null
echo "Exited"
