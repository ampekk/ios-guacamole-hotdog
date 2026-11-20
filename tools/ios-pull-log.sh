#!/bin/sh
# Pull the newest unified-log files from the phone over SSH and make them readable for
# research/ios26/tools/tracev3.py (LZBITMAP -> plain, tools/tracev3-unlzbm.py). Run on the Mac:
#   sh tools/ios-pull-log.sh NAME [HOST]      -> outputs/NAME-diag/{Persist,Special}/*.tracev3
# Takes the 2 newest Persist and Special files (minutes to an hour of log; small enough for the hotspot).
set -eu
name=${1:?usage: ios-pull-log.sh NAME [HOST]}
host=${2:-root@172.20.10.2}
tmp=$(mktemp -d /tmp/ioslog.XXXXXX)
ssh "$host" 'export PATH=/var/jb/usr/bin:/var/jb/bin:$PATH; cd /private/var/db/diagnostics &&
	tar -cf - $(ls -t Persist/*.tracev3 | head -2) $(ls -t Special/*.tracev3 | head -2)' > "$tmp/l.tar"
mkdir -p "$tmp/d" && tar -xf "$tmp/l.tar" -C "$tmp/d"
python3 "$(dirname "$0")/tracev3-unlzbm.py" "$tmp/d" "outputs/$name-diag"
rm -rf "$tmp"
