# iOS 27.2 on OnePlus 7 Pro (guacamole) / OnePlus 7T Pro (hotdog)

This branch ports the **iOS 27.2 (24B119) userland for iPhone 14 Pro (iPhone15,2 / D73AP)** to the OnePlus 7 Pro (`guacamole`, Qualcomm SM8150 / Snapdragon 855, Adreno 640, 8 GB RAM), with a code-level adaptation for the OnePlus 7T Pro (`hotdog`, SM8150-AC / Snapdragon 855+). The device tree presents the D73 identity while the custom XNU kernel and userland drivers adapt the phone's hardware.

This is an experimental bring-up, not a daily-use phone OS. Many hardware features are incomplete, performance and graphics are unreliable, and the port disables or replaces parts of Apple's normal security and device stack. The iOS 16 port is maintained on a separate branch; this README describes the iOS 27 branch only.

## Device support

| Device | Codename | Board setting | Status |
| --- | --- | --- | --- |
| OnePlus 7 Pro (GM191x) | `guacamole` | `BOARD=guacamole` / `mkadt.py --guacamole` | Bring-up target; runtime results below are from this phone |
| OnePlus 7T Pro (HD191x) | `hotdog` | `BOARD=hotdog` / `mkadt.py --hotdog` | **Code-level support only — not boot-tested.** Most hardware is shared with guacamole; see [`docs/hotdog.md`](docs/hotdog.md) for the differences, the build workflow and the on-device validation checklist. |

## Current status

Status snapshot: **2026-11-18**. For detailed evidence, caveats, and engineering history, see [`docs/ios27.md`](docs/ios27.md) and [`NOTES.md`](NOTES.md).

### Working

- **Boot and system UI:** iOS 27.2 reaches the lock screen and SpringBoard when started through the triple-boot selector. The UI runs at a 2x scale (MobileGestalt adjusted, see `NOTES.md`).
- **Display:** the dashed-line GPU corruption is gone: the kernel now presents each frame into a second buffer and flips the display pipes to it instead of overwriting the buffer being scanned out.
- **Shutdown and reboot:** shutdown and reboot work normally on iOS 27; rebooting through SSH has also been confirmed, and `rebootbl` reboots straight into fastboot.
- **Touch and navigation:** touch input works for navigating the UI and launching apps.
- **System apps:** the rebuilt root image puts staged system apps in `/Applications`; Filza was separately installed and confirmed to open. Safari loads pages.
- **Brightness and Night Shift:** both were confirmed working by the user through the `sweet-displayd` daemon and D73 backlight properties.
- **Audio:** speaker playback works without cut-outs (ringtones, system sounds, downloaded mp3s). The cause of the earlier cut-outs was iOS 27 assuming Apple's 24 MHz timebase; the kernel now presents a 24 MHz timebase on the phone's 19.2 MHz timer (`xnu/ios27/xnu12377-tb24.diff`). `adspd` starts at boot from a launchd job.
- **Haptics:** working, fixed together with audio (the haptics actuator runs through the same audio system).
- **Bluetooth:** the Settings switch works, devices are found, and Bluetooth audio plays.
- **Wi-Fi:** joining works and networks are remembered for auto-join; SSH works over Wi-Fi.
- **Camera:** both cameras work in the Camera app, for the preview and photos. The rear main camera (Sony IMX586) has contrast autofocus through its lens/OIS controller (LC898124EP3). The front camera (Sony IMX471) rises on its pop-up motor when selected and lowers when you switch away or close the app. The kernel drives the sensors, receivers, image front end and SMMU itself and hands raw frames to `ISP.mediacapture` through `/dev/hakios-camera` (`xnu/ios27/xnu12377-camhw.diff`, `userland/camera/`). The lens controller's program comes from OnePlus's GPL driver and is kept out of git: `tools/lc898124-fw.py` makes it, and it goes to `/var/local/fw/lc898124-so2820.fw` on the phone.
- **Passcode:** a device passcode can be set (4-digit or alphanumeric confirmed) and changed in Settings (turning it off is untested), and the lock screen asks for it with the matching number pad or keyboard. The kernel stands in for the Secure Enclave's credential manager and the keystore (`SweetACM`, `SweetKeyStore`, `bsd/dev/sweet_passcode.c`; `xnu/ios27/xnu12377-passcode.diff`, boot-arg `sweetpasscode=1`). It keeps only a PBKDF2 verifier in `/var/local/passcode.v1`: the passcode gates the UI but does not encrypt data. `sysctl kern.sweet_passcode_reset=1` (root) clears it. Ported from the redfin branch's iOS 16 passcode support. Face ID / Touch ID are not supported.
- **Cellular (no SIM):** iOS sees a modem: Settings shows Cellular, iOS reports "No SIM", and Settings > General > About shows the phone's real IMEI. The kernel publishes stand-ins for Apple's baseband drivers (from the redfin branch, boot-arg `sweetabp=3`) and answers CommCenter's QMI questions itself as a modem with no SIM; `wland` asks the real modem for the IMEI (`userland/wland/dms.c`). Needs MobileGestalt `HasBaseband`, `cellular-data`, `telephony` and `CellularTelephonyCapability` set (`mgtool setbool`). Only SIM slot 1; no calls, SMS or mobile data (CommCenter never reaches the real modem). See `xnu/ios27/xnu12377-cell1.diff`.
- **GPS through the modem:** `wland` runs a fix session on the modem's GNSS engine (QMI LOC, `userland/wland/gnss.c`) while `/var/local/gnss.on` exists, and `sweet-gnssfeed` (boot job `com.sweet.gnssfeed`) hands each fix to Location Services through locationd's simulation interface; Maps follows the modem's position. The modem locks its engine against our sessions: the word `unlock=all` in `gnss.on` clears that lock, after saving the original to `/var/local/gnss.lock-orig`; removing the word restores it. First fix takes a few minutes with no SIM or internet assistance; indoors accuracy is about 100 m.
- **Fake signal bars:** `sweet-bars` (boot job `com.sweet.bars`) publishes a cellular entry into SystemStatus' status-bar overrides from `/var/local/bars.conf`, e.g. `bars=4 type=lte text=Sweet` (bars 0-4; type none, edge, 3g, 4g, lte, 5ge, 5g, ...; text = carrier name, last). Delete the file to remove the override. Display only: no real service.
- **Memory:** iOS manages 5 GB (`hw.memsize=5367660544`), with a GPU pool of about 2 GB placed outside it.

### Partial or unreliable

- **Metal/GPU:** NativeAdrenoMetal and Turnip render the UI, at roughly 30-40 fps while scrolling (Turnip concurrent binning on: `tu-cb=1`). The display controller scales frames to the panel in hardware (QSEED3) instead of a GPU blit. Stutters are shorter (about 100 ms at worst while scrolling) but not gone; textures still use a linear layout. Unity games render since late store actions were added (Subway Surfers plays; `xnu/ios27/adrenometal-late-store.diff`, boot-arg `thid_should_crash=0`), but other apps and games are untested.
- **Landscape apps:** working (the rotated framebuffer, apps' rotated drawables, the scaler's rotation;
  `xnu/ios27/adrenometal-landscape.diff`, `xnu12377-scaler-rotate.diff`). Minecraft renders and plays, at a low frame
  rate (about 20 fps), after the Metal fixes for vertex attributes and `blitCommandEncoderWithDescriptor:`
  (`adrenometal-blitdesc.diff`) and skipping unchanged binds (`adrenometal-bindcache.diff`).
- **Performance:** better after removing the spinning `PerfPowerServices` job and preferring the big CPU cores, but `locationd` can spin on a full core after boot (killing it works around it) and `wland` polls Wi-Fi every millisecond (about 12% of a core).
- **Video playback:** plays in Safari (YouTube) and from local files since the YUV video layer fix (`xnu/ios27/xnu12377-fb-yuv.diff`); the player controls over the video are fixed in source but not yet confirmed on a build.
- **Camera limits:** the rear camera takes about 2 s to start, because the sensor setup and lens program are sent over a slow 100 kHz I2C bus. The preview is processed on the CPU from full 4000x3000 frames, so it is not smooth. The pop-up is moved on a timer like Android's, but its position sensors and drop detection are not used yet. The ultra-wide and telephoto cameras are not supported, and photos come out as single frames (no multi-frame processing).
- **Microphone:** input is unconfirmed.
- **Sleep:** iOS's system sleep panicked the kernel (`SweetCPU::haltCPU` is not implemented). `tools/mkadt.py` now sets `sleep-disabled` in the device tree so iOS no longer attempts it; not yet confirmed on a build. Turning the screen off is unaffected.
- **Identity:** the generated device tree uses the iPhone 14 Pro/D73 identity, but that does not guarantee all identity-dependent apps and services behave like they do on real iPhone hardware.

### Not working or not implemented

- **Full memory capacity:** about 5 of the phone's 8 GB go to iOS; giving it more needs XNU to use non-contiguous RAM ranges.
- **Suspend (system sleep), cellular calls/SMS/data with a SIM, NFC, and most onboard sensors:** no working end-to-end port has been demonstrated. Location Services work from Wi-Fi and from the modem's GPS (see above).

## Booting

Use the combined triple-boot image and select iOS 27 from the shim menu. The Alert Slider picks the default in the menu's order: top iOS 27, middle iOS 16, bottom Android. **Do not assume a standalone iOS wrapper image will boot**; this setup requires the selector/triple-boot path.

The existing data partition is named `ios26`, so iOS 27 boot arguments commonly contain `ufsroot=ios26`. That is the partition label, not the OS version. See [`docs/dualboot.md`](docs/dualboot.md) for the image layout and selector workflow. The phone's Android installation and partition backups should be preserved when building or booting test images.

On hotdog, build the selector with `BOARD=hotdog sh shim/build.sh` and the payloads with `tools/hotdog-mkboot.sh`; the stock `boot.img` passed to `tools/mkhotdogboot.py` / `tools/mktripleboot.py` must come from the 7T Pro. Note that T-series devices use a dynamic `super` partition, but iOS still lives in the standalone `userdata` partition. See [`docs/hotdog.md`](docs/hotdog.md).

## Build and project layout

- **iOS 27 kernel:** the tree at `work/xnu-12377` is not in this repository; each change made to it is kept as a diff in [`xnu/ios27/`](xnu/ios27/README.md).
- **Metal plugin:** source/build work is in the separate Git repository at `work/UniXNU`; it is not included in commits to this repository.
- **Root filesystem:** `tools/mkios27root-mac.sh` builds the iOS 27.2 root image on macOS from the prepared IPSW contents.
- **Device tree and payload:** `tools/mkadt.py`, `tools/mkpayload.py`, and the shim build create the ADT and kernel payload used by the boot image. Use `--guacamole` or `--hotdog` (both require `--highmem`); the two boards share the SM8150 tree, firmware nodes and framebuffer size.
- **Boot image:** `tools/mkguacboot.py` wraps the shim using the DTB and header of a phone's own stock `boot.img` (board-agnostic; `tools/mkhotdogboot.py` is an alias). `tools/guac-mkboot.sh` / `tools/hotdog-mkboot.sh` build an iOS payload for a board.
- **Triple-boot image:** `tools/mktripleboot.py` combines the stock boot image, selector shim, iOS 16 payload, and iOS 27 payload. See `docs/dualboot.md` for required inputs.
- **Userland tools and drivers:** `userland/` contains Wi-Fi, Bluetooth, audio, display, haptics, GPU and diagnostic code, including the phone-side diagnostics `ptop` (CPU, memory and process churn), `tsample` (sampling profiler), `tprobe` (clocks, timed waits, thread policies) and `rebootbl` (reboot to fastboot) in `userland/jb/`.
- **launchd jobs:** `tools/launchd-add-job.py` and `tools/launchd-drop-job.py` add and remove jobs in iOS's launchd plist (the `adspd` boot job, the removed `PerfPowerServices`).
- **Detailed status:** [`docs/ios27.md`](docs/ios27.md) tracks the current feature state; [`NOTES.md`](NOTES.md) records experiments, build artifacts, crashes and decisions. A source change or successful build does not by itself establish that a feature works at runtime.

## Security and artifacts

This port is for development on the owner's device. Code-signing, sandbox, and other security paths are modified or stubbed, so it should not be used for personal or sensitive data. iOS firmware, root images, device dumps, credentials, and generated build trees are not source-controlled; do not add them to Git.

## Credits

Built on Apple's open-source XNU and related releases, Qualcomm and OnePlus hardware documentation and kernel sources, [Mesa](https://mesa3d.org) Freedreno/Turnip, [HackiOS](https://github.com/oosexgang/hackios-xnu-kernel), [ios-topaz](https://github.com/MakrSas/ios-topaz), [ios-flame](https://github.com/NWylynko/ios-flame), [UniXNU](https://github.com/UniXNU/UniXNU), and [OtVaL228/ios-sweet](https://github.com/OtVaL228/ios-sweet). See source files and component READMEs for origins and licenses. iOS itself is Apple's and is not distributed here.
