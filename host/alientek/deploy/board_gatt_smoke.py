"""
Board-local GATT smoke test via system D-Bus.

This SSHes to the board, finds the running focus_app D-Bus unique name, calls
FF01 WriteValue with a JSON command, then reads FF02 and decodes the response.
It validates the board-side GATT object path without requiring a second BLE
central device.

Usage:
  python deploy/board_gatt_smoke.py
  EDGEFOCUS_BOARD_IP=192.168.10.229 python deploy/board_gatt_smoke.py --cmd get_current_state
"""
import argparse
import base64
import json
import os
import re
import sys
import time

import paramiko

BOARD_IP = os.environ.get("EDGEFOCUS_BOARD_IP", "192.168.10.210")
BOARD_USER = os.environ.get("EDGEFOCUS_BOARD_USER", "root")
BOARD_PASS = os.environ.get("EDGEFOCUS_BOARD_PASS", "")


def ssh_exec(command: str, timeout: int = 20) -> str:
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        BOARD_IP,
        username=BOARD_USER,
        password=BOARD_PASS,
        look_for_keys=False,
        allow_agent=False,
        timeout=8,
        banner_timeout=8,
        auth_timeout=8,
    )
    try:
        _, stdout, stderr = client.exec_command(command, timeout=timeout)
        out = stdout.read().decode(errors="replace")
        err = stderr.read().decode(errors="replace")
        code = stdout.channel.recv_exit_status()
    finally:
        client.close()
    if code != 0:
        raise RuntimeError(f"remote command failed ({code}):\n{out}\n{err}")
    return out


def busctl_byte_array(payload: bytes) -> str:
    return f"{len(payload)} " + " ".join(str(b) for b in payload)


def decode_busctl_ay(output: str) -> str:
    match = re.search(r"\bay\s+(\d+)\s+(.+)", output.strip(), re.S)
    if not match:
        raise ValueError(f"Could not parse busctl ay output: {output}")
    count = int(match.group(1))
    values = [int(x) for x in re.findall(r"\b\d+\b", match.group(2))]
    if len(values) < count:
        raise ValueError(f"Expected {count} bytes, got {len(values)}: {output}")
    return bytes(values[:count]).decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmd", default="get_device_status")
    parser.add_argument("--data", help="JSON object for request.data")
    parser.add_argument("--data-b64", help="Base64-encoded JSON object for request.data")
    parser.add_argument("--request-id", type=int, default=1)
    parser.add_argument("--start-notify", action="store_true",
                        help="Call StartNotify on FF02 before writing FF01")
    parser.add_argument("--read-after-seconds", type=float, default=0.0,
                        help="Read FF02 again after this delay")
    args = parser.parse_args()

    request = {"cmd": args.cmd, "id": args.request_id}
    if args.data_b64:
        request["data"] = json.loads(base64.b64decode(args.data_b64).decode("utf-8"))
    elif args.data:
        request["data"] = json.loads(args.data)
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8")

    byte_args = busctl_byte_array(payload)
    notify_line = (
        'busctl --system call "$BUS" /com/edgefocus/app/service0/char1 '
        'org.bluez.GattCharacteristic1 StartNotify\n'
        if args.start_notify else ""
    )
    command = f"""
set -eu
BUS=$(busctl --system list --no-pager | awk '$3=="focus_app" {{print $1; exit}}')
if [ -z "$BUS" ]; then
  echo "focus_app D-Bus connection not found" >&2
  exit 2
fi
echo "BUS=$BUS"
busctl --system call "$BUS" /com/edgefocus/app org.freedesktop.DBus.ObjectManager GetManagedObjects >/dev/null
{notify_line}\
busctl --system call "$BUS" /com/edgefocus/app/service0/char0 org.bluez.GattCharacteristic1 WriteValue aya{{sv}} {byte_args} 0
busctl --system call "$BUS" /com/edgefocus/app/service0/char1 org.bluez.GattCharacteristic1 ReadValue a{{sv}} 0
"""
    out = ssh_exec(command)
    print(out.strip())

    response = decode_busctl_ay(out)
    print(f"decoded FF02: {response}")
    parsed = json.loads(response)
    if parsed.get("status") != "success":
        print(f"unexpected response: {parsed}", file=sys.stderr)
        return 1

    if args.read_after_seconds > 0:
        time.sleep(args.read_after_seconds)
        delayed_out = ssh_exec("""
set -eu
BUS=$(busctl --system list --no-pager | awk '$3=="focus_app" {print $1; exit}')
if [ -z "$BUS" ]; then
  echo "focus_app D-Bus connection not found" >&2
  exit 2
fi
echo "BUS=$BUS"
busctl --system call "$BUS" /com/edgefocus/app/service0/char1 org.bluez.GattCharacteristic1 ReadValue a{sv} 0
""")
        print(delayed_out.strip())
        delayed_response = decode_busctl_ay(delayed_out)
        print(f"decoded delayed FF02: {delayed_response}")
        delayed = json.loads(delayed_response)
        if not (delayed.get("status") == "success" or str(delayed.get("cmd", "")).startswith("push_")):
            print(f"unexpected delayed response: {delayed}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
