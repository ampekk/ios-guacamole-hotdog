#!/bin/sh
# Record what Android's camera driver does to power and start the main camera (IMX586): the sensor power sequence,
# every CCI (camera I2C) write and the CSIPHY setup, from the kernel's own debug log (camera_8150_op cam_debug_util
# debug_mdl: CAM_SENSOR 0x20 | CAM_CCI 0x4000 | CAM_CSIPHY 0x8000). Run on the Mac in the repo folder with the phone
# booted into ANDROID (adb + root) and the Camera app CLOSED:
#   sh tools/android-camera-trace.sh     then open Camera when it says so -> outputs/android-camera/trace.txt
# MASK=n picks other modules (default 49184 = 0xC020; the CSID/IFE setup: MASK=32776, CAM_ISP 0x8 | CAM_CSIPHY 0x8000),
# SECS=n the time recorded (default 25), OUT=name the file (default trace.txt).
# Only reads, apart from the debug mask, which it puts back to 0 at the end.
set -eu
cd "$(dirname "$0")/.."
adb get-state >/dev/null 2>&1 || { echo "no adb device: boot Android and connect USB" >&2; exit 1; }
out=outputs/android-camera
mkdir -p "$out"
p=/sys/module/cam_debug_util/parameters/debug_mdl
mask=${MASK:-49184}
secs=${SECS:-25}
file=${OUT:-trace.txt}
adb exec-out "su -c 'cat $p'" > "$out/debug_mdl.before" || { echo "no $p on this kernel" >&2; exit 1; }
adb exec-out "su -c 'dmesg -c > /dev/null; echo $mask > $p; cat $p'"
echo "recording for $secs seconds: open the Camera app NOW (main camera), leave it open"
adb exec-out "su -c 'timeout $secs cat /proc/kmsg'" > "$out/$file" || true
adb exec-out "su -c 'echo 0 > $p'"
echo "debug mask back to 0"
ls -l "$out/$file"
grep -c 'CAM' "$out/$file" || true
