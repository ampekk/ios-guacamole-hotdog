#!/bin/sh
# Decode an iOS 27 phone's unified log on the Mac (the tracev3 chunks are LZBITMAP-compressed, which only
# Apple's libcompression reads; research/ios26/tools/tracev3.py cannot). Run on the Mac:
#   sh tools/ios-logshow.sh outputs/wp.tar [PREDICATE]
# wp.tar holds private/var/db/diagnostics and private/var/db/uuidtext from the phone (ssh ... tar -cf -).
# Writes outputs/<name>-log.txt (compact style, everything or what PREDICATE selects).
set -eu
tarf=${1:?usage: ios-logshow.sh PHONE.tar [PREDICATE]}
pred=${2:-}
name=$(basename "$tarf" .tar)
work=$(mktemp -d /tmp/ioslog.XXXXXX)
tar -xf "$tarf" -C "$work" private/var/db/diagnostics private/var/db/uuidtext
arc="$work/$name.logarchive"
mkdir -p "$arc"
cp -R "$work/private/var/db/diagnostics/." "$arc/"
cp -R "$work/private/var/db/uuidtext/." "$arc/"
plutil -create xml1 "$arc/Info.plist"
plutil -insert OSArchiveVersion -integer 4 "$arc/Info.plist"
out="outputs/$name-log.txt"
if [ -n "$pred" ]; then
	log show --archive "$arc" --style compact --info --debug --predicate "$pred" > "$out"
else
	log show --archive "$arc" --style compact --info --debug > "$out"
fi
ls -l "$out"
echo "archive kept at $arc (delete when done)"
