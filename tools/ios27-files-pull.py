#!/usr/bin/env python3
"""ios27-files-pull.py OUTDIR [/path ...] : copy files off the iOS 27 root (HFS+, partition ios26) with the phone
booted into ANDROID (adb + su). No iOS, screen, Wi-Fi or SSH needed. Only reads. Run on the Mac in the repo folder.
Reads the volume header and the whole catalog, finds each path, then reads that file's data extents.
A path ending in "/" means a folder: its 2 newest files (by modification date) are copied.
With no paths it copies the Setup preferences and the newest unified-log files (DEFAULT below).
PART=name picks another partition (default ios26)."""
import os, struct, subprocess, sys, time

DEFAULT = ['/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist',
           '/private/var/mobile/Library/Preferences/com.apple.purplebuddy.notbackedup.plist',
           '/private/var/mobile/Library/Preferences/com.apple.springboard.plist',
           '/private/var/mobile/Library/Preferences/com.apple.backboardd.plist',
           '/private/var/mobile/Library/Preferences/.GlobalPreferences.plist',
           '/private/var/db/diagnostics/Persist/', '/private/var/db/diagnostics/Special/']
MAC_EPOCH = 2082844800

def sh(cmd, binary=False):
    r = subprocess.run(['adb', 'exec-out', cmd], capture_output=True)
    if r.returncode: sys.exit('adb failed: %s' % r.stderr.decode(errors='replace'))
    return r.stdout if binary else r.stdout.decode().strip()

out = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
paths = sys.argv[2:] or DEFAULT
part = os.environ.get('PART', 'ios26')
img = os.environ.get('IMG')  # test against a raw image file instead of the phone
dev = img or sh("su -c 'readlink -f /dev/block/by-name/%s'" % part)
if not img and not dev.startswith('/dev/'): sys.exit('no partition %s (is the phone in Android with root?): %r' % (part, dev))

def rd(blk, cnt, bs):
    if img:
        with open(img, 'rb') as f: f.seek(blk * bs); return f.read(cnt * bs)
    data = sh("su -c 'dd if=%s bs=%d skip=%d count=%d 2>/dev/null'" % (dev, bs, blk, cnt), True)
    if len(data) != cnt * bs: sys.exit('short read at block %d (+%d): %d bytes' % (blk, cnt, len(data)))
    return data

vh = rd(0, 1, 4096)[1024:1536]
if vh[:2] not in (b'H+', b'HX'): sys.exit('%s: no HFS+ volume header (got %r)' % (dev, vh[:2]))
bs = struct.unpack_from('>I', vh, 40)[0]
attrs = struct.unpack_from('>I', vh, 4)[0]
print('%s: HFS+ block size %d, journaled %s' % (dev, bs, bool(attrs & (1 << 13))))

def fork(rec):
    size, _, total = struct.unpack_from('>QII', rec, 0)
    ext = [struct.unpack_from('>II', rec, 16 + 8 * i) for i in range(8)]
    return size, total, [e for e in ext if e[1]]

csize, ctotal, cext = fork(vh[0x110:0x160])
if sum(c for _, c in cext) < ctotal: print('warning: catalog has more than 8 extents, the tail is missing')
t = time.time()
cat = b''.join(rd(s, c, bs) for s, c in cext)[:csize]
print('catalog %d MB read in %.0f s' % (len(cat) >> 20, time.time() - t))

ns = struct.unpack_from('>H', cat, 32)[0]
node = struct.unpack_from('>I', cat, 24)[0]
kids = {}  # parent CNID -> {name: (type, id, record bytes)}
while node:
    b = cat[node * ns:(node + 1) * ns]
    nxt, nr = struct.unpack_from('>I', b, 0)[0], struct.unpack_from('>H', b, 10)[0]
    for r in range(nr):
        ro = struct.unpack_from('>H', b, ns - 2 * (r + 1))[0]
        kl, par, nl = struct.unpack_from('>HIH', b, ro)
        name = b[ro + 8:ro + 8 + 2 * nl].decode('utf-16-be', 'replace')
        d = ro + 2 + kl
        typ = struct.unpack_from('>H', b, d)[0]
        if typ in (1, 2):
            kids.setdefault(par, {})[name] = (typ, struct.unpack_from('>I', b, d + 8)[0], b[d:d + 248])
    node = nxt

def lookup(path):
    cnid, ent = 2, None
    for comp in [c for c in path.split('/') if c]:
        ent = kids.get(cnid, {}).get(comp)
        if not ent: return None
        cnid = ent[1]
    return ent

def mtime(rec): return struct.unpack_from('>I', rec, 16)[0] - MAC_EPOCH

def save(path, rec):
    size, total, ext = fork(rec[88:168])
    flags = struct.unpack_from('>H', rec, 2)[0]
    if size == 0 and fork(rec[168:248])[0]: print('  %s: data is in the resource fork (compressed), skipped' % path); return
    if sum(c for _, c in ext) < total: print('  warning: %s has more than 8 extents, the tail is missing' % path)
    data = b''.join(rd(s, c, bs) for s, c in ext)[:size]
    dst = os.path.join(out, path.lstrip('/'))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(data)
    print('  %s  %d bytes%s  modified %s UTC' % (path, len(data), ' (empty or compressed)' if not data else '', time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(mtime(rec)))))

for p in paths:
    ent = lookup(p)
    if not ent: print('  %s: not found' % p); continue
    if p.endswith('/'):
        files = [(mtime(r), n, r) for n, (ty, _, r) in kids.get(ent[1], {}).items() if ty == 2]
        for _, n, r in sorted(files, reverse=True)[:2]: save(p + n, r)
    elif ent[0] == 2: save(p, ent[2])
    else: print('  %s: is a folder (end it with / to copy its newest files)' % p)
print('=> %s' % out)
