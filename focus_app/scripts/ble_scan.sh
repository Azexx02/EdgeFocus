#!/bin/bash
# Scans for ESP32-Band and prints MAC address to stdout.
# Usage: ble_scan.sh [target_name] [timeout_seconds]
# Exit code 0 = found, 1 = not found.

TARGET="${1:-ESP32-Band}"
TIMEOUT="${2:-12}"

rfkill unblock bluetooth 2>/dev/null
hciconfig hci0 up 2>/dev/null

coproc btc { stdbuf -i0 -o0 -e0 bluetoothctl; }
sleep 0.5
echo "power on" >&${btc[1]}
sleep 0.5
echo "agent on" >&${btc[1]}
sleep 0.5
echo "scan on" >&${btc[1]}

MAC=""
start_time=$(date +%s)
while IFS= read -r -t 1 -u ${btc[0]} line || true; do
    if echo "$line" | grep -q "$TARGET"; then
        MAC=$(echo "$line" | grep -oE "Device ([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}" | awk '{print $2}' | head -n 1)
        if [ -n "$MAC" ]; then break; fi
    fi
    current_time=$(date +%s)
    if [ $((current_time - start_time)) -ge $TIMEOUT ]; then break; fi
done

echo "scan off" >&${btc[1]}
sleep 0.5
echo "quit" >&${btc[1]}

if [ -n "$MAC" ]; then
    echo "$MAC"
    exit 0
else
    exit 1
fi
