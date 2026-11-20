#!/usr/bin/env python3
"""Remove launchd jobs (by Label) from a copy of the phone's /System/Library/xpc/launchd.plist.

  launchd-drop-job.py IN OUT LABEL [LABEL ...]

IN is the plist pulled from the phone; OUT is written as a binary plist for pushing back (scp onto the existing
file keeps its owner and mode). Labels not found are reported; the script fails if none were found.
2026-11-17: com.apple.PerfPowerServices (powerlog) spins a core forever on this phone: in a dispatch_once in
PowerlogLiteOperators it enumerates SMC keys with SMCGetKeyFromIndex(i) and stops only on result -4 (index out of
range); without an AppleSMC every call fails some other way, so i counts up for ever.
"""
import plistlib, sys

src, dst, labels = sys.argv[1], sys.argv[2], set(sys.argv[3:])
with open(src, 'rb') as f:
    plist = plistlib.load(f)
gone = []
for domain in ('LaunchDaemons', 'LaunchAgents'):
    jobs = plist.get(domain, {})
    for k in [k for k, j in jobs.items() if isinstance(j, dict) and j.get('Label') in labels]:
        gone.append(jobs[k]['Label'] + ' (' + k + ')')
        del jobs[k]
for l in labels - {g.split(' ')[0] for g in gone}:
    print('not found:', l)
if not gone:
    sys.exit('nothing removed')
with open(dst, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
print('removed:', ', '.join(gone))
