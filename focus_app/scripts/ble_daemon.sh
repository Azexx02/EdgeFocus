#!/bin/bash

# Usage: ble_daemon.sh <MAC>
# Connects to the given MAC, selects GATT characteristic, listens on FIFO for commands.
# Uses FIFO for bluetoothctl stdin, log file for output monitoring.

MAC="$1"
FIFO="/tmp/ble_cmd_fifo"
STATUS_FILE="/tmp/ble_status"
BTCTL_IN="/tmp/btctl_in"
BTCTL_LOG="/tmp/btctl.log"

if [ -z "$MAC" ]; then
    echo "Usage: ble_daemon.sh <MAC>"
    exit 1
fi

rfkill unblock bluetooth 2>/dev/null
hciconfig hci0 up 2>/dev/null

set_status() { echo "$1" > "$STATUS_FILE"; }
set_status "disconnected"

cleanup() {
    set_status "disconnected"
    echo "disconnect $MAC" >$BTCTL_IN 2>/dev/null
    echo "quit" >$BTCTL_IN 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    wait $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_IN" "$BTCTL_LOG"
    exit 0
}
trap cleanup SIGTERM SIGINT

# Start bluetoothctl with FIFO stdin and log file stdout
rm -f "$BTCTL_IN" "$BTCTL_LOG"
mkfifo "$BTCTL_IN"
stdbuf -i0 -o0 -e0 bluetoothctl <"$BTCTL_IN" >"$BTCTL_LOG" 2>&1 &
BTCTL_PID=$!
exec 7>"$BTCTL_IN"

sleep 1

echo "power on" >&7
sleep 1
echo "agent on" >&7
sleep 1

# Connect directly (MAC was already found by ble_scan.sh via popen)
echo "connect $MAC" >&7
sleep 8

# Verify connection
if tail -30 "$BTCTL_LOG" 2>/dev/null | grep -q "Connection successful\|Connected: yes"; then
    echo "Connected"
elif tail -30 "$BTCTL_LOG" 2>/dev/null | grep -q "not available\|disconnected with reason"; then
    echo "Connection failed"
    set_status "disconnected"
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_IN" "$BTCTL_LOG"
    exit 1
else
    echo "Connection status unknown, assuming failed"
    set_status "disconnected"
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_IN" "$BTCTL_LOG"
    exit 1
fi

# GATT setup
echo "menu gatt" >&7
sleep 1
echo "select-attribute 00002a56-0000-1000-8000-00805f9b34fb" >&7
sleep 2

# Verify still connected after GATT
if tail -10 "$BTCTL_LOG" 2>/dev/null | grep -q "disconnected with reason\|Connected: no"; then
    echo "Connection dropped during GATT setup"
    set_status "disconnected"
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_IN" "$BTCTL_LOG"
    exit 1
fi

set_status "connected $MAC"
echo "Connected and ready"

# Command loop with disconnection monitoring
while true; do
    if read -t 1 line <"$FIFO" 2>/dev/null; then
        if [ "$line" == "buzz" ]; then
            echo "write \"0x01\"" >&7
        elif [ "$line" == "quit" ]; then
            break
        fi
    fi

    # Check bluetoothctl log for disconnection
    if tail -20 "$BTCTL_LOG" 2>/dev/null | grep -q "disconnected with reason\|Connected: no"; then
        echo "Detected disconnection"
        break
    fi

    # Check if bluetoothctl is still alive
    if ! kill -0 $BTCTL_PID 2>/dev/null; then
        echo "bluetoothctl process died"
        break
    fi
done

set_status "disconnected"
echo "disconnect $MAC" >&7 2>/dev/null
echo "quit" >&7 2>/dev/null
exec 7>&- 2>/dev/null
kill $BTCTL_PID 2>/dev/null
rm -f "$BTCTL_IN" "$BTCTL_LOG"
echo "Daemon exited"
