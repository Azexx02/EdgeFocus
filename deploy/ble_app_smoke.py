"""
Host-side BLE smoke test for the EdgeFocus phone-facing GATT service.

Usage:
  python deploy/ble_app_smoke.py
  python deploy/ble_app_smoke.py --address AA:BB:CC:DD:EE:FF
  python deploy/ble_app_smoke.py --name EdgeFocus --cmd get_current_state

Requires a host BLE adapter and the optional Python package `bleak`.
This does not run on the board; it acts like the phone APP central.
"""
import argparse
import asyncio
import json
import sys
from typing import Optional

SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
TX_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"  # phone -> board
RX_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"  # board -> phone


def load_bleak():
    try:
        from bleak import BleakClient, BleakScanner  # type: ignore
    except ImportError:
        print("Missing dependency: bleak. Install it on the host that has BLE access.", file=sys.stderr)
        raise SystemExit(2)
    return BleakClient, BleakScanner


def decode_payload(data: bytes) -> str:
    return data.decode("utf-8", errors="replace").strip("\x00\r\n ")


async def find_device(scanner, name: str, timeout: float) -> Optional[str]:
    print(f"Scanning for {name} advertising {SERVICE_UUID}...")
    devices = await scanner.discover(timeout=timeout, service_uuids=[SERVICE_UUID])
    for device in devices:
        if device.name == name or (device.name and name.lower() in device.name.lower()):
            print(f"Found {device.name} at {device.address}")
            return device.address

    devices = await scanner.discover(timeout=timeout)
    for device in devices:
        if device.name == name or (device.name and name.lower() in device.name.lower()):
            print(f"Found {device.name} at {device.address}")
            return device.address
    return None


async def run(args) -> int:
    BleakClient, BleakScanner = load_bleak()
    address = args.address or await find_device(BleakScanner, args.name, args.scan_timeout)
    if not address:
        print(f"Could not find BLE device named {args.name}", file=sys.stderr)
        return 1

    request = {
        "cmd": args.cmd,
        "id": args.request_id,
    }
    if args.data:
        request["data"] = json.loads(args.data)
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8")

    response_event = asyncio.Event()
    responses = []

    def on_notify(_sender, data: bytearray):
        text = decode_payload(bytes(data))
        print(f"notify FF02: {text}")
        responses.append(text)
        response_event.set()

    print(f"Connecting to {address}...")
    async with BleakClient(address, timeout=args.connect_timeout) as client:
        print("Connected")
        await client.start_notify(RX_UUID, on_notify)
        print(f"write FF01: {payload.decode('utf-8')}")
        await client.write_gatt_char(TX_UUID, payload, response=True)

        try:
            await asyncio.wait_for(response_event.wait(), timeout=args.response_timeout)
        except asyncio.TimeoutError:
            print("No notify response; trying FF02 read...")
            data = await client.read_gatt_char(RX_UUID)
            text = decode_payload(bytes(data))
            print(f"read FF02: {text}")
            responses.append(text)
        finally:
            await client.stop_notify(RX_UUID)

    if not responses:
        return 1

    try:
        parsed = json.loads(responses[-1])
    except json.JSONDecodeError:
        print("Response is not valid JSON", file=sys.stderr)
        return 1

    if parsed.get("status") == "success" or parsed.get("cmd", "").startswith("push_"):
        return 0
    print(f"Unexpected response: {parsed}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="EdgeFocus")
    parser.add_argument("--address")
    parser.add_argument("--cmd", default="get_device_status")
    parser.add_argument("--data", help="JSON object for request.data")
    parser.add_argument("--request-id", type=int, default=1)
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--connect-timeout", type=float, default=12.0)
    parser.add_argument("--response-timeout", type=float, default=5.0)
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
