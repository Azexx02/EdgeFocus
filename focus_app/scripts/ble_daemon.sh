#!/bin/bash

# Usage: ble_daemon.sh <MAC>
# Connects to the given MAC, selects GATT characteristic, listens on FIFO for commands.

MAC="$1"
FIFO="/tmp/ble_cmd_fifo"
STATUS_FILE="/tmp/ble_status"

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
    echo "disconnect $MAC" >&7 2>/dev/null
    echo "quit" >&7 2>/dev/null
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_FIFO"
    exit 0
}
trap cleanup SIGTERM SIGINT

# Helper: check if device is currently connected
check_connected() {
    echo "info $MAC" | timeout 3 bluetoothctl 2>/dev/null | grep -q "Connected: yes"
}

BTCTL_FIFO="/tmp/btctl_fifo"
rm -f "$BTCTL_FIFO"
mkfifo "$BTCTL_FIFO"
stdbuf -i0 -o0 -e0 bluetoothctl < "$BTCTL_FIFO" &
BTCTL_PID=$!
exec 7>"$BTCTL_FIFO"

echo "power on" >&7
sleep 1
echo "agent on" >&7
sleep 1
echo "connect $MAC" >&7
sleep 5

# Verify connection actually succeeded
if ! check_connected; then
    echo "Connection failed, device not connected"
    set_status "disconnected"
    echo "quit" >&7 2>/dev/null
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_FIFO"
    exit 1
fi

echo "menu gatt" >&7
sleep 1
echo "select-attribute 00002a56-0000-1000-8000-00805f9b34fb" >&7
sleep 1

# Wait 3 seconds then verify connection is still alive
# (wristband may disconnect immediately after GATT discovery)
sleep 3
if ! check_connected; then
    echo "Connection dropped during setup, retrying"
    set_status "disconnected"
    echo "quit" >&7 2>/dev/null
    exec 7>&- 2>/dev/null
    kill $BTCTL_PID 2>/dev/null
    rm -f "$BTCTL_FIFO"
    exit 1
fi

set_status "connected $MAC"
echo "Connected and ready"

# Command loop with connection check every 3 seconds
CHECK_COUNT=0
while true; do
    if read -t 1 line <"$FIFO" 2>/dev/null; then
        if [ "$line" == "buzz" ]; then
            echo "write \"0x01\"" >&7
        elif [ "$line" == "quit" ]; then
            break
        fi
    fi
    CHECK_COUNT=$((CHECK_COUNT + 1))
    if [ $CHECK_COUNT -ge 3 ]; then
        CHECK_COUNT=0
        if ! check_connected; then
            echo "Device disconnected"
            break
        fi
    fi
done

set_status "disconnected"
echo "disconnect $MAC" >&7 2>/dev/null
echo "quit" >&7 2>/dev/null
exec 7>&- 2>/dev/null
kill $BTCTL_PID 2>/dev/null
rm -f "$BTCTL_FIFO"
echo "Daemon exited"
