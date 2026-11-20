# iOS 27.2 kernel and driver diffs

The iOS 27.2 kernel is built from `work/xnu-12377` (Apple's xnu-12377.121.6 plus the SWEET port), which is not
in this repository. These are the individual changes made to that tree during the iOS 27 bring-up, kept here so
they are not only in the ignored `research/` folder. Each was made with `diff -u` against a backup of the file
just before the change, so they apply in the order the work was done (see `NOTES.md`), not to a clean xnu tree.

Wi-Fi (known networks / auto-join, confirmed on the phone 2026-11-16):
- `xnu12377-wifi-quiet-link.diff` - `bsd/dev/flame_sta.c`: no link-lost report while a join, leave or power-off
  of ours runs; Wi-Fi off in Settings leaves the network.
- `xnu12377-wifi-assoc540.diff` - `bsd/kern/kern_event.c` (SWEET: kernel events larger than one mbuf go in a
  cluster) and `bsd/dev/redwood_apple80211.c` (ASSOC_DONE carries the 540-byte record iOS 27's IO80211 reads; all
  zero, so it asks request 21 for the join result). Its base already had the 8-byte payload attempt.

Timebase, audio and haptics (confirmed on the phone 2026-11-17: no audio cut-outs, haptics and Bluetooth audio
work):
- `xnu12377-tb24.diff` - iOS 27 user space hard-codes Apple's 24 MHz timebase (CoreAudio converts host time as
  x * 125 / 3, and so do ~90 other places in the audio libraries), while the SM8150's generic timer runs at
  19.2 MHz: the audio server ran its I/O cycles 25 % slow and dropped a piece every 65536 frames. The kernel now
  presents 24 MHz: `pexpert/arm/pe_identify_machine.c` reports 24 MHz, `osfmk/arm64/machine_routines.c` scales
  every counter read by 5/4 and timer programming by 4/5, user space takes the mach_absolute_time /
  mach_continuous_time traps (USER_TIMEBASE_NONE), and `osfmk/arm64/sleh.c` emulates any remaining EL0 read of
  the counter or CNTFRQ. Boot-arg `sweettb24=0` turns it off.

Display and responsiveness:
- `xnu12377-fb-yuv.diff` - `SweetFramebuffer.cpp`: iOS 27 hands decoded video to the display as its own
  '420v'/'420f' (NV12) layer, which the compositor skipped (black video with sound). Those layers are now
  converted to BGRA (BT.709) underneath the page, which is blended over them so the player controls show
  (boot-arg `sweetfbyuv=0`: off). Confirmed: video plays (Safari, YouTube, local files).
- `xnu12377-fb-hwscale.diff` - `SweetFramebuffer.cpp` + `serial_console.c`: the display's VIG pipes scale the
  828x1792 frame to the 1440x3120 panel with their QSEED3 scalers (bilinear) instead of a GPU stretch blit; the
  page-flip buffers hold the logical frame, the GPU copies 1:1 (5.9 MB a frame instead of 18 MB) and the display
  reads a third of the memory. A panic first restores the 1:1 setup so the panic screen shows. Boot-arg
  `sweetfbhwscale=0`: off. Confirmed on the phone (registers checked with `scanprobe`, picture fine).
- `xnu12377-fb-cpuflip.diff` - `SweetFramebuffer.cpp`: frames the GPU present cannot take (video layers, a
  translucent top layer, more than two layers) are composed by the CPU into the hidden buffer and flipped to, instead
  of being written into the buffer the panel is scanning (that brought the dash artifacts back).
- `xnu12377-avoid-little.diff` - `sched_clutch.c` (+ the count in `SweetFramebuffer.cpp`'s 5 s statistics): Clutch
  had no avoid-processor check, so a busy thread stayed on the A55 it landed on; now a thread whose quantum expires on
  a little core moves to an idle big one (as Edge does). Printing from the scheduler panicked the first version.
- `xnu12377-fb-pageflip.diff` - `SweetFramebuffer.cpp`: present into a second buffer and flip the display pipes
  (VIG0/VIG1 SRC0_ADDR) instead of writing over the buffer being scanned out (the dashed-line corruption).
- `xnu12377-fb-asyncflip.diff` - the flip completes from a timer instead of a blocking wait in the present.
- `xnu12377-surface-cache-clean.diff` - `SweetSurface.cpp`: clean the CPU cache of an IOSurface on a writing
  unlock (boot-arg `sweetsurfclean`).
- `xnu12377-big-first2.diff` - `osfmk/kern/processor.c` + `sweet_cpufreq.c`: prefer the big cores (cpu4-7).
- `xnu12377-emul-atomic.diff` - `sleh.c`: emulated LRCPC2 loads/stores use single-copy-atomic user accesses.

Passcode (confirmed on the phone 2026-11-18: set, change, lock and unlock; number pad for numeric codes):
- `xnu12377-passcode.diff` - the whole passcode port, against the tree before it (one diff: `bsd/conf/files`,
  `iokit/conf/files.arm64`, new `bsd/dev/sweet_passcode.c`, `iokit/Kernel/arm/SweetACM.cpp`,
  `iokit/Kernel/arm/SweetACMGCM.h`, changed `iokit/Kernel/arm/SweetKeyStore.cpp`). From the redfin branch's iOS 16
  patches 0910/1195-1197, then adapted to iOS 27.2:
  - `sweet_passcode.c`: PBKDF2-HMAC-SHA256 verifier in `/var/local/passcode.v1` (never the passcode), backoff after
    failures, lock state for AKS; sysctls `kern.sweet_passcode`, `kern.sweet_passcode_reset`,
    `kern.sweet_passcode_state`; boot-arg `sweetpasscode=1`.
  - `SweetACM.cpp` (AppleCredentialManager stand-in): the iOS 27 commands coreauthd, profiled and Preferences use
    (context pools, SEPControl, external forms, ContextSetData/GetData with the size-only flag, VerifyPolicy).
    Settings wraps the new passcode as key 7 with ACMEncryptDataEx mode 2: AES-256-GCM under
    HKDF-SHA256(seed = key 13, salt = the context id, info "acm_transport"); the kernel unwraps it (tag checked)
    for profiled and for the keystore. `SweetACMGCM.h`: AES-256-GCM (this kernel has no corecrypto AES).
  - `SweetKeyStore.cpp`: change_secret with the iOS 27 16-byte ACM-context secrets and a temporary bag handle,
    verify_password with the packed buffer passed by address, output descriptors prepare()d before writing (a
    keybagd panic otherwise).

Cellular, no SIM (confirmed on the phone 2026-11-19: Cellular in Settings, "No SIM" alert, IMEI in About):
- `xnu12377-cell1.diff` - redfin's AppleBasebandPCI stand-in (ios-redfin xnu 1061/1063/1064 + 1194's prepare())
  ported to xnu-12377 and iOS 27.2 D73: new `bsd/dev/sweet_abp.c` and `iokit/Kernel/arm/SweetABP.cpp`, hooks in
  `bsd/conf/files`, `iokit/conf/files.arm64`, `bsd/vm/vm_unix.c` and `iokit/KernelConfigTables.cpp`. Boot-arg
  `sweetabp` (default 0: nothing): 1 publishes the baseband nub (D73 `/baseband` properties), AppleBaseband,
  AppleBasebandPCIControl with the qmux1/qmux2 pipes (libPCITransport picks the ABP transport by this service),
  trace and PDP stubs, and ApplePPM (CPMS needs it or ABM asserts); 2 is the fake boot (in CommCenter only,
  capabilities::abs::shouldBoot() in the 27.2 cache returns 0, words checked first). With no bridge on
  /dev/abpbridge the kernel answers QMI itself: CTL, UIM card status (one empty slot), DMS online/revision/IMEI (from
  `/var/local/cell/imei`, written by `userland/wland/dms.c` from the modem), NAS not registered, Apple services as
  redfin's policy, everything else QMI_ERR_NOT_SUPPORTED. Sysctls `kern.sweet_abp_stats`, `kern.sweet_abp_qmi_stats`.
  cell4d (confirmed 2026-11-19): AppleBaseband sel 0/1 (ABM resets / powers on the modem) send the 0xe3ff8000
  "reset detected" message 10 ms later. Without it the first baseband "crash" of a boot became an endless reset loop
  (ABM's 'Baseband Crash Recovery' check fired while ResetDetection still said "alive"): CommCenter at 400 % CPU and
  Settings frozen. Sending "alive" or "wake" after resets instead (cell4, cell4c) did not help.

Memory:
- `xnu12377-ram5g-gpupool.diff` - `sweet_gpu.c`: the GPU pool moves to free ranges outside XNU's RAM, for
  5 GB of RAM for iOS (`tools/mkadt.py --ram5g`) with a ~2 GB GPU pool.

GPU / Metal driver (`adrenometal-*.diff` are against `UniXNU/drivers/AdrenoMetal`):
- `adrenometal-resopts.diff` - pass only defined MTLResourceOptions bits (heap refusals for 0x600200).
- `adrenometal-recovery-exit.diff` - end a process whose VkDevice was recovered (it froze backboardd).
- `adrenometal-late-store.diff` - render encoders take late store actions (`setColorStoreAction:atIndex:`,
  `setDepthStoreAction:`, `setStencilStoreAction:`; bridge `nmb_encoder_set_store_action`). Unity begins every pass
  with `MTLStoreActionUnknown` and names the action before `endEncoding`; the calls were swallowed, the pass was
  lowered as DontCare and nothing reached the drawable (Subway Surfers all black). Also prints each blended
  attachment's blend in `dump-cb` output. Confirmed: Subway Surfers renders and plays.
- `adrenometal-dump-cb.diff` - boot-arg `dump-cb=NAME:FIRST[:COUNT]`: `NATIVE_METAL_DUMP_CB` for the processes whose
  name starts with NAME, their stderr into `/private/var/tmp/NativeAdrenoMetal.trace/dump-NAME-PID.log` (passes,
  draws, bindings and a brightness grid of each attachment read back after the command buffer).
- `adrenometal-roomstall.diff` (paths from the work tree, `UniXNU/...`) - bridge patch 0042 and its line in
  `scripts/build-adreno-metal-bridge.py`: metal2vulkan's `wait_for_room` waits at most 50 ms at a time; with no progress
  it hands its slot to the lowering thread again, and after 3 such waits lowers on its own thread
  (`ROOM_STALLS` counts them). Candy Crush froze 5-10 s with its main thread in `wait_for_room` and the lowering
  thread parked: a lost hand-off. Built in source only; not yet confirmed on the phone.
- `adrenometal-landscape.diff` - landscape apps (2026-11-19). backboardd asks for its portrait framebuffer surface
  (828x1792) as a 1792x828 texture with a private rotation (QuartzCore: setRotation:3, then
  newTextureWithDescriptor:iosurface:plane:); the runtime cannot make it (rows too short) and the screen went black
  ("Failed to allocate [1792 x 828] BGRA framebuffer"). Such a view that is drawn into (RenderTarget usage) now gets a
  private landscape texture of ours, turned into the surface at the end of every command buffer that draws into it,
  blits into it, writes it from a shader or presents it (port of the iOS 16 frontend's 0196). Sampled-only views are
  still refused, so CA goes through the scaler (`xnu12377-scaler-rotate.diff`). Apps' own rotated CAMetalLayer drawables
  (rotation 1) take the same path. Direction from the descriptor's rotation; `/var/local/sweet-rotate-flip` inverts it,
  `/var/local/sweet-norotate` turns it off, `/var/local/sweet-rotate-tint` tints the turn red (test). Always-on log lines
  "native-metal: rotated framebuffer: ..." (first 40 per process). Also: refused render pipelines log their vertex layout;
  error lines to stderr keep 600 characters. Confirmed: the landscape lock screen/launch screen and the turned frames
  reach the display.
- `adrenometal-vertex-absent.diff` - bridge patch 0043: a vertex attribute the function reads and the descriptor lacks is
  left unfetched instead of refusing the pipeline, as Apple's driver does. Confirmed: Minecraft's pipelines are no
  longer refused.
- `adrenometal-vattr-dumpfile.diff` - NBFunction answers vertexAttributes / stageInputAttributes (new bridge calls
  nmb_function_attribute_count/_at); bgfx (Minecraft) builds its vertex descriptors from them. Confirmed: Minecraft's
  descriptors are now filled. And dump-cb can come from the file /var/local/dump-cb (NAME:FIRST[:COUNT]) instead of the
  boot-arg; it also sets NMB_LOG_STDERR and SHENG_LOG_GPU for that process.
- `xnu12377-scaler-rotate.diff` - the CPU AppleM2ScalerCSCDriver stand-in turns W x H -> H x W BGRA/RGBA requests
  (QuartzCore's copy_iosurface in landscape; rectangles are the full sizes), and logs refused requests. New
  bsd/dev/sweet_scaler.c: `kern.sweet_scaler_rot` (0 refuse, 1 clockwise, 2 counter-clockwise), `kern.sweet_scaler_last`
  (latest refused/rotated request in hex), `kern.sweet_scaler_stats`. About 5-19 ms per turn on the CPU.
- `adrenometal-blitdesc.diff` (on top of `adrenometal-landscape.diff`) - command buffers answer
  `blitCommandEncoderWithDescriptor:` and `computeCommandEncoderWithDescriptor:`. Minecraft makes its upload encoders with
  the first; unanswered it got nil, every copyFromBuffer:toBuffer: into its vertex and index buffers was lost, and every
  frame was black. Confirmed: Minecraft renders (slowly). Also dump-cb-only diagnostics: each bound buffer's maker,
  contents/gpuAddress/didModifyRange counts and nonzero data at encode, commit and completion; a watch of large mapped
  buffers every 50 commits; the first callers that use each large buffer (image offsets); the first 24
  copyFromBuffer:toBuffer: calls; missing selectors to stderr.
  Also: -[NBBuffer contents] answers from a cached pointer after its first non-null answer (the bridge pins the buffer
  then); each bridge call had been about a quarter of Minecraft's render thread. Minecraft 10.5 -> 12.3 fps.
- `adrenometal-bindcache.diff` (on top of `adrenometal-blitdesc.diff`) - encoders skip a bind that changes nothing:
  the same buffer and offset, texture or sampler already in that slot (first 32 slots of compute, vertex, fragment
  and tile stages; setBytes forgets the slot's buffer; an unchanged setBufferOffset is skipped too). Minecraft 12.3 ->
  about 21 fps, rendering unchanged. Made/skipped counts go to the dump file every 500 commits with dump-cb on.

Boot-args: `thid_should_crash=0` (an existing xnu tunable) lets apps set non-identity-protected exception ports.
Apps run as platform binaries on this port, so the iOS 18+ rule for platform tasks killed any app whose crash
reporter (Crashlytics, Google ads) registers a Mach exception handler (EXC_GUARD, kGUARD_EXC_EXCEPTION_BEHAVIOR_ENFORCE):
Subway Surfers died at 90 % loading.
- `xnu12377-gpu-census.diff` - list every GPU pool holder when an allocation is refused.

The other `xnu12377-*.diff` files are earlier iOS 27 fixes (PAC emulation, data-protection emulation in VFS,
RENAME_SWAP/purgeable on HFS, disk0 nub, canvas size, syscall tracing, Apple80211 scan/join layouts, and others);
`NOTES.md` describes each.
