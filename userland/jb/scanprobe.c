/*
 * scanprobe: READ-ONLY look at how the display controller scans the panel buffer out, for page flipping
 * (2026-11-17: the GPU present writes CoreAnimation's frame into the ONE buffer the display reads continuously, so a
 * refresh taken while the blit is landing shows a scatter of old and new 64-byte lines; screenshots are clean).
 * Through /dev/pmem (boot-arg sweetpmem=1, root). Nothing is written.
 *
 * dispprobe (2026-11-09): MDP base 0x0ae01000; CTL0 is the only active path, LAYER(LM0) 01000005 = VIG0,
 * LAYER(LM1) 01000028 = VIG1, LM0/LM1 720x3120 each, INTF1 = DSI0 in command mode with autorefresh.
 * Here: the two source pipes VIG0/VIG1 (Linux dpu_hw_sspp.c offsets; sm8150 catalog VIG0 0x4000, VIG1 0x6000 from the
 * MDP base): sizes, source address and stride, format, op mode, fetch config; CTL0 again; INTF1's line counter
 * sampled to show where the refresh is. Only blocks that are in use are read (an unused one hung the bus before).
 *
 *   scanprobe        print it (send all of it); 2026-11-17: also the VIG pipes' QSEED3 scaler and pixel extension
 * MIT, as userland/.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MDP 0x0ae01000ull

static int fd = -1;

static uint32_t rd(uint64_t pa)
{
	uint32_t v = 0xdeadbeef;
	if (pread(fd, &v, 4, (off_t)pa) != 4)
		return 0xdeadbeef;
	return v;
}

static void row(const char *name, uint64_t pa, const unsigned *offs, const char *const *names, unsigned n)
{
	printf("%s %#010llx\n", name, (unsigned long long)pa);
	for (unsigned i = 0; i < n; i++)
		printf("  +%03x %-14s %08x\n", offs[i], names[i], rd(pa + offs[i]));
	fflush(stdout);
}

static uint64_t now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

int main(void)
{
	fd = open("/dev/pmem", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "/dev/pmem: %s (boot-arg sweetpmem=1, run as root)\n", strerror(errno));
		return 1;
	}
	uint32_t ver = rd(0x0ae00000), top = rd(MDP);
	printf("MDSS %08x, MDP TOP %08x\n", ver, top);
	if ((ver >> 28) != 5 || (top >> 28) != 5) {
		printf("not the DPU 5.x layout dispprobe saw: stopping\n");
		return 1;
	}
	static const unsigned ctl[] = { 0x000, 0x004, 0x014, 0x018, 0x01c, 0x0e4, 0x0f4, 0x0f8 };
	static const char *const ctln[] = { "LAYER(LM0)", "LAYER(LM1)", "TOP", "FLUSH", "START", "FETCH_ACTIVE",
		"INTF_ACTIVE", "MERGE_3D" };
	row("CTL0", MDP + 0x1000, ctl, ctln, 8);
	uint32_t l0 = rd(MDP + 0x1000), l1 = rd(MDP + 0x1004);
	if ((l0 & 7) == 0 || ((l1 >> 3) & 7) == 0) {
		printf("VIG0/VIG1 not staged as dispprobe saw (LAYER %08x %08x): stopping\n", l0, l1);
		return 1;
	}
	static const unsigned sspp[] = { 0x000, 0x008, 0x00c, 0x010, 0x014, 0x018, 0x01c, 0x020, 0x024, 0x028,
		0x030, 0x034, 0x038, 0x03c, 0x04c, 0x050, 0x054, 0x060, 0x064, 0x070, 0x0d0, 0x0d4 };
	static const char *const ssppn[] = { "SRC_SIZE", "SRC_XY", "OUT_SIZE", "OUT_XY", "SRC0_ADDR", "SRC1_ADDR",
		"SRC2_ADDR", "SRC3_ADDR", "SRC_YSTRIDE0", "SRC_YSTRIDE1", "SRC_FORMAT", "SRC_UNPACK", "SRC_OP_MODE",
		"SRC_CONSTCOLOR", "FETCH_CONFIG", "DANGER_LUT", "SAFE_LUT", "QOS_CTRL+?", "+064", "SW_PIX_EXT_C0?",
		"SRC_ADDR_SW_ST", "CURRENT_SRC0?" };
	row("VIG0", MDP + 0x4000, sspp, ssppn, sizeof(sspp) / sizeof(sspp[0]));
	row("VIG1", MDP + 0x6000, sspp, ssppn, sizeof(sspp) / sizeof(sspp[0]));
	/*
	 * 2026-11-17, for hardware scaling (the VIG pipes upscale CoreAnimation's 828x1792 frame to the panel instead of a
	 * GPU blit): each VIG pipe's QSEED3 scaler (Linux sm8150 catalog: scaler block at VIG + 0xa00; dpu_hw_util.c
	 * QSEED3_* offsets) and its pixel-extension registers (SSPP_SW_PIX_EXT_C0/C1C2/C3 at 0x100). Read only.
	 */
	static const unsigned qs[] = { 0x00, 0x04, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54,
		0x58, 0x90, 0x94, 0x98, 0x9c };
	static const char *const qsn[] = { "HW_VERSION", "OP_MODE", "PHASE_INIT", "STEP_Y_H", "STEP_Y_V", "STEP_UV_H",
		"STEP_UV_V", "PRELOAD", "SRC_SIZE_Y", "SRC_SIZE_UV", "DST_SIZE", "COEF_LUT_CTRL", "BUFFER_CTRL", "CLK_CTRL0",
		"CLK_CTRL1", "INIT_Y_H", "INIT_Y_V", "INIT_UV_H", "INIT_UV_V" };
	row("VIG0-QSEED3", MDP + 0x4000 + 0xa00, qs, qsn, sizeof(qs) / sizeof(qs[0]));
	row("VIG1-QSEED3", MDP + 0x6000 + 0xa00, qs, qsn, sizeof(qs) / sizeof(qs[0]));
	static const unsigned pe[] = { 0x100, 0x104, 0x108, 0x110, 0x114, 0x118, 0x120, 0x124, 0x128 };
	static const char *const pen[] = { "PE_C0_LR", "PE_C0_TB", "PE_C0_REQ", "PE_C12_LR", "PE_C12_TB", "PE_C12_REQ",
		"PE_C3_LR", "PE_C3_TB", "PE_C3_REQ" };
	row("VIG0-PE", MDP + 0x4000, pe, pen, sizeof(pe) / sizeof(pe[0]));
	row("VIG1-PE", MDP + 0x6000, pe, pen, sizeof(pe) / sizeof(pe[0]));
	/* INTF1 tear check line counter (+0x2b0, 0..height once a frame): where the refresh is, 40 samples 1 ms apart */
	printf("INTF1 line counter (1 ms apart):");
	for (int i = 0; i < 40; i++) {
		uint64_t t = now_us();
		printf(" %u", rd(MDP + 0x6a000 + 0x800 + 0x2b0) & 0xffff);
		while (now_us() - t < 1000)
			;
	}
	printf("\n");
	static const unsigned te[] = { 0x280, 0x284, 0x288, 0x28c, 0x294, 0x298, 0x29c, 0x2a0, 0x2a4, 0x2b0, 0x2b4 };
	static const char *const ten[] = { "+280", "TE_CONFIG?", "SYNC_CFG?", "VSYNC_INIT?", "HEIGHT", "+298",
		"START_POS?", "RD_PTR_IRQ?", "WR_PTR_IRQ?", "LINE_COUNT", "AUTOREFRESH" };
	row("INTF1-TE", MDP + 0x6a000 + 0x800, te, ten, sizeof(te) / sizeof(te[0]));
	return 0;
}
