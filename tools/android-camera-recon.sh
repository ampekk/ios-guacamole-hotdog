#!/bin/sh
# What the OnePlus 7 Pro's cameras are, as Android sees them, for the iOS camera bring-up (a real sensor behind
# /dev/hakios-camera). Run on the Mac in the repo folder with the phone booted into ANDROID (adb + root):
#   sh tools/android-camera-recon.sh      -> outputs/android-camera/ (git-ignored; vendor files stay local)
# Only reads: the flattened device tree, the kernel log's camera lines, the camera nodes and their drivers, the
# regulators/clocks/GPIOs as they stand, and the vendor camera sensor/module settings files (CamX).
set -eu
cd "$(dirname "$0")/.."
adb get-state >/dev/null 2>&1 || { echo "no adb device: boot Android and connect USB" >&2; exit 1; }
out=outputs/android-camera
mkdir -p "$out/vendor"
su() { adb exec-out "su -c '$1'"; }

su 'cat /sys/firmware/fdt' > "$out/fdt.dtb"
su 'dmesg' > "$out/dmesg.txt"
grep -i -E 'cam|cci|csiphy|csid|ife|vfe|imx|sensor|ois|actuator|eeprom|titan|icp|jpeg|motor|popup' "$out/dmesg.txt" \
    > "$out/dmesg-camera.txt" || true
su 'for d in /sys/class/video4linux/*; do echo "$d $(cat $d/name 2>/dev/null)"; done' > "$out/v4l.txt"
su 'ls -l /dev/v4l-subdev* /dev/video* /dev/media* 2>/dev/null' > "$out/dev-nodes.txt" || true
su 'grep -i -E "cam|cci|csiphy|csid|ife|vfe|icp|jpeg|cpas|cdm" /proc/interrupts' > "$out/interrupts.txt" || true
su 'mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null; cat /sys/kernel/debug/regulator/regulator_summary' \
    > "$out/regulator_summary.txt" 2>/dev/null || true
su 'cat /sys/kernel/debug/clk/clk_summary' > "$out/clk_summary.txt" 2>/dev/null || true
su 'cat /sys/kernel/debug/gpio' > "$out/gpio.txt" 2>/dev/null || true
su 'cat /proc/iomem' > "$out/iomem.txt" || true
su 'getprop' | grep -i -E 'cam|sensor|product|build.fingerprint' > "$out/props.txt" || true
su 'ls -l /vendor/lib64/camera /vendor/lib/camera /vendor/etc/camera /vendor/firmware 2>/dev/null' > "$out/vendor-ls.txt" || true
for f in $(adb exec-out "su -c 'ls /vendor/lib64/camera/*.bin /vendor/lib/camera/*.bin /vendor/etc/camera/* 2>/dev/null'" | tr -d '\r'); do
    adb exec-out "su -c 'cat $f'" > "$out/vendor/$(echo "$f" | tr '/' '_')"
done
if command -v dtc >/dev/null 2>&1; then
    dtc -I dtb -O dts -o "$out/fdt.dts" "$out/fdt.dtb" 2>/dev/null || true
fi
ls -l "$out" | head -40
echo "vendor files: $(ls "$out/vendor" | wc -l)"
