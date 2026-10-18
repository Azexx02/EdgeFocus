#!/usr/bin/env python3
"""Probe, deploy, or stop the EdgeFocus M33 fan firmware.

Linux owns the STM32 timer/PWM platform devices after boot.  Merely unexporting
the PWM channel does not release the timer registers, clocks, or reset line, so
deployment performs an explicit child-then-parent driver handoff before M33 is
started.  A failed start restores the Linux driver bindings.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
import posixpath
import re
import shlex
import sys
import time
from pathlib import Path

import paramiko


DEFAULT_BOARD = "192.168.10.174"
FIRMWARE_NAME = "EdgeFocus_FanController_CM33_NonSecure_stripped.elf"
FALLBACK_FIRMWARE_NAME = "OpenAMP_TTY_echo_CM33_NonSecure.elf"
FINAL_BRINGUP_STAGE = 9
PWM_DRIVER = "/sys/bus/platform/drivers/stm32-pwm"
TIMER_DRIVER = "/sys/bus/platform/drivers/stm32-timers"
PWM_DEVICE = "40270000.timer:pwm"
TIMER_DEVICE = "40270000.timer"


class RemoteError(RuntimeError):
    pass


@dataclass
class LinuxTimerHandoff:
    pwm_was_bound: bool = False
    timer_was_bound: bool = False


def run(
    ssh: paramiko.SSHClient,
    command: str,
    *,
    check: bool = True,
    timeout: float = 15,
) -> str:
    print(f"[remote] {command}")
    _stdin, stdout, stderr = ssh.exec_command(command, timeout=timeout)
    status = stdout.channel.recv_exit_status()
    output = stdout.read().decode("utf-8", errors="replace").strip()
    error = stderr.read().decode("utf-8", errors="replace").strip()
    if output:
        print(output)
    if error:
        print(error, file=sys.stderr)
    if check and status != 0:
        raise RemoteError(f"remote command failed ({status}): {command}")
    return output


def read_remote(ssh: paramiko.SSHClient, path: str) -> str:
    return run(ssh, f"cat {shlex.quote(path)}").replace("\x00", "").strip()


def connect(board: str, username: str, password: str) -> paramiko.SSHClient:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(
        board,
        username=username,
        password=password,
        timeout=8,
        banner_timeout=8,
        auth_timeout=8,
    )
    return ssh


def probe(ssh: paramiko.SSHClient, remoteproc: str, pwm_chip: str) -> None:
    print("\n== fan service ==")
    run(ssh, "systemctl is-active fan-temp-control.service", check=False)
    run(ssh, "systemctl is-enabled fan-temp-control.service", check=False)
    print("\n== M33 remoteproc ==")
    for attribute in ("name", "state", "firmware", "fw_format"):
        path = posixpath.join(remoteproc, attribute)
        value = run(ssh, f"cat {shlex.quote(path)}", check=False)
        print(f"{attribute}: {value or '<unavailable>'}")
    print("\n== RPMsg and PWM ==")
    run(ssh, "ls -l /dev/ttyRPMSG*", check=False)
    run(ssh, f"ls -la {shlex.quote(pwm_chip)}", check=False)
    run(ssh, f"ls -l {shlex.quote(PWM_DRIVER)}", check=False)
    run(ssh, f"ls -l {shlex.quote(TIMER_DRIVER)}", check=False)
    run(ssh, "grep -E 'PB10|PB11|gpio-10|gpio-11' /sys/kernel/debug/gpio", check=False)


def stop_remoteproc(ssh: paramiko.SSHClient, remoteproc: str) -> None:
    state_path = posixpath.join(remoteproc, "state")
    if read_remote(ssh, state_path) == "running":
        run(ssh, f"echo stop > {shlex.quote(state_path)}")


def release_linux_pwm(
    ssh: paramiko.SSHClient, pwm_chip: str, pwm_channel: int
) -> None:
    channel_path = posixpath.join(pwm_chip, f"pwm{pwm_channel}")
    exists = run(
        ssh,
        f"if test -d {shlex.quote(channel_path)}; then echo yes; else echo no; fi",
    )
    if exists == "yes":
        enable_path = posixpath.join(channel_path, "enable")
        run(
            ssh,
            f"if test -w {shlex.quote(enable_path)}; "
            f"then echo 0 > {shlex.quote(enable_path)}; fi",
        )
        run(
            ssh,
            f"echo {pwm_channel} > {shlex.quote(posixpath.join(pwm_chip, 'unexport'))}",
        )


def driver_has_device(ssh: paramiko.SSHClient, driver: str, device: str) -> bool:
    device_link = posixpath.join(driver, device)
    return run(
        ssh,
        f"if test -L {shlex.quote(device_link)}; then echo yes; else echo no; fi",
    ) == "yes"


def unbind_device(ssh: paramiko.SSHClient, driver: str, device: str) -> bool:
    if not driver_has_device(ssh, driver, device):
        return False
    run(
        ssh,
        f"printf '%s' {shlex.quote(device)} > {shlex.quote(posixpath.join(driver, 'unbind'))}",
    )
    if driver_has_device(ssh, driver, device):
        raise RemoteError(f"driver did not release {device}: {driver}")
    return True


def bind_device(ssh: paramiko.SSHClient, driver: str, device: str) -> None:
    if driver_has_device(ssh, driver, device):
        return
    run(
        ssh,
        f"printf '%s' {shlex.quote(device)} > {shlex.quote(posixpath.join(driver, 'bind'))}",
    )
    if not driver_has_device(ssh, driver, device):
        raise RemoteError(f"driver did not bind {device}: {driver}")


def handoff_timer_to_m33(ssh: paramiko.SSHClient) -> LinuxTimerHandoff:
    """Release the Linux child before its multifunction-timer parent."""
    handoff = LinuxTimerHandoff()
    handoff.pwm_was_bound = unbind_device(ssh, PWM_DRIVER, PWM_DEVICE)
    try:
        handoff.timer_was_bound = unbind_device(ssh, TIMER_DRIVER, TIMER_DEVICE)
    except Exception:
        if handoff.pwm_was_bound:
            bind_device(ssh, PWM_DRIVER, PWM_DEVICE)
        raise
    return handoff


def restore_timer_to_linux(
    ssh: paramiko.SSHClient, handoff: LinuxTimerHandoff
) -> None:
    """Restore the parent first; it recreates the PWM child platform device."""
    if handoff.timer_was_bound:
        bind_device(ssh, TIMER_DRIVER, TIMER_DEVICE)
    if handoff.pwm_was_bound:
        bind_device(ssh, PWM_DRIVER, PWM_DEVICE)


def wait_for_remote_path(
    ssh: paramiko.SSHClient, path: str, timeout_seconds: float
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        exists = run(
            ssh,
            f"if test -e {shlex.quote(path)}; then echo yes; else echo no; fi",
            check=False,
        )
        if exists == "yes":
            return
        time.sleep(0.25)
    raise RemoteError(f"timed out waiting for {path}")


def collect_stage_trace(
    ssh: paramiko.SSHClient,
    tty_path: str,
    timeout_seconds: float,
    expected_stage: int,
) -> tuple[list[tuple[int, str]], list[tuple[str, str]], list[tuple[str, str]]]:
    transport = ssh.get_transport()
    if transport is None or not transport.is_active():
        raise RemoteError("SSH transport is not active while reading stage trace")

    channel = transport.open_session(timeout=5)
    channel.exec_command(f"cat {shlex.quote(tty_path)}")
    payload = bytearray()
    deadline = time.monotonic() + timeout_seconds
    try:
        # Keep the reader open before writing so no early M33 marker can be
        # dropped between separate tty opens.  The probe also resolves the
        # remote endpoint destination used by rpmsg_send.
        run(
            ssh,
            f"printf 'EFAN_PROBE\\n' > {shlex.quote(tty_path)}",
        )
        while time.monotonic() < deadline:
            if channel.recv_ready():
                payload.extend(channel.recv(4096))
                if f"EFAN_STAGE:{expected_stage}:OK".encode() in payload:
                    break
            elif channel.exit_status_ready():
                break
            else:
                time.sleep(0.02)
    finally:
        channel.close()

    decoded = payload.decode("ascii", errors="ignore")
    stages = [
        (int(stage), status)
        for stage, status in re.findall(
            r"EFAN_STAGE:([0-9]):(BEGIN|OK|FAIL)", decoded
        )
    ]
    pwm_steps = re.findall(
        r"EFAN_PWM:(RESET|CCXN|MOE|CEN|CCR|APP_CFG|NONCE|CTOR|INIT):(BEGIN|OK|FAIL)",
        decoded,
    )
    diagnostics = re.findall(r"EFAN_DIAG:([A-Z0-9_]+):([01])", decoded)
    print("\n== M33 bring-up stages ==")
    if stages:
        for stage, status in stages:
            print(f"stage {stage}: {status}")
    else:
        print("<no stage markers received>")
    print("\n== PWM start steps ==")
    if pwm_steps:
        for step, status in pwm_steps:
            print(f"{step}: {status}")
    else:
        print("<no PWM start markers received>")
    print("\n== TIM17 diagnostics ==")
    if diagnostics:
        for name, value in diagnostics:
            print(f"{name}: {value}")
    else:
        print("<no diagnostics received>")
    return stages, pwm_steps, diagnostics


def start_firmware(
    ssh: paramiko.SSHClient, remoteproc: str, firmware_name: str
) -> None:
    firmware_path = posixpath.join(remoteproc, "firmware")
    state_path = posixpath.join(remoteproc, "state")
    run(ssh, f"echo {shlex.quote(firmware_name)} > {shlex.quote(firmware_path)}")
    run(ssh, f"echo start > {shlex.quote(state_path)}")


def rollback_to_linux_echo(
    ssh: paramiko.SSHClient,
    remoteproc: str,
    handoff: LinuxTimerHandoff,
    fallback_name: str,
) -> None:
    stop_remoteproc(ssh, remoteproc)
    restore_timer_to_linux(ssh, handoff)
    start_firmware(ssh, remoteproc, fallback_name)
    wait_for_remote_path(ssh, "/dev/ttyRPMSG0", 5)


def upload_firmware(
    ssh: paramiko.SSHClient, firmware: Path, remote_name: str
) -> None:
    remote_dir = "/lib/firmware"
    remote_path = posixpath.join(remote_dir, remote_name)
    temporary_path = remote_path + ".uploading"
    run(ssh, f"mkdir -p {shlex.quote(remote_dir)}")
    sftp = ssh.open_sftp()
    try:
        sftp.put(str(firmware), temporary_path)
    finally:
        sftp.close()
    run(ssh, f"chmod 0644 {shlex.quote(temporary_path)}")
    run(ssh, f"mv -f {shlex.quote(temporary_path)} {shlex.quote(remote_path)}")


def deploy(args: argparse.Namespace, ssh: paramiko.SSHClient) -> None:
    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise FileNotFoundError(f"firmware not found: {firmware}")

    fw_format = read_remote(ssh, posixpath.join(args.remoteproc, "fw_format"))
    if fw_format != "ELF":
        raise RemoteError(
            f"remoteproc expects {fw_format!r}, but this build is an unsigned ELF"
        )

    upload_firmware(ssh, firmware, args.remote_name)

    # Stop every possible writer before changing pin ownership.
    run(ssh, "systemctl stop edgefocus-fan-bridge.service", check=False)
    stop_remoteproc(ssh, args.remoteproc)
    run(ssh, "systemctl stop fan-temp-control.service", check=False)
    run(ssh, "systemctl disable fan-temp-control.service", check=False)
    release_linux_pwm(ssh, args.pwm_chip, args.pwm_channel)
    handoff = handoff_timer_to_m33(ssh)

    state_path = posixpath.join(args.remoteproc, "state")
    try:
        start_firmware(ssh, args.remoteproc, args.remote_name)
        wait_for_remote_path(ssh, "/dev/ttyRPMSG0", args.stage_timeout)
        state = read_remote(ssh, state_path)
        if state != "running":
            raise RemoteError(f"M33 did not reach running state: {state!r}")
        stages, pwm_steps, diagnostics = collect_stage_trace(
            ssh, "/dev/ttyRPMSG0", args.stage_timeout, args.expected_stage
        )
        if (args.expected_stage, "OK") not in stages:
            last = pwm_steps[-1] if pwm_steps else (stages[-1] if stages else None)
            raise RemoteError(
                "M33 bring-up did not reach "
                f"stage {args.expected_stage} OK; last marker={last!r}"
            )
        time.sleep(args.stability_seconds)
        if read_remote(ssh, state_path) != "running":
            raise RemoteError("M33 stopped during the stability window")
        run(ssh, "test -e /dev/ttyRPMSG0")
    except Exception:
        rollback_to_linux_echo(
            ssh, args.remoteproc, handoff, args.fallback_name
        )
        raise

    print("\nM33 firmware is running and /dev/ttyRPMSG0 exists.")
    print("The Linux bridge remains stopped until its separate board test.")


def default_firmware() -> Path:
    repo_root = Path(__file__).resolve().parents[4]
    workspace_root = repo_root.parents[1]
    return (
        workspace_root
        / "_workspace"
        / "b"
        / "m33fan"
        / "artifacts"
        / FIRMWARE_NAME
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("probe", "deploy", "stop"))
    parser.add_argument("--board", default=DEFAULT_BOARD)
    parser.add_argument("--username", default="root")
    parser.add_argument("--password", default=os.environ.get("EDGEFOCUS_BOARD_PASSWORD", ""))
    parser.add_argument("--remoteproc", default="/sys/class/remoteproc/remoteproc0")
    parser.add_argument("--pwm-chip", default="/sys/class/pwm/pwmchip0")
    parser.add_argument("--pwm-channel", type=int, default=0)
    parser.add_argument("--firmware", type=Path, default=default_firmware())
    parser.add_argument("--remote-name", default=FIRMWARE_NAME)
    parser.add_argument("--fallback-name", default=FALLBACK_FIRMWARE_NAME)
    parser.add_argument("--stage-timeout", type=float, default=10.0)
    parser.add_argument("--stability-seconds", type=float, default=30.0)
    parser.add_argument(
        "--expected-stage", type=int, choices=range(10), default=FINAL_BRINGUP_STAGE
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        ssh = connect(args.board, args.username, args.password)
        try:
            if args.action == "probe":
                probe(ssh, args.remoteproc, args.pwm_chip)
            elif args.action == "deploy":
                deploy(args, ssh)
            else:
                run(ssh, "systemctl stop edgefocus-fan-bridge.service", check=False)
                stop_remoteproc(ssh, args.remoteproc)
                restore_timer_to_linux(
                    ssh,
                    LinuxTimerHandoff(pwm_was_bound=True, timer_was_bound=True),
                )
                print("M33 stopped and Linux timer/PWM driver bindings restored.")
                print("The legacy fan service remains disabled.")
        finally:
            ssh.close()
    except (OSError, paramiko.SSHException, RemoteError, FileNotFoundError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
