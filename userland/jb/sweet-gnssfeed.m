/*
 * sweet-gnssfeed: hand the modem's GNSS fixes to locationd (ios-sweet iOS 27 gnss1, 2026-11-19, Claude).
 * wland's LOC client (userland/wland/gnss.c) writes each good fix to /var/local/gnss.fix; this daemon reads it once a
 * second and feeds it to locationd through its simulation interface (CoreLocation's private CLSimulationManager, the
 * route jb/simloc proved). A fix older than 15 s (no sky view, gnss.on removed, wland gone) ends the simulation, so
 * Wi-Fi location takes over again; the next fresh fix starts it again. SIGTERM/SIGINT end it too.
 *   sweet-gnssfeed [-v]     -v: print every fix fed (coordinates rounded to 0.01 deg)
 * Needs com.apple.locationd.simulation (simloc-ents.plist). MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#include <objc/message.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FIX_FILE	"/var/local/gnss.fix"
#define STALE_S		15

static id mgr;
static volatile sig_atomic_t quit;

static void call(const char *name)
{
	SEL s = sel_registerName(name);
	if ([mgr respondsToSelector:s])
		((void (*)(id, SEL))objc_msgSend)(mgr, s);
	else
		printf("gnssfeed: CLSimulationManager has no -%s\n", name);
}

static void call_u8(const char *name, uint8_t v)
{
	SEL s = sel_registerName(name);
	if ([mgr respondsToSelector:s])
		((void (*)(id, SEL, uint8_t))objc_msgSend)(mgr, s, v);
}

static void on_sig(int s)
{
	(void)s;
	quit = 1;
}

struct fix {
	unsigned long long t;
	double lat, lon, acc, alt, spd, hdg;
	unsigned n;
};

static int read_fix(struct fix *f, time_t *mtime)
{
	struct stat st;
	char b[256];
	if (stat(FIX_FILE, &st))
		return -1;
	*mtime = st.st_mtime;
	FILE *fp = fopen(FIX_FILE, "r");
	if (!fp)
		return -1;
	size_t k = fread(b, 1, sizeof(b) - 1, fp);
	fclose(fp);
	b[k] = 0;
	memset(f, 0, sizeof(*f));
	if (sscanf(b, "t=%llu lat=%lf lon=%lf acc=%lf alt=%lf spd=%lf hdg=%lf n=%u", &f->t, &f->lat, &f->lon, &f->acc,
		   &f->alt, &f->spd, &f->hdg, &f->n) != 8)
		return -1;
	if (f->lat < -90 || f->lat > 90 || f->lon < -180 || f->lon > 180 || (f->lat == 0 && f->lon == 0))
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	int verbose = argc > 1 && !strcmp(argv[1], "-v");
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	@autoreleasepool {
		Class c = NSClassFromString(@"CLSimulationManager");
		if (!c) {
			printf("gnssfeed: no CLSimulationManager class\n");
			return 1;
		}
		mgr = [[c alloc] init];
		SEL ap = sel_registerName("appendSimulatedLocation:");
		if (![mgr respondsToSelector:ap]) {
			printf("gnssfeed: CLSimulationManager has no -appendSimulatedLocation:\n");
			return 1;
		}
		printf("gnssfeed: watching %s\n", FIX_FILE);
		int simulating = 0;
		unsigned last_n = 0, fed = 0;
		time_t last_mtime = 0;
		while (!quit) {
			@autoreleasepool {
				struct fix f;
				time_t mt = 0, now = time(NULL);
				int ok = read_fix(&f, &mt) == 0;
				int fresh = ok && now - mt <= STALE_S && mt <= now + 5;
				if (fresh && (f.n != last_n || mt != last_mtime)) {
					last_n = f.n;
					last_mtime = mt;
					double acc = f.acc > 0 ? f.acc : 30;
					if (acc < 3)
						acc = 3;
					CLLocation *l = [[CLLocation alloc]
					    initWithCoordinate:CLLocationCoordinate2DMake(f.lat, f.lon)
						      altitude:(f.alt > -9000 ? f.alt : 0)
					    horizontalAccuracy:acc
					      verticalAccuracy:(f.alt > -9000 ? acc * 1.5 : -1)
							course:(f.hdg >= 0 ? f.hdg : -1)
							 speed:(f.spd >= 0 ? f.spd : -1)
						     timestamp:[NSDate date]];
					call("clearSimulatedLocations");
					((void (*)(id, SEL, id))objc_msgSend)(mgr, ap, l);
					if (!simulating) {
						call_u8("setLocationDeliveryBehavior:", 0);	/* deliver at once */
						call_u8("setLocationRepeatBehavior:", 1);	/* hold the last position */
						call("startLocationSimulation");
						simulating = 1;
						printf("gnssfeed: modem fixes coming in: feeding locationd\n");
					}
					call("flush");
					fed++;
					if (verbose || fed <= 3 || fed % 300 == 0)
						printf("gnssfeed: fed #%u: %.2f, %.2f +- %.0f m\n", fed, f.lat, f.lon, acc);
				} else if (!fresh && simulating) {
					call("stopLocationSimulation");
					call("clearSimulatedLocations");
					call("flush");
					simulating = 0;
					printf("gnssfeed: no fresh fix for %d s: simulation stopped (Wi-Fi location again)\n", STALE_S);
				}
				[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
			}
		}
		if (simulating) {
			call("stopLocationSimulation");
			call("clearSimulatedLocations");
			call("flush");
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1]];
		}
		printf("gnssfeed: exiting (%u fixes fed)\n", fed);
	}
	return 0;
}
