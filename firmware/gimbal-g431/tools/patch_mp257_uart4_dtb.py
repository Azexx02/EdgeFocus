"""Enable STM32MP257 UART4 on PB6/PB7 in an existing flattened DTB."""

import argparse
from pathlib import Path

from pyfdt.pyfdt import (
    FdtBlobParse,
    FdtNode,
    FdtProperty,
    FdtPropertyStrings,
    FdtPropertyWords,
)


PINCTRL_PATH = "/soc@0/pinctrl@44240000"
UART4_PATH = "/soc@0/bus@42080000/serial@40100000"


def property_by_name(node, name):
    for item in node:
        if item.get_name() == name:
            return item
    return None


def replace_property(node, prop):
    old = property_by_name(node, prop.get_name())
    if old is not None:
        node.remove(old.get_name())
    node.append(prop)


def pin_group(name, pinmux, phandle, rx=False, idle=False, sleep=False):
    group = FdtNode(name)
    group.append(FdtPropertyWords("phandle", [phandle]))

    pins = FdtNode("pins" if sleep else ("pins2" if rx else "pins1"))
    pins.append(FdtPropertyWords("pinmux", pinmux))
    if not sleep:
        pins.append(FdtProperty("bias-pull-up" if rx else "bias-disable"))
    if not rx and not idle and not sleep:
        pins.append(FdtProperty("drive-push-pull"))
        pins.append(FdtPropertyWords("slew-rate", [0]))
    group.append(pins)
    return group


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with args.input.open("rb") as stream:
        fdt = FdtBlobParse(stream).to_fdt()

    root = fdt.get_rootnode()
    used_phandles = []
    pb67_users = []
    for path, items in root.walk():
        item = items[-1] if items else None
        if isinstance(item, FdtPropertyWords):
            if item.get_name() == "phandle":
                used_phandles.extend(item.words)
            if item.get_name() == "pinmux" and any(
                (word >> 8) in (22, 23) for word in item.words
            ):
                pb67_users.append((path, item.words))
    if pb67_users:
        raise RuntimeError(f"PB6/PB7 already used by pinctrl: {pb67_users}")

    pinctrl = fdt.resolve_path(PINCTRL_PATH)
    uart4 = fdt.resolve_path(UART4_PATH)
    aliases = fdt.resolve_path("/aliases")
    if pinctrl is None or uart4 is None or aliases is None:
        raise RuntimeError("Expected STM32MP257 pinctrl/UART4 nodes were not found")

    serial3 = property_by_name(aliases, "serial3")
    if serial3 is not None and serial3.strings != [UART4_PATH]:
        raise RuntimeError(f"serial3 alias is already used: {serial3.strings}")

    for name in ("uart4-0", "uart4-idle-0", "uart4-sleep-0"):
        if property_by_name(pinctrl, name) is not None:
            raise RuntimeError(f"Pinctrl node already exists: {name}")

    first = max(used_phandles) + 1
    # STM32_PINMUX('B', 6/7, AF4): (global pin number << 8) | function.
    pb6_uart4_rx = (22 << 8) | 4
    pb7_uart4_tx = (23 << 8) | 4
    pb6_analog = (22 << 8) | 17
    pb7_analog = (23 << 8) | 17

    active = FdtNode("uart4-0")
    active.append(FdtPropertyWords("phandle", [first]))
    tx = FdtNode("pins1")
    tx.append(FdtPropertyWords("pinmux", [pb7_uart4_tx]))
    tx.append(FdtProperty("bias-disable"))
    tx.append(FdtProperty("drive-push-pull"))
    tx.append(FdtPropertyWords("slew-rate", [0]))
    active.append(tx)
    rx = FdtNode("pins2")
    rx.append(FdtPropertyWords("pinmux", [pb6_uart4_rx]))
    rx.append(FdtProperty("bias-pull-up"))
    active.append(rx)
    pinctrl.append(active)

    idle = FdtNode("uart4-idle-0")
    idle.append(FdtPropertyWords("phandle", [first + 1]))
    idle_tx = FdtNode("pins1")
    idle_tx.append(FdtPropertyWords("pinmux", [pb7_analog]))
    idle.append(idle_tx)
    idle_rx = FdtNode("pins2")
    idle_rx.append(FdtPropertyWords("pinmux", [pb6_uart4_rx]))
    idle_rx.append(FdtProperty("bias-pull-up"))
    idle.append(idle_rx)
    pinctrl.append(idle)

    sleep = FdtNode("uart4-sleep-0")
    sleep.append(FdtPropertyWords("phandle", [first + 2]))
    sleep_pins = FdtNode("pins")
    sleep_pins.append(FdtPropertyWords("pinmux", [pb7_analog, pb6_analog]))
    sleep.append(sleep_pins)
    pinctrl.append(sleep)

    replace_property(uart4, FdtPropertyStrings("status", ["okay"]))
    replace_property(
        uart4, FdtPropertyStrings("pinctrl-names", ["default", "idle", "sleep"])
    )
    replace_property(uart4, FdtPropertyWords("pinctrl-0", [first]))
    replace_property(uart4, FdtPropertyWords("pinctrl-1", [first + 1]))
    replace_property(uart4, FdtPropertyWords("pinctrl-2", [first + 2]))
    replace_property(aliases, FdtPropertyStrings("serial3", [UART4_PATH]))

    blob = fdt.to_dtb()
    args.output.write_bytes(blob)

    # Reparse the output before it can be installed on the target.
    with args.output.open("rb") as stream:
        checked = FdtBlobParse(stream).to_fdt()
    checked_uart4 = checked.resolve_path(UART4_PATH)
    status = property_by_name(checked_uart4, "status")
    if status.strings != ["okay"]:
        raise RuntimeError("Output validation failed: UART4 is not enabled")
    checked_alias = property_by_name(checked.resolve_path("/aliases"), "serial3")
    if checked_alias.strings != [UART4_PATH]:
        raise RuntimeError("Output validation failed: serial3 alias is missing")
    print(
        f"Wrote {args.output} ({len(blob)} bytes), UART4 PB6=RX/PB7=TX AF4, "
        f"phandles={first:#x}..{first + 2:#x}"
    )


if __name__ == "__main__":
    main()
