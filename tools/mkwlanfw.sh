#!/bin/bash
# Pull the Wi-Fi firmware wland needs from the phone's own Android partitions (userland/wland/README.md) into
# research/ios27.2/extras/fw, where tools/mkios27root-mac.sh picks it up. Device data (EFS: IMEI, calibration):
# research/ is git-ignored; never commit these files.
#   tools/mkwlanfw.sh            phone in Android (adb, root), needs mtools (brew install mtools)
set -euo pipefail
cd "$(dirname "$0")/.."
X=research/ios27.2/extras
D=$X/dumps
mkdir -p "$D" "$X/fw/modem" "$X/fw/rmtfs"
command -v mcopy >/dev/null || { echo "need mtools (brew install mtools)" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no adb device: boot Android first" >&2; exit 1; }
rd() {	# partition name -> file, via su when plain adb cannot read block devices
	local dev
	dev=$(adb shell "readlink -f /dev/block/by-name/$1 || readlink -f /dev/block/bootdevice/by-name/$1" 2>/dev/null | tr -d '\r' | head -1)
	[ -n "$dev" ] || { echo "no partition $1" >&2; exit 1; }
	adb exec-out "su -c 'cat $dev'" > "$2"
	[ -s "$2" ] || adb exec-out "cat $dev" > "$2"
	echo "$1 ($dev): $(wc -c < "$2" | tr -d ' ') bytes"
	[ -s "$2" ] || { echo "empty read of $1" >&2; exit 1; }
}
rd modem_b "$D/modem_b.img"
for p in modemst1 modemst2 fsg fsc; do rd "$p" "$D/$p.img"; cp "$D/$p.img" "$X/fw/rmtfs/$p.img"; done
# modem_b is FAT: copy image/ entries wland loads, lower-cased
n=0
for f in $(mdir -b -i "$D/modem_b.img" ::image 2>/dev/null || mdir -b -i "$D/modem_b.img" ::IMAGE); do
	b=$(basename "$f"); l=$(echo "$b" | tr 'A-Z' 'a-z')
	case "$l" in
	modem.mdt|modem.b[0-9][0-9]|modemr.jsn|modemuw.jsn|wlanmdsp.mbn|bdwlan.*)
		mcopy -n -i "$D/modem_b.img" "::${f#::}" "$X/fw/modem/$l"; n=$((n + 1)) ;;
	esac
done
echo "fw/modem: $n files ($(ls "$X/fw/modem" | tr '\n' ' '))"
ls "$X/fw/modem/modem.mdt" "$X/fw/modem/wlanmdsp.mbn" >/dev/null
