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

is_connected() {
    bluetoothctl info "$MAC" 2>/dev/null | grep -q "Connected: yes"
}

write_alert_frame() {
    local pattern="$1"
    local level="$2"
    local dev_path="/org/bluez/hci0/dev_${MAC//:/_}"
    local char_path="${dev_path}/service001f/char0024"
    local payload="[byte 0x01, 0x${pattern}, 0x${level}]"
    local out
    local rc

    if ! is_connected; then
        echo "WriteValue skipped: device not connected"
        echo "payload=${payload} path=${char_path} rc=not_connected out=device not connected" > /tmp/ble_last_write
        set_status "disconnected"
        return 1
    fi

    echo "payload=${payload} path=${char_path} rc=started out=" > /tmp/ble_last_write
    out=$(timeout 5 gdbus call --system \
        --dest org.bluez \
        --object-path "$char_path" \
        --method org.bluez.GattCharacteristic1.WriteValue \
        "$payload" "{}" 2>&1)
    rc=$?
    echo "WriteValue alert payload=${payload} path=${char_path} rc=${rc} out=${out}"
    echo "payload=${payload} path=${char_path} rc=${rc} out=${out}" > /tmp/ble_last_write
    return "$rc"
}

# Start bluetoothctl via coproc
coproc stdbuf -i0 -o0 -e0 bluetoothctl
BT_OUT="${COPROC[0]}"
BT_IN="${COPROC[1]}"
BT_PID="${COPROC_PID}"
sleep 1

echo "power on" >&"$BT_IN"
sleep 1
echo "agent on" >&"$BT_IN"
sleep 1
echo "default-agent" >&"$BT_IN"
sleep 1
echo "scan le" >&"$BT_IN"
sleep 8
echo "connect $MAC" >&"$BT_IN"
sleep 10
echo "scan off" >&"$BT_IN"
sleep 1

# Check connection
CONNECTED=0
# Read pending output
while IFS= read -r -t 2 -u "$BT_OUT" line 2>/dev/null; do
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
    echo "quit" >&"$BT_IN" 2>/dev/null
    exit 1
fi

# GATT setup
echo "menu gatt" >&"$BT_IN"
sleep 1
echo "select-attribute 00002a56-0000-1000-8000-00805f9b34fb" >&"$BT_IN"
sleep 1

set_status "connected $MAC"
echo "Connected"

# Command loop - listen on FIFO for commands from C++.
# Supported commands:
#   buzz
#   alert <level>
# Alert frame follows docs/BLE_Signal_Protocol.md: {0x01, vibration_pattern, level}.
exec 3<>"$FIFO"
last_connected_check=0
while true; do
    date +%s > /tmp/ble_loop_heartbeat
    now=$(date +%s)
    if [ $((now - last_connected_check)) -ge 3 ]; then
        last_connected_check=$now
        if is_connected; then
            set_status "connected $MAC"
        else
            echo "Disconnected"
            set_status "disconnected"
            break
        fi
    fi

    if read -t 1 line <&3 2>/dev/null; then
        echo "line=${line}" > /tmp/ble_last_cmd
        if [ "$line" == "buzz" ]; then
            write_alert_frame "01" "01"
        elif echo "$line" | grep -qE '^alert [0-9]+$'; then
            level=$(echo "$line" | awk '{print $2}')
            if [ "$level" -ge 3 ]; then
                pattern=3
            elif [ "$level" -ge 2 ]; then
                pattern=2
            else
                pattern=1
            fi
            pattern_hex=$(printf "%02x" "$pattern")
            level_hex=$(printf "%02x" "$level")
            write_alert_frame "$pattern_hex" "$level_hex"
        elif [ "$line" == "quit" ]; then
            break
        fi
    fi

    # Check for disconnection without starving FIFO command handling.
    drain_count=0
    while [ "$drain_count" -lt 20 ] && IFS= read -r -t 0.01 -u "$BT_OUT" line 2>/dev/null; do
        drain_count=$((drain_count + 1))
        if echo "$line" | grep -q "disconnected with reason\|Connected: no"; then
            echo "Disconnected"
            break 2
        fi
    done

    # Check if bluetoothctl is alive
    if ! kill -0 "$BT_PID" 2>/dev/null; then
        echo "bluetoothctl died"
        break
    fi
done

exec 3>&-
set_status "disconnected"
echo "quit" >&"$BT_IN" 2>/dev/null
echo "Exited"
