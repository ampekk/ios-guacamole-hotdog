#!/usr/bin/env python3
"""Set (or remove) environment variables of one job in a copy of the phone's /System/Library/xpc/launchd.plist.

  launchd-env.py IN OUT LABEL NAME=VALUE ...     add/replace
  launchd-env.py IN OUT LABEL -NAME ...          remove

IN is the plist pulled from the phone; OUT is written as a binary plist for pushing back. LABEL is the job's Label
(e.g. com.apple.backboardd). 2026-11-19 (Claude): for allocator experiments on backboardd (perf).
"""
import plistlib
import sys

src, dst, label, changes = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
with open(src, 'rb') as f:
    plist = plistlib.load(f)
jobs = [j for j in plist['LaunchDaemons'].values() if j.get('Label') == label]
if len(jobs) != 1:
    sys.exit(f'{label}: {len(jobs)} jobs with that label')
env = jobs[0].setdefault('EnvironmentVariables', {})
for c in changes:
    if c.startswith('-'):
        print(('removed: ' if env.pop(c[1:], None) is not None else 'not set: ') + c[1:])
    else:
        k, v = c.split('=', 1)
        env[k] = v
        print(f'set: {k}={v}')
if not env:
    del jobs[0]['EnvironmentVariables']
print(f'{label} environment now: {jobs[0].get("EnvironmentVariables", {})}')
with open(dst, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
