#!/bin/bash

# Ensure Bluetooth is unblocked and up
rfkill unblock bluetooth
hciconfig hci0 up

# Start bluetoothctl in background
coproc btc { stdbuf -i0 -o0 -e0 bluetoothctl; }

# Initialize bluetooth properly based on ATK docs
echo "power on" >&${btc[1]}
sleep 0.5
echo "agent on" >&${btc[1]}
sleep 0.5
echo "discoverable on" >&${btc[1]}
sleep 0.5

echo "scan on" >&${btc[1]}

MAC=""
start_time=$(date +%s)

# Read output from bluetoothctl until we find the device or timeout (15 seconds)
while IFS= read -r -t 1 -u ${btc[0]} line || true; do
    if echo "$line" | grep -q "ESP32-Band"; then
        MAC=$(echo "$line" | grep -oE "Device ([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}" | awk '{print $2}' | head -n 1)
        if [ -n "$MAC" ]; then
            break
        fi
    fi
    
    current_time=$(date +%s)
    elapsed=$((current_time - start_time))
    if [ $elapsed -ge 15 ]; then
        break
    fi
done

echo "scan off" >&${btc[1]}
sleep 1

if [ -n "$MAC" ]; then
    echo "Found ESP32-Band at MAC: $MAC"
    echo "connect $MAC" >&${btc[1]}
    sleep 3
    echo "menu gatt" >&${btc[1]}
    sleep 1
    echo "select-attribute 00002a56-0000-1000-8000-00805f9b34fb" >&${btc[1]}
    sleep 1
    echo "write \"0x01\"" >&${btc[1]}
    sleep 1
    echo "back" >&${btc[1]}
    sleep 0.5
    echo "disconnect $MAC" >&${btc[1]}
    sleep 1
else
    echo "ESP32-Band not found during scan"
fi

echo "quit" >&${btc[1]}
