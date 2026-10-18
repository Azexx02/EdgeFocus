# RTL8723DU Board Install Notes

These steps are for later, after the board is available again.

## Artifact

- Driver module: `D:\Aze\Documents\developMP257\8723du.ko`
- Target kernel: `6.6.48`
- Module vermagic: `6.6.48 SMP preempt mod_unload modversions aarch64`

## Manual Bring-Up

Copy the module to the board, then run on the board:

```sh
uname -r
modprobe cfg80211 || true
modprobe mac80211 || true
insmod /root/8723du.ko
dmesg | tail -80
ip link
iw dev
```

If a WLAN interface appears, bring it up:

```sh
ip link set wlan0 up
iw dev
```

The interface name may differ from `wlan0`; use the name shown by `ip link` or `iw dev`.

## Persistence Option

After runtime verification succeeds, install the module under the matching kernel modules tree:

```sh
mkdir -p /lib/modules/$(uname -r)/extra
cp /root/8723du.ko /lib/modules/$(uname -r)/extra/
depmod -a
modprobe 8723du
```

Add `8723du` to the board's module auto-load configuration only after `modprobe 8723du` works cleanly.
