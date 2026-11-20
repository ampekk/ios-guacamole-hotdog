/*
 * ptop: what the phone is busy with (its SSH shell has no ps/top). 2026-11-17, for "system responsiveness".
 *   ptop [SECS] [N]     (default 5 s, top 20)
 * Over SECS seconds: per-core load (host_processor_info), memory and paging (host_statistics64 HOST_VM_INFO64,
 * kern.memorystatus_level), processes started and gone (crash / respawn loops), and the N processes that used the
 * most CPU (proc_pidinfo PROC_PIDTASKINFO: user+system time, resident size, threads, context switches, page-ins,
 * faults). Output is plain text for the Mac. Signed with tsample.plist (root reads every process).
 */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>

/* <sys/proc_info.h> PROC_PIDTASKINFO (not in every SDK; same layout since 10.5) */
struct ptaskinfo {
	uint64_t virtual_size, resident_size, total_user, total_system, threads_user, threads_system;
	int32_t policy, faults, pageins, cow_faults, msgs_sent, msgs_recv, sys_mach, sys_unix, csw, threadnum, numrunning,
	    priority;
};
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#define PIDTASKINFO 4

struct pr {
	int pid;
	char name[MAXCOMLEN + 1];
	int ok;
	struct ptaskinfo t;
};

static struct pr *
snap(int *count)
{
	int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
	size_t len = 0;
	if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
		return NULL;
	len += 64 * sizeof(struct kinfo_proc);
	struct kinfo_proc *kp = malloc(len);
	if (!kp || sysctl(mib, 3, kp, &len, NULL, 0) != 0)
		return NULL;
	int n = (int)(len / sizeof(*kp));
	struct pr *p = calloc(n ? n : 1, sizeof(*p));
	for (int i = 0; i < n; i++) {
		p[i].pid = kp[i].kp_proc.p_pid;
		strncpy(p[i].name, kp[i].kp_proc.p_comm, MAXCOMLEN);
		p[i].ok = proc_pidinfo(p[i].pid, PIDTASKINFO, 0, &p[i].t, sizeof(p[i].t)) == (int)sizeof(p[i].t);
	}
	free(kp);
	*count = n;
	return p;
}

static struct pr *
find(struct pr *p, int n, int pid)
{
	for (int i = 0; i < n; i++)
		if (p[i].pid == pid)
			return &p[i];
	return NULL;
}

static int
cpu_ticks(natural_t *ncpu, processor_cpu_load_info_t *info)
{
	mach_msg_type_number_t cnt;
	return host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, ncpu, (processor_info_array_t *)info, &cnt);
}

static int
vm(vm_statistics64_data_t *v)
{
	mach_msg_type_number_t c = HOST_VM_INFO64_COUNT;
	return host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)v, &c);
}

struct row {
	struct pr *p;
	double cpu;
	int csw, pageins, faults;
};

static int
by_cpu(const void *a, const void *b)
{
	double x = ((const struct row *)a)->cpu, y = ((const struct row *)b)->cpu;
	return x < y ? 1 : x > y ? -1 : 0;
}

int
main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 5, top = argc > 2 ? atoi(argv[2]) : 20;
	if (secs < 1)
		secs = 1;
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	vm_size_t pgsz = 0;
	host_page_size(mach_host_self(), &pgsz);

	int n0, n1;
	natural_t nc0, nc1;
	processor_cpu_load_info_t c0, c1;
	vm_statistics64_data_t v0, v1;
	struct pr *p0 = snap(&n0);
	cpu_ticks(&nc0, &c0);
	vm(&v0);
	uint64_t t0 = mach_absolute_time();
	sleep(secs);
	struct pr *p1 = snap(&n1);
	cpu_ticks(&nc1, &c1);
	vm(&v1);
	double wall_ns = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom;
	if (!p0 || !p1) {
		fprintf(stderr, "ptop: sysctl KERN_PROC_ALL failed\n");
		return 1;
	}

	printf("ptop: %.2f s, %d processes\n", wall_ns / 1e9, n1);
	/* per core: user / system / idle share of the interval */
	double busy_all = 0;
	for (natural_t i = 0; i < nc1 && i < nc0; i++) {
		double d[CPU_STATE_MAX], tot = 0;
		for (int s = 0; s < CPU_STATE_MAX; s++) {
			d[s] = (double)(c1[i].cpu_ticks[s] - c0[i].cpu_ticks[s]);
			tot += d[s];
		}
		double busy = tot ? 100.0 * (tot - d[CPU_STATE_IDLE]) / tot : 0;
		busy_all += busy;
		printf("cpu%u: %5.1f%% busy (user %4.1f, sys %4.1f, nice %4.1f)\n", i, busy,
		    tot ? 100 * d[CPU_STATE_USER] / tot : 0, tot ? 100 * d[CPU_STATE_SYSTEM] / tot : 0,
		    tot ? 100 * d[CPU_STATE_NICE] / tot : 0);
	}
	printf("all cores: %.0f%% of one core busy in total\n", busy_all);

	double mb = (double)pgsz / (1024 * 1024);
	int level = -1;
	size_t ll = sizeof(level);
	sysctlbyname("kern.memorystatus_level", &level, &ll, NULL, 0);
	printf("memory: free %.0f MB, active %.0f, inactive %.0f, speculative %.0f, wired %.0f, compressor %.0f MB "
	    "(holding %.0f MB), purgeable %.0f; memorystatus level %d%%\n",
	    v1.free_count * mb, v1.active_count * mb, v1.inactive_count * mb, v1.speculative_count * mb,
	    v1.wire_count * mb, v1.compressor_page_count * mb, v1.total_uncompressed_pages_in_compressor * mb,
	    v1.purgeable_count * mb, level);
	printf("paging in %d s: pageins %llu, pageouts %llu, compressions %llu, decompressions %llu, swapins %llu, "
	    "swapouts %llu, faults %llu\n", secs,
	    (unsigned long long)(v1.pageins - v0.pageins), (unsigned long long)(v1.pageouts - v0.pageouts),
	    (unsigned long long)(v1.compressions - v0.compressions),
	    (unsigned long long)(v1.decompressions - v0.decompressions),
	    (unsigned long long)(v1.swapins - v0.swapins), (unsigned long long)(v1.swapouts - v0.swapouts),
	    (unsigned long long)(v1.faults - v0.faults));

	/* churn: started / gone during the interval */
	int born = 0, gone = 0;
	for (int i = 0; i < n1; i++)
		if (!find(p0, n0, p1[i].pid)) {
			if (born++ < 30)
				printf("started: %s[%d]\n", p1[i].name, p1[i].pid);
		}
	for (int i = 0; i < n0; i++)
		if (!find(p1, n1, p0[i].pid)) {
			if (gone++ < 30)
				printf("gone: %s[%d]\n", p0[i].name, p0[i].pid);
		}
	printf("processes started %d, gone %d\n", born, gone);

	struct row *r = calloc(n1 ? n1 : 1, sizeof(*r));
	int nr = 0, unreadable = 0;
	for (int i = 0; i < n1; i++) {
		if (!p1[i].ok) {
			unreadable++;
			continue;
		}
		struct pr *o = find(p0, n0, p1[i].pid);
		uint64_t before = o && o->ok ? o->t.total_user + o->t.total_system : 0;
		uint64_t now = p1[i].t.total_user + p1[i].t.total_system;
		r[nr].p = &p1[i];
		r[nr].cpu = 100.0 * (double)(now - before) * tb.numer / tb.denom / wall_ns;
		r[nr].csw = p1[i].t.csw - (o && o->ok ? o->t.csw : 0);
		r[nr].pageins = p1[i].t.pageins - (o && o->ok ? o->t.pageins : 0);
		r[nr].faults = p1[i].t.faults - (o && o->ok ? o->t.faults : 0);
		nr++;
	}
	qsort(r, nr, sizeof(*r), by_cpu);
	if (unreadable)
		printf("(%d processes unreadable)\n", unreadable);
	printf("%-7s %-17s %7s %8s %4s %8s %7s %8s\n", "PID", "NAME", "CPU%", "RSS MB", "THR", "CSW", "PAGEIN", "FAULTS");
	for (int i = 0; i < nr && i < top; i++)
		printf("%-7d %-17s %7.1f %8.1f %4d %8d %7d %8d\n", r[i].p->pid, r[i].p->name, r[i].cpu,
		    r[i].p->t.resident_size / 1048576.0, r[i].p->t.threadnum, r[i].csw, r[i].pageins, r[i].faults);
	/* biggest resident sets too: memory pressure culprits */
	printf("largest resident:");
	for (int k = 0; k < 8; k++) {
		int best = -1;
		for (int i = 0; i < nr; i++)
			if (r[i].p && (best < 0 || r[i].p->t.resident_size > r[best].p->t.resident_size))
				best = i;
		if (best < 0)
			break;
		printf(" %s %.0f MB%s", r[best].p->name, r[best].p->t.resident_size / 1048576.0, k < 7 ? "," : "");
		r[best].p = NULL;
	}
	printf("\n");
	return 0;
}
