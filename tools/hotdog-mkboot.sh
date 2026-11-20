#!/bin/bash
# Convenience wrapper: build a OnePlus 7T Pro (hotdog) boot image.
# Usage and environment are identical to tools/guac-mkboot.sh:
#   tools/hotdog-mkboot.sh RUN_ID STOCK_boot.img SHIM.Image OUT.img ["extra boot-args"]
# BOARD=hotdog is fixed here, so the ADT is generated with --hotdog and the
# intermediates are named htd-hi.adt / payload-htd.bin.
exec env BOARD=hotdog "$(dirname "$0")/guac-mkboot.sh" "$@"
