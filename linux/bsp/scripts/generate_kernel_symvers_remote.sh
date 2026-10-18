#!/usr/bin/env bash
set -euo pipefail

cd /home/hu/linux-6.6.48-build
source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux

make ARCH=arm64 CROSS_COMPILE=aarch64-ostl-linux- modules -j4
ls -l Module.symvers
