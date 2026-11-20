#!/bin/sh
# Pull the iOS 27 kernel log (the SWEETLOG ring in the last 4 MiB + 4 KiB of the ios26 partition) from the phone
# booted into ANDROID (adb + root), and decode it. Run on the Mac in the repo folder:
#   sh tools/ios27-klog-pull.sh            -> outputs/ios27-klog.bin (raw) and outputs/ios27-klog.txt (text)
# Only reads. PART=name picks another partition (default ios26).
set -eu
cd "$(dirname "$0")/.."
part=${PART:-ios26}
adb get-state >/dev/null 2>&1 || { echo "no adb device: boot Android and connect USB" >&2; exit 1; }
dev=$(adb shell "readlink -f /dev/block/by-name/$part" | tr -d '\r')
[ -n "$dev" ] || { echo "no partition $part" >&2; exit 1; }
bytes=$(adb shell "su -c 'blockdev --getsize64 $dev'" | tr -d '\r')
skip=$((bytes / 4096 - 1025))
adb exec-out "su -c 'dd if=$dev bs=4096 skip=$skip count=1025 2>/dev/null'" > outputs/ios27-klog.bin
got=$(wc -c < outputs/ios27-klog.bin | tr -d ' ')
echo "$dev: $bytes bytes, read blocks $skip.. ($got bytes)"
[ "$got" -eq $((1025 * 4096)) ] || { echo "short read: expected $((1025 * 4096)) bytes" >&2; exit 1; }
python3 tools/sweet-log.py outputs/ios27-klog.bin outputs/ios27-klog.txt
ls -l outputs/ios27-klog.txt
