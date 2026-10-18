import serial, time

ser = serial.Serial('COM7', 115200, timeout=5)
time.sleep(0.5)

def send_cmd(cmd, wait=3):
    ser.write((cmd + '\n').encode())
    time.sleep(wait)
    out = ser.read(ser.in_waiting or 8192).decode(errors='replace')
    return out

print('=== Connecting to WiFi (SSID from environment) ===')
print(send_cmd('wpa_passphrase "$EDGEFOCUS_WIFI_SSID" "$EDGEFOCUS_WIFI_PASSWORD" > /tmp/wpa.conf'))
print(send_cmd('wpa_supplicant -B -i wlan0 -c /tmp/wpa.conf', 5))
print(send_cmd('udhcpc -i wlan0', 5))
print(send_cmd('ip addr show wlan0'))
print('=== Done ===')
ser.close()
