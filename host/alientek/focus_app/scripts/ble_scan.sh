#!/bin/bash
# Scans for ESP32-Band and prints MAC address to stdout.
# Usage: ble_scan.sh [target_name] [timeout_seconds]
# Exit code 0 = found, 1 = not found.

TARGET="${1:-ESP32-Band}"
TIMEOUT="${2:-12}"

rfkill unblock bluetooth 2>/dev/null
hciconfig hci0 up 2>/dev/null

OUTPUT=$(
    {
        echo "power on"
        sleep 0.5
        echo "agent on"
        sleep 0.5
        echo "scan on"
        sleep "$TIMEOUT"
        echo "scan off"
        sleep 0.5
        echo "quit"
    } | bluetoothctl 2>&1
)

MAC=$(printf "%s\n" "$OUTPUT" \
    | grep "$TARGET" \
    | grep -oE "Device ([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}" \
    | awk '{print $2}' \
    | head -n 1)

if [ -n "$MAC" ]; then
    echo "$MAC"
    exit 0
else
    exit 1
fi
