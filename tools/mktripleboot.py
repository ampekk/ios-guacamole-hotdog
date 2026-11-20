#!/usr/bin/env python3
"""mktripleboot.py - one boot image for boot_b with the boot menu and both iOS versions:

  python3 tools/mktripleboot.py STOCK_boot.img SHIM.Image IOS16_boot.img IOS27_boot.img OUT.img

IOS16_boot.img / IOS27_boot.img are ordinary boot images as tools/guac-mkboot.sh (BOARD=guacamole) or
tools/hotdog-mkboot.sh makes them (iOS 16: the one on boot_b now, ufsroot=ios; iOS 27: e.g.
outputs/hotdog-ios27a-*.img, ufsroot=ios26). Their ramdisks are the SWEETXNU payloads (kernel + ADT +
boot-args); they go into one ramdisk, iOS 16 first, each starting on a 4 KiB boundary
(shim/loader.c payload_at). SHIM.Image must be built from a shim with loader_boot_n
(BOARD=guacamole or BOARD=hotdog shim/build.sh): the menu then offers iOS 27 / iOS 16 / Android. An
older shim simply boots the first payload (iOS 16). Writes nothing to the phone.
"""
import os, struct, subprocess, sys, tempfile

HDR = "<8s10I16s512s32s1024sIQIIQ"


def ramdisk_of(path):
    b = open(path, "rb").read()
    if b[:8] != b"ANDROID!":
        sys.exit(f"{path}: not an Android boot image")
    f = struct.unpack_from(HDR, b, 0)
    ksz, rsz, page = f[1], f[3], f[8]
    off = page * (1 + (ksz + page - 1) // page)
    rd = b[off:off + rsz]
    if rd[:8] != b"SWEETXNU":
        sys.exit(f"{path}: its ramdisk is not a SWEETXNU payload")
    cmd = rd[0x48:0x48 + 608].split(b"\0")[0].decode(errors="replace")
    return rd, cmd


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    stock, shim, ios16, ios27, out = sys.argv[1:]
    p16, c16 = ramdisk_of(ios16)
    p27, c27 = ramdisk_of(ios27)
    for name, cmd, want in (("iOS 16", c16, "ufsroot=ios "), ("iOS 27", c27, "ufsroot=ios26 ")):
        if want not in cmd + " ":
            print(f"warning: {name} payload boot-args have no '{want.strip()}': {cmd[:120]}...", file=sys.stderr)
    blob = p16 + b"\0" * (-len(p16) % 4096) + p27
    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as t:
        t.write(blob)
        tmp = t.name
    try:
        here = os.path.dirname(os.path.abspath(__file__))
        subprocess.check_call([sys.executable, os.path.join(here, "mkguacboot.py"), stock, shim, out, "--ramdisk", tmp])
    finally:
        os.unlink(tmp)
    size = os.path.getsize(out)
    print(f"payloads: iOS 16 {len(p16)} B, iOS 27 {len(p27)} B at {len(p16) + (-len(p16) % 4096):#x}; image {size} B")
    if size > 100 * 1024 * 1024:
        print("warning: larger than 100 MiB: check the boot_b partition size before flashing", file=sys.stderr)
    print("iOS 16 args:", c16)
    print("iOS 27 args:", c27)


if __name__ == "__main__":
    main()
