#!/usr/bin/env python3
"""Serialize M33/Linux ownership transitions for the EdgeFocus fan."""

from __future__ import annotations

import argparse
import fcntl
import glob
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time


REMOTE_PROC = Path("/sys/class/remoteproc/remoteproc0")
FIRMWARE = "EdgeFocus_FanController_CM33_NonSecure_stripped.elf"
FIRMWARE_PATH = Path("/lib/firmware") / FIRMWARE
TTY = Path("/dev/ttyRPMSG0")
PWM_DRIVER = Path("/sys/bus/platform/drivers/stm32-pwm")
TIMER_DRIVER = Path("/sys/bus/platform/drivers/stm32-timers")
PWM_DEVICE = "40270000.timer:pwm"
TIMER_DEVICE = "40270000.timer"
RUNTIME = Path("/run/edgefocus")
LOCK = RUNTIME / "fan-transition.lock"
START_MARKER = RUNTIME / "m33-fan-start.json"
STATUS_FILE = RUNTIME / "fan-status.json"
CALIBRATION_FILE = Path("/etc/edgefocus/fan-calibration.json")


class TransitionError(RuntimeError):
    pass


def log(message: str) -> None:
    print(f"edgefocus-fan-transition: {message}", flush=True)


def read_text(path: Path) -> str:
    return path.read_text(encoding="ascii").strip()


def write_text(path: Path, value: str) -> None:
    path.write_text(value, encoding="ascii")


def write_if_different(path: Path, value: str) -> None:
    if path.exists() and read_text(path) == value:
        return
    write_text(path, value)


def run(command: list[str], *, check: bool = True, timeout: float = 15) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, timeout=timeout)
    if check and result.returncode != 0:
        raise TransitionError(
            f"command failed ({result.returncode}): {' '.join(command)}; "
            f"stderr={result.stderr.strip()}"
        )
    return result


def systemctl(*arguments: str, check: bool = False) -> None:
    run(["systemctl", *arguments], check=check)


def driver_bound(driver: Path, device: str) -> bool:
    return (driver / device).is_symlink()


def bind_driver(driver: Path, device: str) -> None:
    if driver_bound(driver, device):
        return
    write_text(driver / "bind", device)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if driver_bound(driver, device):
            return
        time.sleep(0.05)
    raise TransitionError(f"driver did not bind {device}")


def unbind_driver(driver: Path, device: str) -> None:
    if not driver_bound(driver, device):
        return
    write_text(driver / "unbind", device)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if not driver_bound(driver, device):
            return
        time.sleep(0.05)
    raise TransitionError(f"driver did not release {device}")


def find_pwm_chip() -> Path | None:
    for candidate_text in glob.glob("/sys/class/pwm/pwmchip*"):
        candidate = Path(candidate_text)
        try:
            resolved = str((candidate / "device").resolve())
        except OSError:
            continue
        if TIMER_DEVICE in resolved:
            return candidate
    return None


def stop_remoteproc() -> None:
    if (REMOTE_PROC / "state").exists() and read_text(REMOTE_PROC / "state") == "running":
        write_text(REMOTE_PROC / "state", "stop")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if read_text(REMOTE_PROC / "state") != "running":
                return
            time.sleep(0.1)
        raise TransitionError("remoteproc did not stop")


def release_linux_pwm() -> None:
    chip = find_pwm_chip()
    if chip is not None and (chip / "pwm0").is_dir():
        enable = chip / "pwm0" / "enable"
        if enable.exists():
            write_if_different(enable, "0")
        write_text(chip / "unexport", "0")
    unbind_driver(PWM_DRIVER, PWM_DEVICE)
    unbind_driver(TIMER_DRIVER, TIMER_DEVICE)


def wait_for(path: Path, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.1)
    raise TransitionError(f"timed out waiting for {path}")


def wait_until_absent(path: Path, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if not path.exists():
            return
        time.sleep(0.1)
    raise TransitionError(f"timed out waiting for removal of {path}")


def collect_stage_trace(timeout_seconds: float = 10) -> str:
    descriptor = os.open(TTY, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    payload = bytearray()
    try:
        os.write(descriptor, b"EFAN_PROBE\n")
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            readable, _, _ = select.select([descriptor], [], [], 0.2)
            if not readable:
                continue
            try:
                payload.extend(os.read(descriptor, 4096))
            except BlockingIOError:
                continue
            if b"EFAN_STAGE:9:OK" in payload:
                break
    finally:
        os.close(descriptor)
    trace = payload.decode("ascii", errors="ignore")
    (RUNTIME / "m33-stage-trace.log").write_text(trace, encoding="ascii")
    if "EFAN_STAGE:9:OK" not in trace:
        raise TransitionError("M33 bring-up did not reach stage 9")
    return trace


def calibration_approved() -> bool:
    try:
        data = json.loads(CALIBRATION_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        data.get("tach_pulses_per_revolution") == 2
        and (
            data.get("waveform_verified") is True
            or data.get("waveform_verification_waived") is True
        )
        and data.get("autostart_approved") is True
    )


def start_m33(require_calibration: bool, stop_bridge: bool) -> None:
    if require_calibration and not calibration_approved():
        raise TransitionError("autostart calibration gate is not approved")
    if not FIRMWARE_PATH.is_file():
        raise TransitionError(f"firmware is missing: {FIRMWARE_PATH}")

    if stop_bridge:
        systemctl("stop", "edgefocus-fan-bridge.service")
    systemctl("stop", "fan-temp-control.service")
    RUNTIME.mkdir(parents=True, exist_ok=True)
    STATUS_FILE.unlink(missing_ok=True)
    START_MARKER.unlink(missing_ok=True)
    stop_remoteproc()
    wait_until_absent(TTY, 5)
    release_linux_pwm()
    write_text(REMOTE_PROC / "firmware", FIRMWARE)
    write_text(REMOTE_PROC / "state", "start")
    wait_for(TTY, 15)
    collect_stage_trace()
    if read_text(REMOTE_PROC / "state") != "running":
        raise TransitionError("M33 stopped after staged bring-up")
    if driver_bound(TIMER_DRIVER, TIMER_DEVICE) or driver_bound(PWM_DRIVER, PWM_DEVICE):
        raise TransitionError("Linux timer/PWM remained bound while M33 was running")
    marker = {
        "firmware": FIRMWARE,
        "started_monotonic_ms": int(time.monotonic() * 1000),
        "stage": 9,
    }
    START_MARKER.write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
    log("M33 owns TIM17/PB10 and reached stage 9")


def sample_tach(seconds: float = 3.0) -> tuple[int, int]:
    command = [
        "timeout", "-s", "INT", str(seconds), "gpiomon", "--by-name",
        "--edges", "rising", "PB11",
    ]
    result = run(command, check=False, timeout=seconds + 3)
    pulses = sum(1 for line in result.stdout.splitlines() if line.strip())
    rpm = round(pulses * 60 / (2 * seconds))
    return pulses, rpm


def enable_linux_safe_pwm() -> dict[str, int | str]:
    bind_driver(TIMER_DRIVER, TIMER_DEVICE)
    bind_driver(PWM_DRIVER, PWM_DEVICE)
    deadline = time.monotonic() + 3
    chip = find_pwm_chip()
    while chip is None and time.monotonic() < deadline:
        time.sleep(0.05)
        chip = find_pwm_chip()
    if chip is None:
        raise TransitionError("PWM chip did not appear after driver bind")
    if not (chip / "pwm0").is_dir():
        try:
            write_text(chip / "export", "0")
        except OSError as error:
            if error.errno != 16:  # EBUSY means it was already exported.
                raise
        wait_for(chip / "pwm0", 3)
    pwm = chip / "pwm0"
    write_if_different(pwm / "enable", "0")
    write_if_different(pwm / "duty_cycle", "0")
    write_if_different(pwm / "period", "40000")
    write_if_different(pwm / "polarity", "normal")
    write_if_different(pwm / "duty_cycle", "8000")
    write_if_different(pwm / "enable", "1")
    readback = {
        "period_ns": int(read_text(pwm / "period")),
        "duty_ns": int(read_text(pwm / "duty_cycle")),
        "polarity": read_text(pwm / "polarity"),
        "enabled": int(read_text(pwm / "enable")),
    }
    if readback != {
        "period_ns": 40000,
        "duty_ns": 8000,
        "polarity": "normal",
        "enabled": 1,
    }:
        raise TransitionError(f"Linux PWM readback mismatch: {readback}")
    pulses, rpm = sample_tach()
    readback["tach_pulses_3s"] = pulses
    readback["rpm_at_2ppr"] = rpm
    if rpm <= 300:
        raise TransitionError(f"Linux safe PWM produced insufficient tach: {rpm} RPM")
    return readback


def fallback_to_linux() -> None:
    systemctl("stop", "edgefocus-fan-bridge.service")
    RUNTIME.mkdir(parents=True, exist_ok=True)
    stop_remoteproc()
    result = enable_linux_safe_pwm()
    (RUNTIME / "fan-fallback.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    log(f"Linux safe PWM active: {result}")


def healthcheck() -> None:
    if not calibration_approved():
        raise TransitionError("calibration approval is missing")
    deadline = time.monotonic() + 10
    last_error = "status not available"
    while time.monotonic() < deadline:
        try:
            marker = json.loads(START_MARKER.read_text(encoding="utf-8"))
            status = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
            if STATUS_FILE.stat().st_mtime_ns < START_MARKER.stat().st_mtime_ns:
                raise TransitionError("status predates current M33 start")
            if read_text(REMOTE_PROC / "state") != "running":
                raise TransitionError("remoteproc is not running")
            if read_text(REMOTE_PROC / "firmware") != FIRMWARE:
                raise TransitionError("unexpected remoteproc firmware")
            if driver_bound(TIMER_DRIVER, TIMER_DEVICE) or driver_bound(PWM_DRIVER, PWM_DEVICE):
                raise TransitionError("Linux PWM is bound during M33 ownership")
            if marker.get("firmware") != FIRMWARE or marker.get("stage") != 9:
                raise TransitionError("invalid current-run marker")
            required_true = ("thermal_ready", "transport_connected", "protocol_ready")
            if not all(status.get(field) is True for field in required_true):
                raise TransitionError("bridge protocol/thermal is not ready")
            if status.get("control_state") != "CONTROLLED":
                raise TransitionError("M33 has not entered CONTROLLED")
            if status.get("current_faults") != 0:
                raise TransitionError(f"M33 current faults={status.get('current_faults')}")
            age = int(time.monotonic() * 1000) - int(status.get("last_status_rx_ms", 0))
            if age < 0 or age > 2500:
                raise TransitionError(f"stale M33 status age={age}ms")
            if int(status.get("m33_boot_id", 0)) == 0:
                raise TransitionError("M33 boot id is missing")
            rpm = int(status.get("rpm", 0))
            if not 300 < rpm <= 7500:
                raise TransitionError(f"tach RPM outside safety window: {rpm}")
            if status.get("curve_band") == 0 and not 600 <= rpm <= 1500:
                raise TransitionError(f"normal-band RPM outside startup gate: {rpm}")
            log(f"healthcheck passed: boot={status['m33_boot_id']} rpm={rpm}")
            return
        except (OSError, ValueError, KeyError, json.JSONDecodeError, TransitionError) as error:
            last_error = str(error)
            time.sleep(0.25)
    raise TransitionError(f"startup healthcheck failed: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("start", "fallback", "healthcheck"))
    parser.add_argument("--require-calibration", action="store_true")
    parser.add_argument(
        "--stop-bridge",
        action="store_true",
        help="stop an existing bridge before a manual M33 ownership transition",
    )
    args = parser.parse_args()
    RUNTIME.mkdir(parents=True, exist_ok=True)
    try:
        with LOCK.open("a+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if args.action == "start":
                start_m33(args.require_calibration, args.stop_bridge)
            elif args.action == "fallback":
                fallback_to_linux()
            else:
                healthcheck()
    except (OSError, subprocess.SubprocessError, TransitionError) as error:
        log(f"ERROR: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
