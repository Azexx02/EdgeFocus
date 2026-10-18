#!/usr/bin/env bash
set -euo pipefail

cd /home/hu
rm -rf rtl8723du_build
mkdir rtl8723du_build
tar -xzf rtl8723du_driver_src.tar.gz -C rtl8723du_build --strip-components=1
cd rtl8723du_build

source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux

make clean || true
make ARCH=arm64 \
  CROSS_COMPILE=aarch64-ostl-linux- \
  KSRC=/home/hu/linux-6.6.48-build \
  CONFIG_PLATFORM_I386_PC=n \
  CONFIG_BR_EXT=n \
  USER_EXTRA_CFLAGS="-DCONFIG_LITTLE_ENDIAN -DODM_ENDIAN_TYPE=ODM_ENDIAN_LITTLE -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT" \
  -j4

file 8723du.ko
modinfo 8723du.ko | head -40
