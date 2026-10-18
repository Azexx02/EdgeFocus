#!/usr/bin/env python3
"""Temperature based fan control example for the self-developed STM32MP257 board.

Hardware mapping:
- PB10 -> timer PWM, exposed as /sys/class/pwm/pwmchip0/pwm0
- PB11 -> fan tach input, available as GPIOB line 11 after can@402d0000 is disabled

The board's PB10 fan PWM path is inverted by the external driver circuit:
smaller duty_cycle values make the fan run faster.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import time
from pathlib import Path


PWM_CHIP = Path("/sys/class/pwm/pwmchip0")
PWM = PWM_CHIP / "pwm0"
PERIOD_NS = 40_000  # 25 kHz, common 4-wire fan PWM frequency.

# (minimum temperature in millicelsius, duty_cycle in ns)
# The table is sorted cold -> hot. Lower duty is faster on this board.
TEMP_TABLE = [
    (0, 36_000),
    (45_000, 30_000),
    (55_000, 22_000),
    (65_000, 14_000),
    (75_000, 8_000),
]


def write_text(path: Path, value: str) -> None:
    path.write_text(value, encoding="ascii")


def read_int(path: Path) -> int:
    return int(path.read_text(encoding="ascii").strip())


def find_thermal_zone(preferred_type: str) -> Path:
    zones = sorted(glob.glob("/sys/class/thermal/thermal_zone*"))
    fallback: Path | None = None
    for zone_name in zones:
        zone = Path(zone_name)
        try:
            zone_type = (zone / "type").read_text(encoding="ascii").strip()
        except OSError:
            continue
        if fallback is None:
            fallback = zone
        if zone_type == preferred_type:
            return zone
    if fallback is None:
        raise RuntimeError("no thermal_zone found")
    return fallback


def ensure_pwm() -> None:
    if not PWM.exists():
        write_text(PWM_CHIP / "export", "0")
        for _ in range(20):
            if PWM.exists():
                break
            time.sleep(0.05)
    if not PWM.exists():
        raise RuntimeError(f"{PWM} was not created")

    enable = PWM / "enable"
    try:
        write_text(enable, "0")
    except OSError:
        pass
    write_text(PWM / "period", str(PERIOD_NS))
    try:
        write_text(PWM / "polarity", "normal")
    except OSError:
        pass
    write_text(enable, "1")


def duty_for_temp(temp_mc: int) -> int:
    duty = TEMP_TABLE[0][1]
    for threshold, candidate in TEMP_TABLE:
        if temp_mc >= threshold:
            duty = candidate
    return duty


def set_duty(duty_ns: int) -> None:
    write_text(PWM / "duty_cycle", str(duty_ns))


def read_rpm(sample_seconds: float, pulses_per_rev: int) -> int | None:
    if sample_seconds <= 0:
        return None
    cmd = [
        "timeout",
        f"{sample_seconds}",
        "gpiomon",
        "-c",
        "gpiochip1",
        "-e",
        "rising",
        "-F",
        "x",
        "11",
    ]
    try:
        result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    except FileNotFoundError:
        return None
    count = len([line for line in result.stdout.splitlines() if line.strip()])
    return round(count / sample_seconds * 60 / pulses_per_rev)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--thermal-type", default="cpu-thermal")
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--tach-sample", type=float, default=1.0)
    parser.add_argument("--pulses-per-rev", type=int, default=2)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()

    zone = find_thermal_zone(args.thermal_type)
    ensure_pwm()

    while True:
        try:
            temp_mc = read_int(zone / "temp")
            duty = duty_for_temp(temp_mc)
            set_duty(duty)
            rpm = read_rpm(args.tach_sample, args.pulses_per_rev)
            rpm_text = "unknown" if rpm is None else str(rpm)
            print(
                f"zone={zone.name} temp_c={temp_mc / 1000:.1f} "
                f"duty_ns={duty} rpm={rpm_text}",
                flush=True,
            )
        except Exception as exc:
            # Fail fast to high speed if temperature reading/control fails.
            try:
                set_duty(8_000)
            finally:
                print(f"fan control error: {exc}", flush=True)
                raise

        if args.once:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
