# Dual boot: Android and iOS on one OnePlus SM8150 phone (guacamole / hotdog)

Guacamole is the tested device; the `BOARD=hotdog` selector builds the same
menu but is not boot-tested. See [`docs/hotdog.md`](hotdog.md) first.

A boot menu picks the operating system at power-on (`shim/menu.c`): iOS 27, iOS 16, Android. Volume up
and down move the selection, power starts it. The Alert Slider picks the default, which starts after 5 s
unless a key is pressed:

| Alert Slider | Default |
| --- | --- |
| top | iOS 27 (partition `ios26`; when the boot image carries no iOS 27 payload: iOS 16) |
| middle | iOS 16 |
| bottom | Android (boot_a: the stock ROM boot image, Magisk included) |

The slider positions follow the menu's order, top to bottom (changed 2026-11-17; before, top was Android).

Keys come from the PM8150 over the SPMI arbiter, read only: power = PON INT_RT_STS (0x0810) bit 0,
volume down = bit 1 (RESIN) or GPIO7 (0xc610), volume up = GPIO6 (0xc510), as the kernel's keys driver
reads them. Keys still held from power-on are ignored until released.

Status (2026-11-12): both paths work from a tethered `fastboot boot` of the selector image; Android
boots with Magisk. Flashing it to `boot_b` with slot B active is the permanent setup below.

## How it works

The phone's stock bootloader (ABL) always starts slot B's boot image. Its "kernel" is our shim
(`shim/`), which ABL treats as an arm64 Linux Image. The shim reads the slider before anything else:

- **iOS**: unchanged. The boot image's ramdisk is the SWEETXNU payload (XNU + ADT); `loader.c` places
  it and jumps to XNU, whose root is the GPT partition `ios` (`ufsroot=ios`).
- **Android** (`shim/android.c`):
  1. reads `boot_a` straight from the UFS (`shim/ufs.c`, read-only: the shim has no SCSI WRITE) into
     RAM at 0x100000000;
  2. takes the kernel out of Qualcomm's `UNCOMPRESSED_IMG` wrapper and moves it to an 8-byte aligned
     address (the MMU is off, so all memory is Device memory and unaligned 8-byte loads fault);
  3. copies the ramdisk to where ABL placed ours (above the device tree, below the first reserved
     region at 0x85700000);
  4. rebuilds the device tree ABL handed over (`fdt_rebuild_chosen` in `shim/fdt.c`) with a new
     `/chosen`: `bootargs` = ABL's line with our header command line (`console=null`) replaced by
     boot_a's, `androidboot.slot_suffix=_a`, `root=PARTUUID=` of `system_a`, and the new
     `linux,initrd-start/end`;
  5. copies a small trampoline (`linux_tramp`, `shim/start.S`) behind the device tree; it copies the
     kernel to 0x80000000 + text_offset (where ABL puts Android's, over the running shim) and enters
     it as the arm64 boot protocol requires (x0 = device tree, x1-x3 = 0, MMU and caches off).

Android therefore runs exactly the kernel, ramdisk and command line it would get from ABL directly, on
its own slot (`system_a`, `odm_a`, `dtbo_a`). Magisk keeps patching `boot_a` as usual.

Alert Slider: TLMM gpio125 (top), gpio134 (middle), gpio27 (bottom), active low, one 0x1000 page per
pin (GPIO_CFG +0, GPIO_IN_OUT +4). The bootloader leaves pull-downs on them (all read 0), so the shim
writes GPIO_CFG = 0 (input, no pull) and reads after 2 ms: top reads low=1, middle low=2, bottom low=4.
The same pins drive iOS's ring/silent switch (xnu 0264).

If Android cannot be started, the reason stays on screen for 15 s. Errors before the ramdisk copy then
boot iOS; after it the iOS payload in memory is gone and the shim resets.

## Storage layout (LUN0, 4 KiB sectors)

| Partition | Sectors | Size | Contents |
| --- | --- | --- | --- |
| `userdata` (sda19) | 1945992-18723207 | 64 GiB | Android data |
| `ios26` (sda21) | 18723208-30781831 | 46 GiB | blank, for iOS 26 tests |
| `ios` (sda20) | 30781832-61409274 | 116.83 GiB | iOS 16 HFS+ root (`ufsroot=ios`); kernel log ring in its last 4 MiB + 4 KiB |

The table was rewritten from TWRP with `dd` (TWRP 3.7.1 has no sgdisk): `userdata` kept its start,
type and unique GUIDs, `ios26` was added in the freed space, `ios` was not touched. Both GPT copies
(LBA 0-5 and LBA 61409275-61409279) were written and read back by sha256. Android's data partition was
formatted afterwards (TWRP Format Data).

## Setting it up

Requirements: unlocked bootloader, Android on slot A (`boot_a`), the iOS root on a partition named
`ios`, TWRP for log pulls and partition work.

1. Build the shim and an iOS boot image as usual (`docs/guacamole.md`, Workflow): `BOARD=guacamole
   shim/build.sh` (`BOARD=hotdog` on a 7T Pro) builds the selector into `shim/out/Image`;
   `tools/guac-mkboot.sh` / `tools/hotdog-mkboot.sh` and `tools/mkguacboot.py` wrap it with the
   payload. Use that phone's own stock `boot.img`.
2. Test tethered, nothing flashed: `fastboot boot IMAGE` with the slider at the bottom (Android) and at
   the top or middle (iOS).
3. Make `dtbo_b` equal `dtbo_a` (ABL applies the active slot's dtbo; Android needs its own):
   ```
   adb shell su -c "dd if=/dev/block/by-name/dtbo_a of=/dev/block/by-name/dtbo_b bs=4096 conv=fsync"
   adb shell su -c "sha256sum /dev/block/by-name/dtbo_a /dev/block/by-name/dtbo_b"
   ```
4. Back up boot_b, flash the selector image, make slot B active:
   ```
   adb exec-out "su -c 'dd if=/dev/block/by-name/boot_b bs=4096 2>/dev/null'" > boot_b-backup.img
   fastboot flash boot_b IMAGE
   fastboot set_active b
   fastboot reboot
   ```
5. Back to plain Android at any time: `fastboot set_active a`.

## Triple boot image (iOS 27 + iOS 16 + Android)

One boot image carries both iOS payloads in its ramdisk, iOS 16 first, each on a 4 KiB boundary
(`shim/loader.c` `payload_at`, `loader_count`, `loader_boot_n`); the menu enables "iOS 27" when a
second payload is there. Build both iOS boot images as usual (their own kernel, ADT and boot-args,
`ufsroot=ios` / `ufsroot=ios26`), rebuild the shim (`BOARD=guacamole` or `BOARD=hotdog`), then:

```
python3 tools/mktripleboot.py dtb/android-boot_b.img shim/out/Image IOS16.img IOS27.img OUT.img
```

It refuses more than ~50 MiB of payloads (ABL's ramdisk room above the DTB, below 0x85700000). Test with
`fastboot boot OUT.img`, then flash it to `boot_b` as below. An iOS restart (PSCI SYSTEM_RESET) comes
back to this menu.

## Rebuilding the boot image (kernel or payload changes)

Same as before, with the selector shim:

```
tools/xnu-local-build.sh                                   # kernel with any new xnu/patches
BOARD=guacamole shim/build.sh                               # BOARD=hotdog on a 7T Pro; shim/out/Image = selector shim
CPUS=8 TOUCH=1 KEYS=1 USB=1 tools/guac-mkboot.sh KERNEL dtb/android-boot_b.img shim/out/Image OUT.img "BOOT-ARGS"
```

`dtb/android-boot_b.img` only supplies the header fields and the device-tree section (identical in both
slots' images). The current boot-args are in the payload header of the image on boot_b; to print them:

```
python3 -c "import struct,sys;b=open(sys.argv[1],'rb').read();k,_,r=struct.unpack_from('<III',b,8);o=4096*(1+(k+4095)//4096);print(b[o+0x48:o+0x48+608].split(b'\0')[0].decode())" OLD.img
```

## Rules of thumb

- Every future iOS boot image must be built with this shim, or the selector disappears.
- Magisk: use "Direct install" (patches boot_a). Never "Install to inactive slot": that would write
  boot_b, the selector.
- A ROM update that switches slots (OTA to slot B) would overwrite boot_b and system_b; reinstall the
  ROM on slot A, or reflash the selector image to boot_b afterwards.
- Never "Format Data" or "Wipe" in TWRP unless Android's data is meant to go; the iOS root is on `ios`
  and is not touched by it.

## Known differences from a direct ABL boot of Android

ABL does not run Android Verified Boot for boot_a when the shim boots it, so Android gets
`root=PARTUUID=<system_a>` instead of the dm-verity device and no `androidboot.vbmeta.*` values. On
this unlocked phone (orange state) Android accepts that; if something depends on it (SafetyNet-style
checks, keystore), the shim could pass ABL's verity line for slot A instead.

## Files

- `shim/menu.c`, `shim/menu.h`: boot menu (keys over SPMI, drawing).
- `shim/bootsel.c`, `shim/bootsel.h`: slider read, diagnostics screen.
- `shim/android.c`, `shim/android.h`: boot_a loader.
- `shim/ufs.c`: `ufs_open`, `ufs_find`, `ufs_find_guid`, `ufs_read` (read-only, any LUN).
- `shim/fdt.c`: `fdt_chosen_prop`, `fdt_rebuild_chosen`.
- `shim/start.S`: `linux_tramp`.
- `shim/build.sh`: builds them in, adds `-fno-jump-tables` (the shim runs unrelocated, so no tables of
  absolute addresses).
