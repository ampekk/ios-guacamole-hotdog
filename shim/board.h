#pragma once
/*
 * Per-board constants for the shim. Build with BOARD=guacamole (OnePlus 7 Pro)
 * or BOARD=hotdog (OnePlus 7T Pro); the default stays sweet. See build.sh.
 *
 * guacamole and hotdog share the SM8150 SoC layout, the PM8150 PMIC, the
 * 1440x3120 a8r8g8b8 splash framebuffer at 0x9c000000 and the same ABL reboot
 * behaviour (IMEM restart_reason ignored; PMIC PON_SOFT_RB_SPARE + PSCI).
 * Only BOARD_NAME differs here. Everything derived from the live FDT that is
 * genuinely board-specific lives in tools/mkadt.py (firmware blobs, wlan
 * compatible) and in the on-device validation list in docs/hotdog.md.
 */
#if defined(BOARD_GUACAMOLE) || defined(BOARD_HOTDOG)
/* OnePlus SM8150 phones (guacamole / hotdog). Facts from the live FDT +
 * guac-probe:
 *  - splash: two VIG pipes (720 px halves) scanning one 1440x3120 a8r8g8b8
 *    buffer at cont_splash_region, stride 5760; the panel keeps refreshing
 *    with no MDP writes from us (CTL_START kicks corrupt it: never kick).
 *  - this ABL ignores IMEM restart_reason: reboot-to-fastboot is the PMIC
 *    PON_SOFT_RB_SPARE reason + PSCI SYSTEM_RESET. Writing PS_HOLD from EL1
 *    is XPU-protected (QUALCOMM CrashDump Mode).
 *
 * hotdog note: these are inherited from guacamole, not probed on a 7T Pro.
 * The shared SoC/PMIC/panel facts are well established (see docs/hotdog.md);
 * confirm the splash address and reboot path with `fastboot boot` before
 * trusting them on a specific hotdog unit. */
#if defined(BOARD_HOTDOG)
#define BOARD_NAME		"hotdog"
#else
#define BOARD_NAME		"guacamole"
#endif
#define FB_W			1440
#define FB_H			3120
#define IMEM_RESTART_REASON	0x146bf65cUL	/* msm-imem@146bf000 + 0x65c (unused by ABL) */
#define REBOOT_VIA_PMIC_PON	1
#define PSHOLD_WRITABLE		0
#else
#define BOARD_NAME		"sweet"
#define FB_W			1080
#define FB_H			2400
#define IMEM_RESTART_REASON	0x146aa65cUL	/* qcom,msm-imem restart_reason@65c */
#define REBOOT_VIA_PMIC_PON	0
#define PSHOLD_WRITABLE		1
#endif
