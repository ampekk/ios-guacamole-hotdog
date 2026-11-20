#!/usr/bin/env python3
"""Add one of our boot jobs to a copy of the phone's /System/Library/xpc/launchd.plist.

  launchd-add-job.py IN OUT NAME [NAME ...]      NAME: adspd

IN is the plist pulled from the phone; OUT is written as a binary plist for pushing back (scp onto the existing file
keeps its owner and mode). The job definitions are the ones tools/mkios27root-mac.sh adds when building a root.
2026-11-17: the iOS 27 root was built without extras/fw/tfa98xx.cnt, so mkios27root-mac.sh left com.sweet.adspd out
and the speaker stack (adspd serve: ADSP boot, AFE, TFA amps, PCM ring) never starts at boot.
"""
import plistlib, sys

JOBS = {
    'adspd': ('/System/Library/LaunchDaemons/com.sweet.adspd.plist', {
        'Label': 'com.sweet.adspd', 'ProgramArguments': ['/var/local/bin/adspd', 'serve'], 'RunAtLoad': True,
        'KeepAlive': True, 'EnvironmentVariables': {'ADSPD_BOOT': '30'}, 'POSIXSpawnType': 'Interactive',
        'StandardOutPath': '/var/local/adspd.out', 'StandardErrorPath': '/var/local/adspd.out'}),
}

src, dst, names = sys.argv[1], sys.argv[2], sys.argv[3:]
with open(src, 'rb') as f:
    plist = plistlib.load(f)
for n in names:
    if n not in JOBS:
        sys.exit(f'unknown job {n} (known: {", ".join(JOBS)})')
    key, job = JOBS[n]
    print(('replaced: ' if key in plist['LaunchDaemons'] else 'added: ') + job['Label'])
    plist['LaunchDaemons'][key] = job
with open(dst, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
