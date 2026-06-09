#!/bin/bash

# ble_daemon.sh
# A persistent background daemon that keeps the BLE connection to ESP32-Band alive
# and listens for instant commands via a named pipe.

TARGET_NAME="ESP32-Band"
FIFO="/tmp/ble_cmd_fifo"

# Ensure Bluetooth is unblocked and up
rfkill unblock bluetooth
hciconfig hci0 up

# Clean up and create FIFO
rm -f $FIFO
mkfifo $FIFO
chmod 0666 $FIFO

# Start bluetoothctl in background
coproc btc { stdbuf -i0 -o0 -e0 bluetoothctl; }

# Initialize bluetooth properly
echo "power on" >&${btc[1]}
sleep 0.5
echo "agent on" >&${btc[1]}
sleep 0.5
while true; do
    echo "Scanning for $TARGET_NAME..."
    echo "devices" >&${btc[1]}
    sleep 0.5
    echo "scan on" >&${btc[1]}
    MAC=""
    start_time=$(date +%s)
    
    while IFS= read -r -t 1 -u ${btc[0]} line || true; do
        if echo "$line" | grep -q "$TARGET_NAME"; then
            MAC=$(echo "$line" | grep -oE "Device ([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}" | awk '{print $2}' | head -n 1)
            if [ -n "$MAC" ]; then
                break
            fi
        fi
        
        current_time=$(date +%s)
        if [ $((current_time - start_time)) -ge 15 ]; then
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
        
        echo "BLE Daemon ready. Listening on $FIFO..."
        
        # Persistent loop listening to the FIFO
        quit_received=0
        while true; do
            if read line <$FIFO; then
                if [ "$line" == "buzz" ]; then
                    echo "write \"0x01\"" >&${btc[1]}
                    echo "Buzz sent!"
                elif [ "$line" == "quit" ]; then
                    echo "Quit command received. Shutting down daemon."
                    quit_received=1
                    break
                fi
            fi
        done
        
        echo "back" >&${btc[1]}
        sleep 0.5
        echo "disconnect $MAC" >&${btc[1]}
        sleep 1
        
        if [ $quit_received -eq 1 ]; then
            break
        fi
    else
        echo "ESP32-Band not found. Retrying in 5 seconds..."
        sleep 5
    fi
done

echo "quit" >&${btc[1]}
rm -f $FIFO
