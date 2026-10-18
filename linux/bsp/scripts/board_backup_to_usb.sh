#!/bin/sh
set -eu

BACKUP_ROOT="${1:-/run/media/sda1}"
STAMP="$(date +%Y%m%d_%H%M%S 2>/dev/null || echo unknown_time)"
OUT="$BACKUP_ROOT/mp257_backup_$STAMP"

if [ ! -d "$BACKUP_ROOT" ]; then
	echo "Backup target does not exist: $BACKUP_ROOT" >&2
	echo "Insert/mount a USB disk first, then run: sh $0 /path/to/mount" >&2
	exit 1
fi

mkdir -p "$OUT"

echo "Writing backup to $OUT"

{
	echo "===== uname ====="
	uname -a || true
	echo
	echo "===== cmdline ====="
	cat /proc/cmdline || true
	echo
	echo "===== mounts ====="
	mount || true
	echo
	echo "===== block devices ====="
	lsblk -a -o NAME,MAJ:MIN,SIZE,TYPE,FSTYPE,LABEL,UUID,MOUNTPOINTS 2>/dev/null || true
	echo
	echo "===== df ====="
	df -h || true
	echo
	echo "===== network links ====="
	ip -br link || true
	echo
	echo "===== usb ====="
	lsusb 2>/dev/null || true
	echo
	echo "===== usb tree ====="
	lsusb -t 2>/dev/null || true
	echo
	echo "===== relevant dmesg ====="
	dmesg | grep -Ei 'usb 1-1|8723|rtl|rtk|wlan|cfg80211|mac80211|firmware|error -71|not accepting|unable to enumerate' | tail -250 || true
	echo
	echo "===== enabled units ====="
	systemctl list-unit-files --state=enabled 2>/dev/null || true
	echo
	echo "===== failed units ====="
	systemctl --failed 2>/dev/null || true
} > "$OUT/system_snapshot.txt"

for p in \
	/boot \
	/etc \
	/lib/systemd/system \
	/usr/lib/systemd/system \
	/etc/systemd \
	/etc/udev \
	/etc/modprobe.d \
	/etc/modules-load.d \
	/etc/network \
	/etc/NetworkManager \
	/etc/wpa_supplicant* \
	/usr/lib/firmware \
	/lib/firmware \
	/root \
	/root/scripts \
	/home/root; do
	if [ -e "$p" ]; then
		name="$(echo "$p" | sed 's#^/##; s#[/ *]#_#g')"
		tar -C / -czf "$OUT/${name}.tar.gz" "${p#/}" 2>"$OUT/${name}.tar.err" || true
	fi
done

if [ -d /sys/firmware/devicetree/base ]; then
	tar -C /sys/firmware/devicetree -czf "$OUT/running_devicetree_sysfs.tar.gz" base 2>"$OUT/running_devicetree_sysfs.tar.err" || true
fi

find /boot -maxdepth 3 \( -name '*.dtb' -o -name '*.dtbo' -o -name '*.scr' -o -name '*Image*' -o -name 'uImage' \) \
	-print > "$OUT/boot_artifacts.txt" 2>/dev/null || true

sync
echo "Backup complete: $OUT"
