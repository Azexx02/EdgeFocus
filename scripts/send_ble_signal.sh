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
echo "default-agent" >&${btc[1]}
sleep 0.5

echo "scan le" >&${btc[1]}

MAC=""
start_time=$(date +%s)

# Usage: send_ble_signal.sh [target_name]
TARGET_NAME="${1:-EdgefocusBand}"

# Read output from bluetoothctl until we find the device or timeout (15 seconds)
while IFS= read -r -t 1 -u ${btc[0]} line || true; do
    if echo "$line" | grep -q "$TARGET_NAME"; then
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

if [ -n "$MAC" ]; then
    echo "Found $TARGET_NAME at MAC: $MAC"
    echo "connect $MAC" >&${btc[1]}
    sleep 10
    echo "scan off" >&${btc[1]}
    sleep 1
    DEV_PATH="/org/bluez/hci0/dev_${MAC//:/_}"
    CHAR_PATH="${DEV_PATH}/service001f/char0024"
    gdbus call --system \
        --dest org.bluez \
        --object-path "$CHAR_PATH" \
        --method org.bluez.GattCharacteristic1.WriteValue \
        "[byte 0x01, 0x03, 0x03]" "{}"
    sleep 1
    echo "disconnect $MAC" >&${btc[1]}
    sleep 1
else
    echo "scan off" >&${btc[1]}
    sleep 1
    echo "$TARGET_NAME not found during scan"
fi

echo "quit" >&${btc[1]}
