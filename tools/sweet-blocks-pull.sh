#!/bin/sh
# Read 4 KiB blocks from the iOS root partition of a phone in Android/recovery (adb), the same way
# tools/sweet-log-pull.sh finds the partition (by-name: ios26, ios, ios16, userdata; PART=name to force).
#   tools/sweet-blocks-pull.sh OUT SKIP COUNT [SKIP COUNT ...]   (pieces are appended in order)
# Prints the device used and the size of each piece; fails if a piece comes back short.
set -eu
out=${1:?usage: sweet-blocks-pull.sh OUT SKIP COUNT [SKIP COUNT ...]}
shift
[ $# -ge 2 ] || { echo "need SKIP COUNT pairs" >&2; exit 1; }
command -v adb >/dev/null 2>&1 || { echo "Error: adb not found in PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "Error: no adb device (adb devices is empty: boot Android/recovery first)" >&2; exit 1; }
parts=${PART:-ios26 ios ios16 userdata}
dev=$(adb shell "for p in $parts; do for d in /dev/block/by-name/\$p /dev/block/bootdevice/by-name/\$p; do [ -e \$d ] && readlink -f \$d && exit 0; done; done" 2>/dev/null | tr -d '\r')
[ -n "$dev" ] || { echo "no partition named $parts on the connected device" >&2; exit 1; }
# Plain adb shell may not read block devices; use su when it exists.
if adb shell "dd if=$dev bs=4096 count=1 2>/dev/null | wc -c" 2>/dev/null | tr -d '\r' | grep -qx 4096; then
	pre=""; post=""
else
	pre="su -c '"; post="'"
fi
: > "$out"
while [ $# -ge 2 ]; do
	skip=$1; count=$2; shift 2
	tmp="$out.part"
	adb exec-out "${pre}dd if=$dev bs=4096 skip=$skip count=$count 2>/dev/null${post}" > "$tmp"
	got=$(wc -c < "$tmp" | tr -d ' ')
	echo "$dev blocks $skip+$count: $got bytes" >&2
	[ "$got" -eq $((count * 4096)) ] || { echo "short read (expected $((count * 4096)))" >&2; rm -f "$tmp"; exit 1; }
	cat "$tmp" >> "$out"; rm -f "$tmp"
done
echo "=> $out ($(wc -c < "$out" | tr -d ' ') bytes)" >&2
