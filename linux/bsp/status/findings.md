# STM32MP257 NPU/Galcore Findings

## Known State
- Self-developed board IP during app deployment: `192.168.10.171`.
- Serial console: `COM3`.
- Current app deployment path: `/opt/edgefocus`.
- Stable service mode: `EDGEFOCUS_VISION_MODE=mock`.

## NPU Failure
- Smoke test command:
  `/tmp/npu_smoke /opt/edgefocus/models/edgefocus_pose_v1/edgefocus_pose_v1_1.nb`
- Failure:
  `Loading dynamically: /usr/lib/libstai_mpu_ovx.so.6`
  `[OVX]: Loading nbg model`
  `Failed to open device: No such file or directory`
- Missing device:
  `/dev/galcore`
- Kernel messages:
  `Time out check load galcore module expired`
  `Time out check galcore device expired`

## To Confirm
- Whether the kernel module `galcore.ko` exists on this rootfs.
- Whether the self-developed DTB disables or omits the NPU/GPU/galcore node.
- Whether original ATK DTB has the needed node enabled.

## Current Board Evidence Collected
- Kernel: `Linux ATK-DLMP257 6.6.48 #1 SMP PREEMPT Tue May 6 18:09:52 CST 2025 aarch64`.
- Runtime DT model/compatible:
  - `ALIENTEK STM32MP257 Evaluation Board`
  - `st,stm32mp257d-atk`, `st,stm32mp257`
- `/lib/modules/$(uname -r)` search found no `galcore`, `viv`, `npu`, `gpu`, or `openvx` module files.
- `weston-checkgpu.service` runs `/sbin/check-gpu`; it waits for `galcore` module and `/dev/galcore`.
- `/proc/device-tree` search found no likely `gpu/npu/galcore/viv` nodes under max depth 4, except reserved memory in dmesg.
- Working hypothesis after Phase 1 evidence: this rootfs/kernel deployment lacks the galcore kernel driver and possibly lacks/enables no gcnano DT node. Need compare against original ATK DTB/kernel package before changing.

## Root Cause
- Current runtime DT had `/soc@0/bus@42080000/gpu@48280000/status = disabled`.
- Original ATK DTS has the same node with `status = "okay"`.
- The board did have galcore support available:
  - Kernel config: `CONFIG_GPU_GALCORE=m`.
  - Module file: `/boot/6.6.48/kernel/drivers/gpu/drm/gcnano-driver-stm32mp-6.4.19/galcore.ko`.
  - Module alias: `of:N*T*Cvivante,gc* -> galcore`.
- Because the DT node was disabled, udev/modprobe never bound `galcore`, so `/dev/galcore` was absent and STAI_MPU failed.

## Fix Applied
- Patched only `gpu@48280000` in the selfcarrier v9 DT source:
  `status = "disabled"` -> `status = "okay"`.
- Compiled DTB with ST SDK `dtc`.
- Installed to `/boot/stm32mp257d-atk-ddr-2GB.dtb`.
- Preserved previous DTB as:
  `/boot/stm32mp257d-atk-ddr-2GB.dtb.before-gpu-okay-20260705.bak`.

## Verification
- Runtime DT after reboot:
  `/proc/device-tree/soc@0/bus@42080000/gpu@48280000/status = okay`.
- `galcore` module loaded:
  `galcore 385024 0 - Live ...`
- `/dev/galcore` exists:
  `crw-rw-rw- 1 root video 199, 0 /dev/galcore`.
- dmesg:
  `Galcore version 6.4.19.4.952875`.
- NPU smoke test:
  - Loaded `/usr/lib/libstai_mpu_ovx.so.6`.
  - Model loaded successfully.
  - Input shape `1x3x256x256`.
  - Output shape `1x56x1344`.
  - Exit code `0`.
- `focus_app.service` switched to `EDGEFOCUS_VISION_MODE=real`, remains `active`, HTTP `/demo/snapshot` returns `200`.
