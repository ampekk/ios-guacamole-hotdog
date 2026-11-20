# Wi-Fi, Bluetooth, and haptics patches on iOS 27.2

## Scope and evidence

This is a source-level port assessment for the OnePlus 7 Pro (guacamole), comparing the iOS 16-era patches in `xnu/patches/` with their integrations in `work/xnu-12377/` (the XNU 12377 tree used for the iOS 27.2 work). It records API and hardware assumptions visible in those sources, plus the available iOS 27.2 shared-cache/rootfs material. It does not claim that these functions have been tested under a booted iOS 27.2 userspace.

The current XNU 12377 tree contains the Wi-Fi, Bluetooth, and haptics driver sources and build-file registrations. Existing objects are present for the Wi-Fi, Bluetooth, haptic C driver, and vibrator IOKit service in the SWEET development build directory. That demonstrates that these sources compile against this tree; it does not establish that iOS 27.2 daemons still speak the same private protocols or that the hardware is initialized at boot.

## Summary

| Subsystem | What the patches provide | XNU 12377 status | Main iOS 27.2 question |
| --- | --- | --- | --- |
| Wi-Fi | Qualcomm WCN3990 bring-up in userland (`wland`) plus an Apple-facing `/dev/wlan` / `en0` and Apple80211 shim (0138/0139). | Driver and networking glue are integrated and compile. The Apple80211 request layouts were explicitly based on iOS 16.5. | Does 27.2 `wifid` use the same ioctl types, structures, event payloads, and request sequence? |
| Bluetooth | H4 and H4BC kernel-control endpoints, a Skywalk KPIPE bridge, and the Skywalk registration privilege grant (0220–0223). | XNU 12377 retains the required kernel APIs. Static inspection of the 27.2 `bluetoothd` confirms its H4 path still opens `com.apple.uart.sk.bluetooth`, negotiates an `os_channel`, and transfers the H4 byte stream through channel slots. | Verify which transport 27.2 selects at runtime and resolve the BlueTool platform/chipset/firmware-row mismatch before expecting the existing WCN bridge to initialize. |
| Haptics | AW8697 I2C driver, `vibrator` IOKit service, and HakiosAudio actuator device (0248–0252, 0255, 0258). | C/C++ driver sources are integrated and compile. The code has a hard SE7 firmware prerequisite. | Is the I2C serial-engine firmware loaded before the driver probes, and do 27.2's haptic clients still use the same service/Actuator routes and capability answers? |

## Wi-Fi: 0138 and 0139

### Patch design

0138 exposes the Qualcomm shared-memory/RPMh facilities that `wland` needs. 0139 adds the kernel-facing network device and Apple80211 compatibility layer. The radio protocol itself is primarily userland: `userland/wland` brings up the SM8150/WCN3990 modem-side firmware path, QMI/WLFW, copy engines, WMI/HTT, scan and association, then WPA2 and DHCP. The kernel shim supplies `/dev/wlan`, `en0`, and the subset of BSD networking and Apple80211 requests that iOS expects.

This split is useful for the iOS 27 port: the modem, firmware, and Qualcomm radio protocol are not tied to the iOS release. The potentially release-sensitive layer is the Apple-facing interface between `wifid`/IO80211 and the kernel shim.

### Compatibility assessment

The XNU 12377 source still builds the `flame_wlan` and `redwood_apple80211` pieces. The implementation documents its private layouts as matching iOS 16.5 and includes static size/offset assertions for association, link events, channels, scan requests, and scan results. It handles `SIOCGA80211` and `SIOCSA80211`, and synthesizes Apple80211 kernel events. Those assertions protect the implementation's own assumptions; they do not prove that 27.2's IO80211 framework or `wifid` uses those same layouts.

**Assessment:** likely the most isolated port of the three. `wland` can remain unchanged if `/dev/pmem` and the modem/firmware environment remain compatible. The Apple80211 shim should be treated as an iOS 16 ABI implementation until 27.2 behavior is checked. A 27.2 `wifid` request/event trace is needed before calling Wi-Fi ported. In particular, compare request IDs and input/output lengths for scan, association, association result, state, channel and power, plus event class/subclass and payload offsets.

## Bluetooth: 0220–0223

### Patch design

0220 registers `com.apple.uart.bluetooth` as a stream kernel control for the BlueTool H4 byte stream and a separate privileged `com.sweet.bt.host` control for `btbridge`. 0221 adds `com.apple.uart.sk.bluetooth`, the endpoint expected by the Bluetooth stack's H4BC path. 0222 adds the Skywalk KPIPE path for the bridge. 0223 grants the user-pipe registration privilege that Skywalk checks when registering that provider.

The integration still calls `sweet_btuart_init()` from BSD startup. In the XNU 12377 headers and implementation, `CTL_FLAG_REG_SOCK_STREAM` is present, as are `kern_nexus_controller_register_provider` and the user-pipe privilege check. The port's custom AMFI/MAC hook grants `PRIV_SKYWALK_REGISTER_USER_PIPE` so the userspace bridge can register its KPIPE provider.

### Compatibility assessment

The kernel mechanisms themselves are present in XNU 12377, so there is no obvious kernel API removal blocking this patch series. This is stronger compatibility evidence than merely finding the patch text, but it only covers the kernel side.

The iOS 16 userland work selected a D22/iPhone X Broadcom profile: `BCM4357B1`, manufacturer `0x000f`, LMP subversion `0x420e`, and the USI firmware row. The existing Guacamole bridge instead drives the Qualcomm WCN3998 over UART and emulates the Broadcom-facing startup commands expected by that profile.

Static inspection of the iOS 27.2 `BlueTool` changes the main compatibility concern. The D73/iPhone 14 Pro platform hash is `d3b5ffca33e4604460655adfd77fdfa4757e7e93`; its firmware table has a D73/USI entry for `BCM4387C2` (`BCM4387C2_24.2.643.5978_PCIE_Juniper_CLPC_OS_USI_20260715.bin`). `getFirmwarePosForPlatform` compares the platform product hash, chipset and vendor, then tries the vendor-agnostic row. Thus the iOS 16 fake `BCM4357B1` identity and D22 profile do not match the D73 table entry. This is consistent with the observed 27.2 error, “Couldn't find a platform / chipset / firmware combination for the HW,” followed by BlueTool boot-script result 10.

This is a profile-selection mismatch before transport initialization. The D73 row is for a Broadcom PCIe controller; the handset hardware is Qualcomm WCN3998 on UART. Making BlueTool report `BCM4387C2` or simply selecting the D73 row would not make that PCIe boot script or firmware compatible with WCN3998. The working iOS 16 path is useful as a behavioral reference, but its D22 firmware profile cannot be carried over unchanged while iOS 27 presents the device as D73.

**Port decision:** retain the D73/iPhone 14 Pro identity globally. Bluetooth alone needs a compatibility selection: with the bridge's fake HCI version reporting `BCM4357B1`, BlueTool must select a UART-era BCM4357B1/USI profile instead of D73's BCM4387C2 PCIe profile. This is implemented as a narrow BlueTool firmware-table ProductHash alias, plus the Tuborg HCD filename expected by 27.2. It does not change MobileGestalt, the ADT identity, `bluetoothd` transport, or kernel controls. The bridge already answers Broadcom vendor commands locally and forwards ordinary HCI to WCN3998.

The 27.2 `bluetoothd` H4 transport contract is now verified from its `bluetoothd` binary (SHA-256 `2cac90606d92566a4109e6563a39b783e41de36cf2d6b6638f907e40998095a9`): it uses `PF_SYSTEM`/`SYSPROTO_CONTROL`, defaults to `com.apple.uart.sk.bluetooth`, and creates an `os_channel` after the same channel-open and UUID socket-option negotiation used by the iOS 16 path. This is the endpoint and protocol implemented by kernel patches 0221–0223. The generic H4 socket is not the actual byte-transfer path after channel creation. The 72-byte termios-style socket option is set after channel creation; the channel's nexus UUID and other attributes arrive through the socket negotiation and channel attributes rather than as literal `os_channel_create` arguments. See `research/ios27.2/bluetooth/transport-compare.md` for the call sequence.

This confirms transport compatibility, not end-to-end Bluetooth operation. BlueTool remains the earlier blocker: the 27.2 D73 profile selects a BCM4387C2 PCIe firmware row, while the OnePlus controller is a WCN3998 on UART. The iOS 16 D22/BCM4357B1 firmware boot script cannot simply be substituted into the D73 PCIe profile. Next, obtain the 27.2 runtime transport-selection and BlueTool profile-selection logs, then decide whether to expose a deliberately compatible BlueTool profile or add a D73-to-WCN UART profile. Do not change the kernel controls or fake the complete phone identity based only on the static transport match.

### Runtime confirmation (2026-11-15)

The Bluetooth switch currently cannot complete initialization. The captured `bluetoothd` log reports `BlueTool failed to run boot script with result 10` followed by `Unknown platform/chipset/manufacturer combination, restarting`. A verbose bridge session did initialize the WCN3998 UART and connect to `com.sweet.bt.host`; it then received BlueTool's BCM4357B1-profile `Read Local Version` and `0xFC2E` minidriver commands. This places the failure at BlueTool's platform/chipset/firmware-profile selection, before a usable Bluetooth controller is brought up, rather than in the H4/Skywalk transport negotiation.

The temporary foreground `btbridge -a` session was stopped after capture. Restarting that bridge alone will not fix the profile-selection error. The compatibility build now aliases the D73 hash to the legacy BCM4357B1 row and creates the 27.2 Tuborg USI HCD placeholder. Local build and signature checks pass; it has not yet been installed or runtime-tested because the phone was unreachable over SSH. Do not select the D73 BCM4387C2 PCIe script for this UART controller, and do not rebuild the kernel unless new logs identify a kernel-control failure.

See `research/ios27.2/bluetooth/d73-profile-bridge.md` for the exact changed files, artifact hashes, and staged installation/rollback procedure.

#### Follow-up runtime finding (2026-11-15)

The D73 profile alias and bridge are now installed. Live `btbridge.out` shows the WCN3998 UART at 3 Mbaud,
successful controller initialization, and the bridge endpoint available to BlueTool/bluetoothd. However, the
captured `bluetoothd` startup still logs `marconi-bt: Unable to match bluetooth service in device tree.` The
running ADT currently publishes the `bluetooth` nub directly under `/arm-io`; it lacks the D22-shaped
`/arm-io/uart1/bluetooth` path. Therefore the current toggle blocker is service discovery, before judging the
remaining HCI/Skywalk command mapping. A test kernel + D73 ADT with the nested service path and an explicit
IOKit nub is built as `outputs/guacamole-ios27a-bt-d73-triple.img`; it has not been booted. Retain D73 identity;
boot only through the tripleboot selector and verify Marconi service matching before further transport changes.

## Haptics: 0248–0252, 0255, 0258

### Patch design

0248 implements the AW8697 driver on the phone's I2C/GENI path. 0249 publishes an IOKit service named `vibrator` for the legacy FigVibrator property interface. 0250–0252 add stop/reinitialization handling and diagnostics. 0255 exposes `/dev/hakios-haptic`, which the HakiosAudio `Actuator` path writes as 48 kHz mono signed 16-bit samples; the kernel translates those samples into AW8697 waveforms. 0258 adds recovery from a wedged device. The feature also depends on the device-tree/MobileGestalt capability setup and boot arguments such as `sweethaptic=2`.

### Compatibility assessment

The XNU 12377 tree includes and compiles the haptic driver and `SweetVibrator`. The current driver source explicitly stops if GENI SE7 does not already have I2C protocol firmware (protocol value 3) loaded by the boot chain. Its comment says this kernel code does not load that serial-engine firmware. Therefore the C/C++ port can compile and the service can register while the motor remains unavailable. This is a hardware/boot-chain prerequisite, not an iOS 27 ABI issue.

The actuator route is also version- and identity-sensitive at the system level. The driver comments tie it to `DeviceSupportsClosedLoopHaptics` and `/product/haptics`; the ADT generator currently includes `/product/haptics` by default. That can make iOS choose the Actuator route, but the 27.2 `HakiosAudio`/VirtualAudio and FigVibrator selection behavior has not been verified. The legacy `vibrator` service and the Actuator device are separate routes and should be tested independently.

**Assessment:** the kernel API port is straightforward, but hardware readiness is the key blocker. First check the early boot log for the SE7 firmware/protocol diagnostic and confirm it reaches the configured state. If it reports missing firmware, the next work belongs in the boot chain or an early GENI firmware loader. Then verify that 27.2 routes a test vibration to either the `vibrator` service or `/dev/hakios-haptic`, and that the chosen MobileGestalt/ADT identity actually selects the intended path.

## Recommended port order

1. **Bluetooth:** inspect 27.2 BlueTool and bluetoothd transport setup against the existing control names, socket options, Skywalk channel creation, and KPIPE framing. This can identify a concrete userland ABI mismatch without touching hardware setup.
2. **Wi-Fi:** capture 27.2 `wifid` Apple80211 requests and compare request numbers, lengths, structures, and events to `redwood_apple80211.c`; keep `wland` unchanged unless modem bring-up itself fails.
3. **Haptics:** verify SE7 protocol firmware availability first, then test the `vibrator` and Actuator routes separately under the 27.2 userspace and device identity.

No kernel rebuild, rootfs edit, or phone-side install was performed for this assessment.
