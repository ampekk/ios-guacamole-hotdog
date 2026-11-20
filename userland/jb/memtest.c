/*
 * memtest: is all of the RAM iOS was given really there and working? 2026-11-17, for RAM step 1 (5 GiB for XNU:
 * DDR 0x80000000-0xbbb00000 minus reservations, plus 0x100000000-0x240000000 from mkadt.py --ram5g).
 *   memtest [MB]      (default 2560)
 * 1. What the kernel says: hw.memsize, max_mem, vm.pages, and every page counter (they should add up to ~5 GiB).
 * 2. Fills MB of anonymous memory in 64 MiB chunks with incompressible data (xorshift seeded per page), so the
 *    compressor cannot shrink it and the pages must come from real RAM, pushing total use past 4 GiB (into the
 *    range above 0x100000000 that only the 5 GiB layout has). Then reads everything back and counts every
 *    64-bit word that differs. Page counters are printed as it goes.
 * Other apps may be closed by the system to make room (memory pressure), as on any iPhone.
 * MIT, as userland/.
 */
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define CHUNK (64ull << 20)

static vm_size_t pg;

static void stats(const char *when)
{
	vm_statistics64_data_t v;
	mach_msg_type_number_t c = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&v, &c) != KERN_SUCCESS)
		return;
	double mb = (double)pg / 1048576.0;
	double sum = (v.free_count + v.active_count + v.inactive_count + v.speculative_count + v.wire_count +
	    v.throttled_count + v.compressor_page_count) * mb;
	int level = -1;
	size_t l = sizeof(level);
	sysctlbyname("kern.memorystatus_level", &level, &l, NULL, 0);
	printf("%-10s free %5.0f active %5.0f inactive %5.0f spec %4.0f wired %5.0f compressor %4.0f (holding %5.0f) "
	    "= %5.0f MB in pages; level %d%%\n", when, v.free_count * mb, v.active_count * mb, v.inactive_count * mb,
	    v.speculative_count * mb, v.wire_count * mb, v.compressor_page_count * mb,
	    v.total_uncompressed_pages_in_compressor * mb, sum, level);
}

static inline uint64_t xs(uint64_t *s)
{
	uint64_t x = *s;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return *s = x;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	uint64_t want = (argc > 1 ? strtoull(argv[1], NULL, 0) : 2560) << 20;
	host_page_size(mach_host_self(), &pg);
	uint64_t memsize = 0, maxmem = 0;
	size_t l = sizeof(memsize);
	sysctlbyname("hw.memsize", &memsize, &l, NULL, 0);
	host_basic_info_data_t hb;
	mach_msg_type_number_t c = HOST_BASIC_INFO_COUNT;
	if (host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&hb, &c) == KERN_SUCCESS)
		maxmem = hb.max_mem;
	unsigned int pages = 0;
	l = sizeof(pages);
	sysctlbyname("vm.pages", &pages, &l, NULL, 0);
	printf("hw.memsize %llu (%.3f GiB), max_mem %llu (%.3f GiB), vm.pages %u x %lu = %.0f MB\n",
	    (unsigned long long)memsize, memsize / 1073741824.0, (unsigned long long)maxmem, maxmem / 1073741824.0,
	    pages, (unsigned long)pg, (double)pages * pg / 1048576.0);
	stats("before");

	unsigned n = (unsigned)(want / CHUNK);
	uint64_t **chunk = calloc(n, sizeof(*chunk));
	unsigned got = 0;
	for (; got < n; got++) {
		void *p = mmap(NULL, CHUNK, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED) {
			printf("mmap failed at %u MB\n", got * 64);
			break;
		}
		chunk[got] = p;
		for (uint64_t off = 0; off < CHUNK; off += pg) {
			uint64_t s = ((uint64_t)got << 32 | (off / pg)) * 0x9e3779b97f4a7c15ull | 1;
			uint64_t *w = (uint64_t *)((char *)p + off);
			for (size_t i = 0; i < pg / 8; i++)
				w[i] = xs(&s);
		}
		if ((got + 1) % 4 == 0) {
			char b[32];
			snprintf(b, sizeof(b), "%u MB", (got + 1) * 64);
			stats(b);
		}
	}
	printf("filled %u MB; verifying\n", got * 64);
	uint64_t bad = 0, badpages = 0;
	for (unsigned k = 0; k < got; k++) {
		for (uint64_t off = 0; off < CHUNK; off += pg) {
			uint64_t s = ((uint64_t)k << 32 | (off / pg)) * 0x9e3779b97f4a7c15ull | 1;
			uint64_t *w = (uint64_t *)((char *)chunk[k] + off);
			uint64_t pb = 0;
			for (size_t i = 0; i < pg / 8; i++)
				if (w[i] != xs(&s))
					pb++;
			if (pb) {
				if (badpages < 10)
					printf("  MISMATCH chunk %u page offset 0x%llx: %llu words\n", k, (unsigned long long)off,
					    (unsigned long long)pb);
				badpages++;
				bad += pb;
			}
		}
	}
	stats("after");
	printf("RESULT: %u MB written and read back, %llu bad words in %llu pages: %s\n", got * 64,
	    (unsigned long long)bad, (unsigned long long)badpages, bad ? "FAIL" : "OK");
	return bad != 0;
}
