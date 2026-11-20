/*
 * Boot menu (OnePlus SM8150 boards): three entries, volume up/down to move,
 * power to boot. Used by both guacamole and hotdog builds.
 *
 * Keys are read from the PM8150 over the SPMI PMIC arbiter (read only, the same
 * observer-channel EXT_READL the kernel's sweet_keys.c uses):
 *   power       PON INT_RT_STS (SID0 0x0810) bit 0, 1 = pressed
 *   volume down PON INT_RT_STS bit 1 (RESIN), 1 = pressed; or GPIO7 (0xc610) bit 0, 0 = pressed
 *   volume up   PM8150 GPIO6 INT_RT_STS (0xc510) bit 0, 0 = pressed
 * Drawing goes straight to the splash framebuffer (MMU off: Device memory, aligned
 * 32-bit stores only). Only what changes is redrawn.
 */
#include <stdint.h>
#include "board.h"
#include "console.h"
#include "font.h"
#include "menu.h"

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

/* ---- time ---- */
static uint64_t now(void)
{
	__asm__ volatile("isb" ::: "memory");
	return rsr("cntpct_el0");
}
static uint64_t ms_ticks(unsigned ms) { return rsr("cntfrq_el0") / 1000 * ms; }

/* ---- PMIC keys over SPMI (arbiter v5, EE0, read only) ---- */
#define SPMI_CORE	0x0c440000UL
#define SPMI_OBSRVR	0x0e600000UL
#define SPMI_CNFG	0x0c40a000UL

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static int apid_pon = -1, apid_vup = -1, apid_vdn = -1;

static int find_apid(uint32_t ppid)
{
	uint32_t n = rd32(SPMI_CORE + 0x4) & 0x7ff;
	int found = -1;
	if (n > 512)
		return -1;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t map = rd32(SPMI_CORE + 0x900 + 4 * i);
		if (!map || ((map >> 8) & 0xfff) != ppid)
			continue;
		if (found < 0 || (rd32(SPMI_CNFG + 0x700 + 4 * i) & 7) == 0)
			found = (int)i;
	}
	return found;
}

/* One byte of register addr of the peripheral behind apid; -1 on error. */
static int pmic_read(int apid, uint32_t addr)
{
	if (apid < 0)
		return -1;
	uintptr_t obs = SPMI_OBSRVR + 0x80UL * (unsigned)apid;
	wr32(obs, (1u << 27) | ((addr & 0xff) << 4));	/* EXT_READL, 1 byte */
	__asm__ volatile("dsb sy" ::: "memory");
	uint64_t end = now() + ms_ticks(2);
	while (now() < end) {
		uint32_t st = rd32(obs + 0x08);
		if (st & 1)
			return (st & 0x0e) ? -1 : (int)(rd32(obs + 0x18) & 0xff);
	}
	return -1;
}

static void keys_init(void)
{
	apid_pon = find_apid(0x008);	/* SID0 PON */
	apid_vup = find_apid(0x0c5);	/* SID0 GPIO6 */
	apid_vdn = find_apid(0x0c6);	/* SID0 GPIO7 */
}

#define KEY_POWER	1u
#define KEY_UP		2u
#define KEY_DOWN	4u

static unsigned keys_now(void)
{
	unsigned k = 0;
	int pon = pmic_read(apid_pon, 0x10), up = pmic_read(apid_vup, 0x10), dn = pmic_read(apid_vdn, 0x10);
	if (pon >= 0 && (pon & 1))
		k |= KEY_POWER;
	if (up >= 0 && !(up & 1))
		k |= KEY_UP;
	if ((pon >= 0 && (pon & 2)) || (dn >= 0 && !(dn & 1)))
		k |= KEY_DOWN;
	return k;
}

/* ---- drawing ---- */
#define BG		0xff0e0e12u
#define CARD		0xff24242bu
#define CARD_SEL	0xff0a84ffu
#define TEXT		0xffffffffu
#define TEXT_DIM	0xff8e8e93u
#define TEXT_OFF	0xff55555cu
#define BAR		0xff0a84ffu

static void fill(int x, int y, int w, int h, uint32_t c)
{
	for (int j = y; j < y + h && j < FB_H; j++) {
		volatile uint32_t *p = con_fb + j * (FB_STRIDE / 4);
		for (int i = x; i < x + w && i < FB_W; i++)
			p[i] = c;
	}
}

static int isqrt(int v)
{
	int r = 0;
	while ((r + 1) * (r + 1) <= v)
		r++;
	return r;
}

static void round_rect(int x, int y, int w, int h, int r, uint32_t c)
{
	for (int j = 0; j < h; j++) {
		int inset = 0;
		if (j < r)
			inset = r - isqrt(r * r - (r - j) * (r - j));
		else if (j >= h - r)
			inset = r - isqrt(r * r - (j - (h - r) + 1) * (j - (h - r) + 1));
		volatile uint32_t *p = con_fb + (y + j) * (FB_STRIDE / 4);
		for (int i = x + inset; i < x + w - inset; i++)
			p[i] = c;
	}
}

static int text_w(const char *s, int scale)
{
	int n = 0;
	while (s[n])
		n++;
	return n * 8 * scale;
}

/* Glyph pixels only: whatever is behind the text stays. */
static void text(int x, int y, int scale, const char *s, uint32_t fg)
{
	for (; *s; s++, x += 8 * scale) {
		char ch = *s < 0x20 || *s > 0x7e ? '?' : *s;
		const unsigned char *g = font8x16[ch - 0x20];
		for (int gy = 0; gy < 16; gy++)
			for (int gx = 0; gx < 8; gx++) {
				if (!(g[gy] & (0x80 >> gx)))
					continue;
				for (int sy = 0; sy < scale; sy++) {
					volatile uint32_t *p = con_fb + (y + gy * scale + sy) * (FB_STRIDE / 4) + x + gx * scale;
					for (int sx = 0; sx < scale; sx++)
						p[sx] = fg;
				}
			}
	}
}

static void text_c(int y, int scale, const char *s, uint32_t fg)
{
	text((FB_W - text_w(s, scale)) / 2, y, scale, s, fg);
}

/* ---- menu ---- */
#define CARD_X		120
#define CARD_W		(FB_W - 2 * CARD_X)
#define CARD_H		330
#define CARD_Y0		860
#define CARD_GAP	70
#define BAR_Y		2650

static void draw_entry(int i, const struct menu_entry *e, int selected)
{
	int y = CARD_Y0 + i * (CARD_H + CARD_GAP);
	fill(CARD_X - 12, y - 12, CARD_W + 24, CARD_H + 24, BG);	/* clears the old highlight ring */
	if (selected)
		round_rect(CARD_X - 12, y - 12, CARD_W + 24, CARD_H + 24, 64, CARD_SEL);
	round_rect(CARD_X, y, CARD_W, CARD_H, 52, CARD);
	text(CARD_X + 80, y + 70, 9, e->title, e->enabled ? TEXT : TEXT_OFF);
	text(CARD_X + 84, y + 70 + 16 * 9 + 24, 3, e->detail, e->enabled ? TEXT_DIM : TEXT_OFF);
}

static void draw_bar(uint64_t left, uint64_t total)
{
	int w = total ? (int)((uint64_t)CARD_W * left / total) : 0;
	fill(CARD_X, BAR_Y, w, 16, BAR);
	fill(CARD_X + w, BAR_Y, CARD_W - w, 16, BG);
}

int menu_choose(const struct menu_entry *e, int n, int sel, unsigned timeout_ms)
{
	keys_init();
	fill(0, 0, FB_W, FB_H, BG);
	text_c(300, 7, "Boot", TEXT);
	text_c(300 + 16 * 7 + 40, 3, "Volume up / down to choose, power to start", TEXT_DIM);
	for (int i = 0; i < n; i++)
		draw_entry(i, &e[i], i == sel);
	if (apid_pon < 0 || (apid_vup < 0 && apid_vdn < 0))
		text_c(BAR_Y + 80, 3, "keys not found: starting the default", TEXT_DIM);

	/* Keys held from power-on do not count: wait until all are released (at most 3 s). */
	uint64_t t = now(), end = t + ms_ticks(3000);
	while (keys_now() && now() < end)
		;
	uint64_t total = ms_ticks(timeout_ms), deadline = now() + total, last_bar = 0;
	int counting = timeout_ms != 0;
	unsigned prev = 0;
	for (;;) {
		uint64_t t1 = now();
		if (counting) {
			if (t1 >= deadline)
				break;
			if (t1 - last_bar > ms_ticks(50)) {
				draw_bar(deadline - t1, total);
				last_bar = t1;
			}
		}
		unsigned k = keys_now(), pressed = k & ~prev;
		prev = k;
		if (pressed && counting) {
			counting = 0;
			draw_bar(0, 0);
		}
		int move = (pressed & KEY_DOWN) ? 1 : (pressed & KEY_UP) ? -1 : 0;
		if (move) {
			int s = sel;
			do
				s = (s + move + n) % n;
			while (!e[s].enabled && s != sel);
			if (s != sel) {
				draw_entry(sel, &e[sel], 0);
				draw_entry(s, &e[s], 1);
				sel = s;
			}
		}
		if ((pressed & KEY_POWER) && e[sel].enabled)
			break;
		/* ~20 ms between polls, so one press is one step */
		uint64_t w = now() + ms_ticks(20);
		while (now() < w)
			;
	}
	return sel;
}

/* "Booting ..." on the menu background, centred: the screen until the kernel draws its own. */
void menu_booting(const char *what)
{
	fill(0, 0, FB_W, FB_H, BG);
	text_c(FB_H / 2 - 16 * 6 / 2, 6, what, TEXT);
}
