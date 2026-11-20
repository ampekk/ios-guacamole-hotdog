#!/usr/bin/env python3
"""Alias iOS 27.2's BCM4357B1/USI legacy profile to the D73 ProductHash.

This changes one data string in BlueTool's firmware table, not MobileGestalt's
global ProductHash answer. It keeps the iPhone 14 Pro identity everywhere
else, while letting the existing WCN3998 UART bridge select the BCM4357B1/USI
startup script it emulates.

Usage: tools/patch-bluetool27-profile.py INPUT OUTPUT
The caller must ad-hoc sign OUTPUT after patching.
"""
from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

LEGACY_PROFILE_HASH = b"75ca7dd93c5ac33c4811050d8987504774003971"
D73_PRODUCT_HASH = b"d3b5ffca33e4604460655adfd77fdfa4757e7e93"
MACHO64_MAGIC = b"\xcf\xfa\xed\xfe"


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {Path(sys.argv[0]).name} INPUT OUTPUT", file=sys.stderr)
        return 2
    src, dst = map(Path, sys.argv[1:])
    if not src.is_file() or src.stat().st_size == 0:
        print(f"missing/empty input: {src}", file=sys.stderr)
        return 1
    if src.resolve() == dst.resolve():
        print("refusing to patch in place; keep the original for rollback", file=sys.stderr)
        return 1
    data = src.read_bytes()
    if not data.startswith(MACHO64_MAGIC):
        print("input is not a 64-bit little-endian Mach-O", file=sys.stderr)
        return 1
    old_count = data.count(LEGACY_PROFILE_HASH)
    d73_count = data.count(D73_PRODUCT_HASH)
    if old_count != 1 or d73_count != 1:
        print(f"unexpected profile strings: legacy={old_count}, D73={d73_count}; NOT writing", file=sys.stderr)
        return 1
    patched = data.replace(LEGACY_PROFILE_HASH, D73_PRODUCT_HASH, 1)
    if patched.count(LEGACY_PROFILE_HASH) != 0 or patched.count(D73_PRODUCT_HASH) != 2:
        print("post-patch verification failed; NOT writing", file=sys.stderr)
        return 1
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + ".new")
    if tmp.exists():
        print(f"refusing to overwrite staging file: {tmp}", file=sys.stderr)
        return 1
    tmp.write_bytes(patched)
    # ipsw extraction may leave the Mach-O at 0644 even though it is an
    # executable. Preserve its other permission bits and ensure it can run.
    os.chmod(tmp, (src.stat().st_mode & 0o777) | 0o111)
    if tmp.stat().st_size != src.stat().st_size or tmp.read_bytes() != patched:
        tmp.unlink(missing_ok=True)
        print("output verification failed; NOT publishing", file=sys.stderr)
        return 1
    tmp.replace(dst)
    print(f"patched: {src} -> {dst}")
    print(f"D73 ProductHash aliases the existing BCM4357B1 profile: {D73_PRODUCT_HASH.decode()}")
    print(f"input SHA-256:  {hashlib.sha256(data).hexdigest()}")
    print(f"output SHA-256: {hashlib.sha256(patched).hexdigest()}")
    print("next: codesign -f -s - OUTPUT, then codesign -v OUTPUT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
