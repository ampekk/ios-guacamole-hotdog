/*
 * sweet-bars: fake cellular signal bars in the status bar (ios-sweet iOS 27, 2026-11-19, Claude).
 * iOS 27's status bar reads SystemStatus' data (STStatusBarData); SystemStatus has a publisher domain for overrides,
 * "status-bar-overrides" (STStatusBarOverridesStatusDomainPublisher, data STMutableStatusBarOverridesStatusDomainData
 * with customOverrides = an STMutableStatusBarData), the mechanism behind simctl's status_bar override. A process
 * holding com.apple.systemstatus.publisher.domains = [status-bar-overrides] (sweet-bars-ents.plist) can publish into
 * it. The published data lives as long as the publisher's connection: this is a daemon that keeps it.
 * bars3: the publisher service (com.apple.systemstatus.publisher) is vended by SpringBoard in the user session (the
 * instance the status bar reads) and by systemstatusd in the system domain; a root process looks up the latter,
 * which refused us ("Server connection rejected due to missing entitlement", outputs/bars2-os.log). As jb/nosim:
 * take SpringBoard's bootstrap port (task_for_pid) and re-exec ourselves with it, so libxpc starts in SpringBoard's
 * namespace (a port set after start-up is not seen by libxpc's cached bootstrap).
 *
 *   sweet-bars probe            print the classes and methods involved (with type encodings), to fix the calls below
 *   sweet-bars                  daemon: reads /var/local/bars.conf every 2 s and publishes it; file absent = no override
 *   sweet-bars once BARS [TEXT] publish once and stay until Ctrl-C (test)
 * bars.conf: one line, e.g. "bars=3 type=lte text=Sweet" (bars 0..4; type: none, 1x, gprs, edge, 3g/umts, 4g, lte,
 * 5ge, ltea, lteplus, 5g, 5gplus, 5guwb, 5guc; text = the carrier name, last on the line; defaults bars=4 type=lte,
 * no text). Delete the file to remove the override.
 * bars4 (2026-11-19): works. The earlier "missing entitlement" rejections were our own binary having no code
 * signature: scp over an existing signed file keeps the kernel's cached signature of the old contents, which no
 * longer matches, so the new one is dropped and every entitlement read is empty. Install as NAME.new + mv.
 * bars2 (from the bars1 probe, outputs/bars1-probe.txt): the cellular entry comes from STStatusBarDataCellularEntry's
 * entryWithType:...displayRawValue: factory (entries are immutable), and goes in with the overrides data's
 * -setEntry:forKey: (key "cellularEntry"). MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <errno.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <mach/mach.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONF "/var/local/bars.conf"

static const char *const probe_classes[] = {
	"STStatusDomainPublisher", "STStatusBarOverridesStatusDomainPublisher", "STStatusBarOverridesStatusDomainData",
	"STMutableStatusBarOverridesStatusDomainData", "STStatusBarOverridesStatusDomain",
	"STStatusBarDataAdditionsStatusDomainPublisher", "STMutableStatusBarDataAdditionsStatusDomainData",
	"STStatusBarData", "STMutableStatusBarData", "STStatusBarDataEntry", "STStatusBarDataCellularEntry",
	"STStatusBarDataStringEntry", "STStatusBarDataNetworkEntry", NULL,
};

static void dump_methods(Class c, int meta)
{
	unsigned n = 0;
	Method *m = class_copyMethodList(meta ? object_getClass((id)c) : c, &n);
	for (unsigned i = 0; i < n; i++)
		printf("  %c%s  %s\n", meta ? '+' : '-', sel_getName(method_getName(m[i])), method_getTypeEncoding(m[i]) ?: "?");
	free(m);
}

static int probe(void)
{
	for (int i = 0; probe_classes[i]; i++) {
		Class c = objc_getClass(probe_classes[i]);
		printf("== %s%s\n", probe_classes[i], c ? "" : ": NOT FOUND");
		if (!c)
			continue;
		printf("  superclasses:");
		for (Class s = class_getSuperclass(c); s; s = class_getSuperclass(s))
			printf(" %s", class_getName(s));
		printf("\n");
		unsigned np = 0;
		objc_property_t *p = class_copyPropertyList(c, &np);
		for (unsigned k = 0; k < np; k++)
			printf("  @property %s  %s\n", property_getName(p[k]), property_getAttributes(p[k]));
		free(p);
		dump_methods(c, 1);
		dump_methods(c, 0);
	}
	return 0;
}

/* ---- ents: what this process sees of its own entitlements (bars3 rejection: SystemStatus' client code reads
 * com.apple.systemstatus.publisher.domains from its own audit token and found no array/string) ---- */

typedef struct __SecTask *SecTaskRef;
extern SecTaskRef SecTaskCreateFromSelf(CFAllocatorRef);
extern CFTypeRef SecTaskCopyValueForEntitlement(SecTaskRef, CFStringRef, CFErrorRef *);
extern int csops(pid_t, unsigned int, void *, size_t);

static pid_t find_pid(const char *name);

static int ents(pid_t pid)
{
	uint32_t fl = 0;
	if (pid) {
		/* another process: flags and the size of its XML entitlements blob (CS_OPS_ENTITLEMENTS_BLOB) */
		int r = csops(pid, 0, &fl, sizeof(fl));
		uint8_t hdr[8] = { 0 };
		int r2 = csops(pid, 7, hdr, sizeof(hdr));
		printf("pid %d: csops status %d flags 0x%08x; entitlements blob: %d (errno %d), length %u\n", pid, r, fl, r2,
		       r2 ? errno : 0, (unsigned)(hdr[4] << 24 | hdr[5] << 16 | hdr[6] << 8 | hdr[7]));
		return 0;
	}
	int r = csops(getpid(), 0 /* CS_OPS_STATUS */, &fl, sizeof(fl));
	printf("csops status: %d, flags 0x%08x (CS_VALID 0x1 %s, platform 0x4000000 %s)\n", r, fl, (fl & 1) ? "yes" : "NO",
	       (fl & 0x4000000) ? "yes" : "no");
	SecTaskRef t = SecTaskCreateFromSelf(NULL);
	CFErrorRef err = NULL;
	CFTypeRef v = t ? SecTaskCopyValueForEntitlement(t, CFSTR("com.apple.systemstatus.publisher.domains"), &err) : NULL;
	printf("SecTask: %s%s\n", v ? [[(__bridge id)v description] UTF8String] : "nil",
	       err ? [[(__bridge NSError *)err description] UTF8String] : "");
	dlopen("/System/Library/PrivateFrameworks/BaseBoard.framework/BaseBoard", RTLD_NOW);
	Class at = objc_getClass("BSAuditToken");
	id tok = at ? ((id (*)(id, SEL))objc_msgSend)(at, sel_registerName("tokenForCurrentProcess")) : nil;
	id bv = tok ? ((id (*)(id, SEL, id))objc_msgSend)(tok, sel_registerName("valueForEntitlement:"),
							  @"com.apple.systemstatus.publisher.domains") : nil;
	printf("BSAuditToken: token %s, value %s\n", tok ? "yes" : "nil", bv ? [[bv description] UTF8String] : "nil");
	id pv = tok ? ((id (*)(id, SEL, id))objc_msgSend)(tok, sel_registerName("valueForEntitlement:"), @"platform-application") : nil;
	printf("BSAuditToken platform-application: %s\n", pv ? [[pv description] UTF8String] : "nil");
	return 0;
}

/* ---- SpringBoard's namespace ---- */

extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *sp);

static pid_t find_pid(const char *name)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t len = 0;
	pid_t r = 0;
	if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
		return 0;
	struct kinfo_proc *pr = malloc(len);
	if (pr && sysctl(mib, 3, pr, &len, NULL, 0) == 0)
		for (size_t i = 0; i < len / sizeof(*pr); i++)
			if (strcmp(pr[i].kp_proc.p_comm, name) == 0)
				r = pr[i].kp_proc.p_pid;
	free(pr);
	return r;
}

/* returns only when it could not (or need not) switch; on success the process is replaced */
static void join_springboard(char **argv)
{
	if (getenv("SWEET_BARS_JOINED")) {
		mach_port_t probe = MACH_PORT_NULL;
		kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.systemstatus.publisher", &probe);
		printf("sweet-bars: in SpringBoard's namespace (pid %s); publisher service %s\n", getenv("SWEET_BARS_JOINED"),
		       kr == KERN_SUCCESS ? "found" : "NOT found");
		return;
	}
	pid_t sb = 0;
	for (int i = 0; i < 30 && !(sb = find_pid("SpringBoard")); i++)
		sleep(2);		/* at boot the daemon may start before SpringBoard */
	mach_port_t task = MACH_PORT_NULL, bp = MACH_PORT_NULL;
	if (!sb || task_for_pid(mach_task_self(), sb, &task) != KERN_SUCCESS ||
	    task_get_special_port(task, TASK_BOOTSTRAP_PORT, &bp) != KERN_SUCCESS || bp == MACH_PORT_NULL) {
		printf("sweet-bars: cannot take SpringBoard's (pid %d) bootstrap port: staying in our own\n", sb);
		return;
	}
	if (task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, bp) != KERN_SUCCESS) {
		printf("sweet-bars: cannot set our bootstrap port\n");
		return;
	}
	char v[16];
	snprintf(v, sizeof(v), "%d", sb);
	setenv("SWEET_BARS_JOINED", v, 1);
	execv(argv[0][0] == '/' ? argv[0] : "/var/local/bin/sweet-bars", argv);
	printf("sweet-bars: re-exec failed\n");
}

/* ---- publishing ---- */

static id pub;
static volatile sig_atomic_t quit;
static void on_sig(int s) { (void)s; quit = 1; }

static BOOL has(id o, const char *sel)
{
	BOOL r = o && [o respondsToSelector:sel_registerName(sel)];
	if (!r)
		printf("sweet-bars: %s has no -%s\n", o ? object_getClassName(o) : "(nil)", sel);
	return r;
}

static id get_o(id o, const char *sel)
{
	return has(o, sel) ? ((id (*)(id, SEL))objc_msgSend)(o, sel_registerName(sel)) : nil;
}

struct want {
	int on, bars, type;
	char text[64];
};

/*
 * STStatusBarDataCellularEntry type values, as `sweet-bars types` printed SystemStatus' names for them
 * (outputs/bars3-types.txt): 0 none, 1 1x, 2 GPRS, 3 Edge, 4 UMTS, 5 4G, 6 LTE, 7 5GE, 8 LTEA, 9 LTEPlus, 10 5G,
 * 11 5GPlus, 12 5GUWB, 13 5GUC (14 Satellite, 15 5GA). Network status 5 = connected.
 */
static int type_of(const char *s)
{
	static const char *const n[] = { "none", "1x", "gprs", "edge", "umts", "4g", "lte", "5ge", "ltea", "lteplus",
					 "5g", "5gplus", "5guwb", "5guc", NULL };
	for (int i = 0; n[i]; i++)
		if (!strcmp(s, n[i]))
			return i;
	if (!strcmp(s, "3g"))
		return 4;
	return 6;
}
#define NET_CONNECTED 5

static void read_conf(struct want *w)
{
	char b[256] = "";
	memset(w, 0, sizeof(*w));
	FILE *f = fopen(CONF, "r");
	if (!f)
		return;
	size_t k = fread(b, 1, sizeof(b) - 1, f);
	fclose(f);
	b[k] = 0;
	w->on = 1;
	w->bars = 4;
	w->type = 6;
	char *p;
	if ((p = strstr(b, "bars=")))
		w->bars = atoi(p + 5);
	if (w->bars < 0)
		w->bars = 0;
	if (w->bars > 4)
		w->bars = 4;
	if ((p = strstr(b, "type="))) {
		char t[16] = "";
		sscanf(p + 5, "%15[a-z0-9]", t);
		w->type = type_of(t);
	}
	if ((p = strstr(b, "text="))) {
		sscanf(p + 5, "%63[^\n]", w->text);
		char *e = strstr(w->text, " bars=");
		if (e)
			*e = 0;
		e = strstr(w->text, " type=");
		if (e)
			*e = 0;
	}
}

static id make_cell_entry(const struct want *w)
{
	Class c = objc_getClass("STStatusBarDataCellularEntry");
	SEL f = sel_registerName("entryWithType:stringValue:suffixStringValue:crossfadeStringValue:badgeStringValue:"
				 "wifiCalling:callForwarding:showsSOSWhenDisabled:sosAvailable:isBootstrapCellular:status:"
				 "numberSharingState:lowDataMode:rawValue:displayValue:displayRawValue:");
	if (!c || ![c respondsToSelector:f]) {
		printf("sweet-bars: no STStatusBarDataCellularEntry factory\n");
		return nil;
	}
	NSString *text = [NSString stringWithUTF8String:w->text];
	/* q type, @ string, @ suffix, @ crossfade, @ badge, B wifiCalling, B callForwarding, B showsSOSWhenDisabled,
	 * B sosAvailable, B isBootstrapCellular, q status, q numberSharingState, B lowDataMode, q rawValue,
	 * q displayValue (bars), B displayRawValue */
	id e = ((id (*)(id, SEL, long long, id, id, id, id, BOOL, BOOL, BOOL, BOOL, BOOL, long long, long long, BOOL,
			long long, long long, BOOL))objc_msgSend)(c, f, w->type, text, nil, nil, nil, NO, NO, NO, NO, NO,
								   NET_CONNECTED, 0, NO, -70 - 10 * (4 - w->bars), w->bars, NO);
	printf("sweet-bars: entry %s\n", e ? [[e description] UTF8String] : "nil");
	return e;
}

static int publish(const struct want *w)
{
	Class pc = objc_getClass("STStatusBarOverridesStatusDomainPublisher");
	if (!pc) {
		printf("sweet-bars: no STStatusBarOverridesStatusDomainPublisher\n");
		return -1;
	}
	if (!pub) {
		pub = [[pc alloc] init];
		printf("sweet-bars: publisher %s\n", pub ? "created" : "NOT created");
	}
	SEL upd = sel_registerName("updateDataWithBlock:");
	if (![pub respondsToSelector:upd]) {
		printf("sweet-bars: publisher has no -updateDataWithBlock:\n");
		return -1;
	}
	__block int ok = 0;
	struct want ww = *w;
	void (^blk)(id) = ^(id data) {
		printf("sweet-bars: update block: data is %s\n", data ? object_getClassName(data) : "nil");
		SEL se = sel_registerName("setEntry:forKey:");
		if (![data respondsToSelector:se]) {
			printf("sweet-bars: overrides data has no -setEntry:forKey:\n");
			return;
		}
		id e = ww.on ? make_cell_entry(&ww) : nil;
		((void (*)(id, SEL, id, id))objc_msgSend)(data, se, e, @"cellularEntry");
		id ov = get_o(data, "customOverrides");
		printf("sweet-bars: customOverrides now: %s\n", ov ? [[ov description] UTF8String] : "nil");
		ok = 1;
	};
	((void (*)(id, SEL, id))objc_msgSend)(pub, upd, blk);
	printf("sweet-bars: %s: bars %d, type %d, text \"%s\" (block %s)\n", w->on ? "published" : "cleared", w->bars,
	       w->type, w->text, ok ? "ran" : "did not run (yet)");
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	@autoreleasepool {
		if (!dlopen("/System/Library/PrivateFrameworks/SystemStatus.framework/SystemStatus", RTLD_NOW))
			printf("sweet-bars: SystemStatus not loaded: %s\n", dlerror());
		if (argc > 1 && !strcmp(argv[1], "probe"))
			return probe();
		if (argc > 1 && !strcmp(argv[1], "ents"))
		{
			pid_t tp = 0;
			if (argc > 2) {
				tp = atoi(argv[2]);
				if (tp <= 0 && !(tp = find_pid(argv[2]))) {
					printf("no process named %s\n", argv[2]);
					return 1;
				}
				printf("%s:\n", argv[2]);
			}
			return ents(tp);
		}
		if (argc > 1 && !strcmp(argv[1], "types")) {
			/* the name SystemStatus gives each type value */
			for (int t = 0; t <= 20; t++) {
				struct want w = { 1, 4, t, "" };
				id e = make_cell_entry(&w);
				(void)e;
			}
			return 0;
		}
		if (argc > 1 && !strcmp(argv[argc - 1], "--springboard"))
			join_springboard(argv);		/* opt-in: the server is systemstatusd either way (bars4) */
		else if (getenv("SWEET_BARS_JOINED"))
			join_springboard(argv);
		signal(SIGTERM, on_sig);
		signal(SIGINT, on_sig);
		struct want cur, w;
		memset(&cur, 0, sizeof(cur));
		cur.bars = -1;
		if (argc > 2 && !strcmp(argv[1], "once")) {
			memset(&w, 0, sizeof(w));
			w.on = 1;
			w.bars = atoi(argv[2]);
			w.type = 6;
			if (argc > 3)
				snprintf(w.text, sizeof(w.text), "%s", argv[3]);
			publish(&w);
			while (!quit)
				[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
			return 0;
		}
		printf("sweet-bars: watching %s\n", CONF);
		while (!quit) {
			@autoreleasepool {
				read_conf(&w);
				if (memcmp(&w, &cur, sizeof(w))) {
					if (w.on || cur.on)
						publish(&w);
					cur = w;
				}
				[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2]];
			}
		}
		if (cur.on) {
			w.on = 0;
			publish(&w);
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
		}
	}
	return 0;
}
