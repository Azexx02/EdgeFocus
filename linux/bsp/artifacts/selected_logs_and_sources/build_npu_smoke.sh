#!/usr/bin/env bash
set -euo pipefail
source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
TOOLCHAIN=/opt/st/stm32mp2/5.0.3-snapshot/sysroots/x86_64-ostl_sdk-linux/usr/bin/aarch64-ostl-linux
SYSROOT=/opt/st/stm32mp2/5.0.3-snapshot/sysroots/cortexa35-ostl-linux
"${TOOLCHAIN}/aarch64-ostl-linux-g++" --sysroot="${SYSROOT}" -std=c++17 -I"${SYSROOT}/usr/include/stai_mpu" /home/hu/npu_smoke.cpp -o /home/hu/npu_smoke -lstai_mpu
file /home/hu/npu_smoke
