# hotdog: adapting this port to the OnePlus 7T Pro

This branch's iOS 27.2 port targets the OnePlus 7 Pro (`guacamole`, SM8150).
This document records the code-level adaptation for the **OnePlus 7T Pro**
(`hotdog`, SM8150-AC, models HD1911 / HD1913) and, importantly, what still has
to be checked on real hardware.

**Status: code-level support only. Nothing in this document has been booted or
validated on a hotdog handset.** Every value inherited from guacamole is
flagged as such below. Do not flash a boot image to a phone until the
validation checklist at the end passes.

## Why this is mostly a reuse

Hotdog is the T-series sibling of guacamole, not a new platform. Independent
mainline-Linux bring-up work on an actual HD1913
([Sr-0w/hotdog-linux-bringup](https://github.com/Sr-0w/hotdog-linux-bringup),
status 2026-08-25) reports the same parts this port drives:

| Subsystem | guacamole (7 Pro) | hotdog (7T Pro) | Consequence for this port |
| --- | --- | --- | --- |
| SoC | SM8150 (SDR855) | SM8150-AC (SDR855+) | Same GIC/UFS/SPMI/GPU MMIO; higher CPU/GPU OPPs come from cmd-db at runtime |
| Panel | 1440x3120 DSI/DSC, 90 Hz | 1440x3120 DSI/DSC, 90 Hz | Same framebuffer size/stride; keep `FB_W/H = 1440/3120` |
| Touch | Samsung S6SY761 | Samsung S6SY761 | `userland/jb` touch facts carry over |
| Wi-Fi | WCN3990 | WCN3990 | `userland/wland` carries over; **board-id file differs** (below) |
| BT | WCN3998 H4 | WCN3998 (see caveat below) | UART service topology unchanged; Linux bring-up reports BT controller lifecycle problems, so may stay partial |
| GPU | Adreno 640, `a640_*` firmware | Adreno 640, same firmware names | `/sweet-gpu` ADT node reusable; faster OPPs are read from cmd-db |
| Haptics | AW8697 | AW8697 | Reuse the waveform blob, copied from *this* phone |
| Speaker amps | 2x NXP TFA9874 | 2x NXP TFA9874 | `userland/adsp` carries over |
| Rear main camera | Sony IMX586 + LC898124EP3 lens/OIS | Sony IMX586 + lens/OIS (`xnu12377-camhw.diff` path) | Reuse; re-check the lens firmware and CCI addresses |
| Front camera | 16 MP pop-up | Sony IMX471 pop-up | Reuse the motor/timer path; Hall sensors still unused |
| Telephoto / ultra-wide | 8 MP + 16 MP | S5K3M5 + IMX481 | Not driven by this port today |
| PMIC | PM8150 / PM8150B / PM8150L | PM8150 family | Same SPMI arbiter v5, keys,RTC,PON reboot method |
| Alert slider | 3-position | 3-position (GPIO27 lower contact can be intermittent) | Shim menu default selection unchanged |
| Battery | 4000 mAh | 4085 mAh | Fuel gauge path unchanged; charger current patch may need a new value |
| Storage | UFS 3.0, individual partitions | UFS 3.x, dynamic `super` partitions | iOS root still goes in standalone `userdata`; see below |
| RAM | 8 GB | 8 GB / 12 GB variants | Fixed `--ram5g` window stays inside the first 8 GB |

Sources for the "hotdog" column: the hotdog-linux-bringup status and README
linked above (hardware-validated on HD1913), the postmarketOS wiki for the
`super`-partition difference, and OnePlus/Qualcomm board data. The iOS-side
facts are from this repo's `docs/guacamole.md` and `NOTES.md`.

### What actually changes in this repo

1. **`shim/board.h` / `shim/build.sh` / `shim/shim.c`**: `BOARD=hotdog` is a
   sibling of `BOARD=guacamole`. Same 1440x3120 framebuffer, IMEM/PON reboot
   constants, and triple-boot menu; only `BOARD_NAME` differs.
2. **`tools/mkadt.py`**: `--hotdog` selects the same SM8150 phone tree as
   `--guacamole` (framebuffer size, wlan/uart nodes, `/sweet-gpu`,
   `/sweet-haptic`, product audio/haptics, sRGB gamut, `--highmem`
   requirement). The `wlan` compatible string is board-specific
   (`hotdog,wcn3990`) and the root compatible gains `qcom,sm8150`.
3. **`tools/guac-mkboot.sh`**: takes `BOARD` (default `guacamole`) and derives
   the mkadt flag and intermediate names. `tools/hotdog-mkboot.sh` sets
   `BOARD=hotdog`.
4. **`tools/mkguacboot.py`**: unchanged logic; it copies the DTB and header
   from whichever stock `boot.img` you pass. `tools/mkhotdogboot.py` is an
   alias for discoverability.
5. **`tools/mktripleboot.py`**: unchanged; feed it hotdog-built iOS 16 / iOS 27
   images and a **hotdog** stock boot image.

The XNU diffs, userland drivers and Metal plugin are shared as-is. They key off
SoC-level addresses and the D73 userland identity, not the phone model.

## Build workflow (hotdog)

The stock boot image must come from the **hotdog** handset — its DTB section
and header fields are copied verbatim into the new image.

```sh
# 1. shim with the triple-boot selector
PATH=/path/to/aarch64-cross:$PATH BOARD=hotdog sh shim/build.sh
# -> shim/out/Image

# 2. iOS 27 payload (kernel from CI, or a local kernel path)
TOUCH=1 CPUS=8 KEYS=1 USB=0 \
  tools/hotdog-mkboot.sh RUN_ID dtb/hotdog-stock_boot.img shim/out/Image out/hotdog-ios27.img

# 3. combine with an existing iOS 16 payload + the hotdog stock boot image
python3 tools/mktripleboot.py dtb/hotdog-stock_boot.img shim/out/Image \
  out/hotdog-ios16.img out/hotdog-ios27.img out/hotdog-triple.img

# 4. temporary boot test only (does not modify the phone)
fastboot boot out/hotdog-triple.img
```

Add the usual full-boot arguments from `docs/dualboot.md`
(`rd=ufsroot0`, `ufsroot=ios26`, `sweetfb_scale=2`, etc.) to step 2's last
argument. `fastboot flash boot_b` is only for a build you intend to keep;
preserve the stock boot image and partition backups first.

### Firmware inputs specific to the phone

`mkadt.py` embeds blobs that are **not in git** and must be copied from the
hotdog handset (same paths as guacamole in `docs/guacamole.md`):

- `research/gpu/fw/{a640_gmu.bin,a630_sqe.fw,a640_zap.mdt,a640_zap.b00-.b02}`
- `dtb/haptics/aw8697_haptic_168.bin` (confirm the resonance filename on hotdog)
- the lens-controller program for `tools/lc898124-fw.py`
  -> `/var/local/fw/lc898124-so2820.fw`

The Wi-Fi board file is chosen at runtime by `userland/wland` from
`vendor.boot.prj_version`, `hw_version`, `rf_version` and
`persist.vendor.oem.wifi.chain`. On the tested guacamole those were
`1 / 21 / 4 / chain 0` -> `bdwlan.b0e`. **Hotdog has different values**; read
them from its live DT/properties and regenerate the matching `bdwlan.b<id>`
before expecting Wi-Fi to work.

## Validation checklist (on the device, in order)

Each item is a question to answer with a temporary `fastboot boot` image, not
an assumption. Record the result in `NOTES.md`, distinguishing observations
from guesses.

1. **Boot path**: does ABL accept the v2 image built from the hotdog stock
   boot.img, and does the shim console show `BOARD hotdog`?
2. **Splash / framebuffer**: is the ABL splash still 1440x3120 a8r8g8b8 at
   `0x9c000000`, stride 5760? Compare `shim`'s UFS/display probe output with
   `docs/guacamole.md`. If the address differs, update `shim/board.h` and
   `mkadt.py`'s `FB` constant from the hotdog live FDT.
3. **Reboot to fastboot**: does `rebootbl` / PSCI land in fastboot, or in
   CrashDump? If it differs, re-probe the PON_SOFT_RB_SPARE offset (SPMI
   arbiter v5 mapping is unchanged on SM8150).
4. **Memory map**: confirm the DDR hole and the `--ram5g` window against the
   hotdog live FDT. 12 GB units need a check that the fixed 0x100000000-
   0x240000000 window is still RAM with no firmware carve-outs.
5. **Touch / keys / audio**: enable `sweettouch=1`, `sweetkeys=1` and confirm
   the S6SY761, PM8150 keys and TFA9874 amps answer. Disable USB until its
   addresses are checked (`sweetusb=0`).
6. **Wi-Fi**: derive the board ID as described above; verify WCN3990 firmware
   download and association.
7. **GPU**: check chip id / speed bin (855+ may use a different bin) and that
   the copied A640 firmware authenticates; adjust OPPs only if cmd-db values
   are not picked up.
8. **Camera / haptics**: confirm the IMX586 and lens controller probe, the
   pop-up motor moves, and the AW8697 answers with the copied waveform.
9. **Partition layout**: confirm `userdata` is a standalone partition (the
   dynamic `super` partition on T-series devices groups system/vendor/product,
   not userdata). The iOS root/EFI labels in `tools/mkios27root-mac.sh` are
   APFS/HFS labels inside `userdata`, not Android partitions.
10. **Full boot**: lock screen, SpringBoard, and the known guacamole caveats
    in `docs/ios27.md` (Metal stutter, locationd spin, no full 8/12 GB, etc.).

Until each applies, the honest status is: **the hotdog code path builds and
generates a device tree, but hardware support is unverified.**
