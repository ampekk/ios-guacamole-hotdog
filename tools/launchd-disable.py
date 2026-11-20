#!/usr/bin/env python3
"""Leave one launchd job out of (or put it back into) a copy of the phone's /System/Library/xpc/launchd.plist.

  launchd-disable.py IN OUT LABEL               drop the job whose Label is LABEL (its entry goes to OUT.LABEL.json)
  launchd-disable.py IN OUT --restore SAVED     put a job dropped earlier back from its SAVED .json file

IN is the plist pulled from the phone; OUT is written as a binary plist for pushing back. A dropped job is not started
at all (no on-demand launch either): whatever looks up its services gets no answer. 2026-11-19 (Claude): for
cameracaptured, which the synthetic camera keeps restarting (400 MB of GPU pool, then a jetsam kill, every few seconds).
"""
import base64
import datetime
import json
import plistlib
import sys


def enc(o):
    if isinstance(o, bytes):
        return {'__bytes__': base64.b64encode(o).decode()}
    if isinstance(o, datetime.datetime):
        return {'__date__': o.isoformat()}
    raise TypeError(type(o))


def dec(d):
    if '__bytes__' in d:
        return base64.b64decode(d['__bytes__'])
    if '__date__' in d:
        return datetime.datetime.fromisoformat(d['__date__'])
    return d


src, dst = sys.argv[1], sys.argv[2]
with open(src, 'rb') as f:
    plist = plistlib.load(f)
daemons = plist['LaunchDaemons']
if sys.argv[3] == '--restore':
    with open(sys.argv[4]) as f:
        saved = json.load(f, object_hook=dec)
    daemons[saved['key']] = saved['job']
    print(f"restored: {saved['job'].get('Label')} ({saved['key']})")
else:
    label = sys.argv[3]
    keys = [k for k, j in daemons.items() if j.get('Label') == label]
    if len(keys) != 1:
        sys.exit(f'{label}: {len(keys)} jobs with that label')
    saved = f'{dst}.{label}.json'
    with open(saved, 'w') as f:
        json.dump({'key': keys[0], 'job': daemons[keys[0]]}, f, default=enc, indent=1)
    del daemons[keys[0]]
    print(f'dropped: {label} ({keys[0]}); saved to {saved}')
with open(dst, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
