"""Install persistent Wi-Fi autoconnect + static IP config on the board.

Defaults match the current EdgeFocus lab network. Override with environment:
  EDGEFOCUS_BOARD_IP       board SSH IP, default 192.168.10.210
  EDGEFOCUS_WIFI_IFACE     Wi-Fi interface, default auto
  EDGEFOCUS_WIFI_SSID      required
  EDGEFOCUS_WIFI_PASSWORD  required
  EDGEFOCUS_WIFI_ADDRESS   default 192.168.10.210/24
  EDGEFOCUS_WIFI_GATEWAY   default 192.168.10.1
  EDGEFOCUS_WIFI_DNS       default 192.168.10.1
"""

import os
import shlex
import sys
import time

import paramiko


BOARD = os.environ.get("EDGEFOCUS_BOARD_IP", "192.168.10.210")
USER = os.environ.get("EDGEFOCUS_BOARD_USER", "root")
PASSWORD = os.environ.get("EDGEFOCUS_BOARD_PASS", "")

WIFI_IFACE = os.environ.get("EDGEFOCUS_WIFI_IFACE", "auto")
WIFI_SSID = os.environ.get("EDGEFOCUS_WIFI_SSID", "")
WIFI_PASSWORD = os.environ.get("EDGEFOCUS_WIFI_PASSWORD", "")
WIFI_ADDRESS = os.environ.get("EDGEFOCUS_WIFI_ADDRESS", "192.168.10.210/24")
WIFI_GATEWAY = os.environ.get("EDGEFOCUS_WIFI_GATEWAY", "192.168.10.1")
WIFI_DNS = os.environ.get("EDGEFOCUS_WIFI_DNS", "192.168.10.1")


def connect_ssh():
    last_error = None
    for attempt in range(1, 6):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            client.connect(
                BOARD,
                username=USER,
                password=PASSWORD,
                timeout=15,
                banner_timeout=15,
                auth_timeout=15,
            )
            return client
        except Exception as exc:
            last_error = exc
            client.close()
            print(f"SSH connect failed on attempt {attempt}/5: {exc}")
            time.sleep(2)
    raise last_error


def run(ssh, cmd, timeout=30):
    print(f"RUN: {cmd}")
    stdin, stdout, stderr = ssh.exec_command(cmd, timeout=timeout)
    out = stdout.read().decode(errors="replace")
    err = stderr.read().decode(errors="replace")
    code = stdout.channel.recv_exit_status()
    if out:
        print(out)
    if err:
        print("STDERR:", err)
    if code != 0:
        raise RuntimeError(f"Command failed ({code}): {cmd}")
    return out


def q(value):
    return shlex.quote(value)


def main():
    ssh = connect_ssh()
    try:
        iface_arg = q(WIFI_IFACE)
        ssid_arg = q(WIFI_SSID)
        pass_arg = q(WIFI_PASSWORD)
        addr_arg = q(WIFI_ADDRESS)
        gw_arg = q(WIFI_GATEWAY)
        dns_arg = q(WIFI_DNS)

        script = f"""
set -eu
IFACE={iface_arg}
SSID={ssid_arg}
PASSWORD={pass_arg}
ADDRESS={addr_arg}
GATEWAY={gw_arg}
DNS={dns_arg}

if [ "$IFACE" = "auto" ]; then
  IFACE="$(iw dev 2>/dev/null | awk '/Interface/ {{print $2}}' | while read dev; do
    if iw dev "$dev" link 2>/dev/null | grep -q "SSID: $SSID"; then
      echo "$dev"
      break
    fi
  done)"
  if [ -z "$IFACE" ]; then
    IFACE="$(iw dev 2>/dev/null | awk '/Interface/ {{print $2}}' | tail -n 1)"
  fi
fi

if [ -z "$IFACE" ]; then
  echo "No Wi-Fi interface found" >&2
  exit 1
fi

echo "Using Wi-Fi interface: $IFACE"
ip link set "$IFACE" up || true

mkdir -p /etc/wpa_supplicant /etc/systemd/network
WPA="/etc/wpa_supplicant/wpa_supplicant-$IFACE.conf"
NET="/etc/systemd/network/25-$IFACE.network"

[ -f "$WPA" ] && cp "$WPA" "$WPA.bak.$(date +%Y%m%d%H%M%S)"
[ -f "$NET" ] && cp "$NET" "$NET.bak.$(date +%Y%m%d%H%M%S)"

wpa_passphrase "$SSID" "$PASSWORD" > "$WPA"
chmod 600 "$WPA"

cat > "$NET" <<EOF
[Match]
Name=$IFACE

[Network]
Address=$ADDRESS
Gateway=$GATEWAY
DNS=$DNS
EOF

systemctl enable "wpa_supplicant@$IFACE.service"
systemctl restart "wpa_supplicant@$IFACE.service"
networkctl reload || true
networkctl reconfigure "$IFACE" || systemctl restart systemd-networkd
sleep 4

echo "--- ip addr $IFACE ---"
ip -4 addr show "$IFACE"
echo "--- route ---"
ip route
echo "--- wpa service ---"
systemctl --no-pager --full status "wpa_supplicant@$IFACE.service" 2>&1 | head -40
"""
        run(ssh, f"sh -c {q(script)}", timeout=60)
    finally:
        ssh.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
