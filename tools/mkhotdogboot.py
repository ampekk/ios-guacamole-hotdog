#!/usr/bin/env python3
"""mkhotdogboot.py - OnePlus 7T Pro (hotdog) name for mkguacboot.py.

The wrapper is board-agnostic: it copies the DTB section and header fields out
of any header-v2 OnePlus stock boot.img, so a hotdog stock image works as-is.
Kept as a separate entry point so the hotdog workflow does not depend on a
guacamole-named command.

  python3 mkhotdogboot.py STOCK_boot.img htd.Image OUT.img [--ramdisk FILE]
"""
from mkguacboot import main

if __name__ == '__main__':
    main()
