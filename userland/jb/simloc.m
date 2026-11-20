/*
 * simloc: feed a position to locationd through its simulation interface (CoreLocation's private CLSimulationManager,
 * what Xcode's "Simulate Location" uses), to test whether that route works on this port before the modem GNSS
 * daemon is written (ios-sweet iOS 27, 2026-11-19, Claude).
 *   simloc LAT LON [ACC_M]   one fix (accuracy default 10 m), kept until `simloc stop`
 *   simloc stop              end the simulation (real/Wi-Fi location again)
 * Needs the com.apple.locationd.simulation entitlement (simloc-ents.plist). Prints which CLSimulationManager methods
 * exist, so a missing one is visible instead of an exception. MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#include <objc/message.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static id mgr;

static BOOL call(const char *name)
{
	SEL s = sel_registerName(name);
	if (![mgr respondsToSelector:s]) {
		printf("simloc: CLSimulationManager has no -%s\n", name);
		return NO;
	}
	((void (*)(id, SEL))objc_msgSend)(mgr, s);
	return YES;
}

static BOOL call_u8(const char *name, uint8_t v)
{
	SEL s = sel_registerName(name);
	if (![mgr respondsToSelector:s]) {
		printf("simloc: CLSimulationManager has no -%s\n", name);
		return NO;
	}
	((void (*)(id, SEL, uint8_t))objc_msgSend)(mgr, s, v);
	return YES;
}

int main(int argc, char **argv)
{
	@autoreleasepool {
		Class c = NSClassFromString(@"CLSimulationManager");
		if (!c) {
			printf("simloc: no CLSimulationManager class\n");
			return 1;
		}
		mgr = [[c alloc] init];
		if (argc == 2 && !strcmp(argv[1], "stop")) {
			call("stopLocationSimulation");
			call("clearSimulatedLocations");
			call("flush");
			printf("simloc: simulation stopped\n");
			return 0;
		}
		if (argc < 3) {
			fprintf(stderr, "usage: simloc LAT LON [ACC_M] | simloc stop\n");
			return 2;
		}
		double lat = atof(argv[1]), lon = atof(argv[2]), acc = argc > 3 ? atof(argv[3]) : 10;
		CLLocation *l = [[CLLocation alloc] initWithCoordinate:CLLocationCoordinate2DMake(lat, lon) altitude:0
		    horizontalAccuracy:acc verticalAccuracy:-1 timestamp:[NSDate date]];
		call("stopLocationSimulation");
		call("clearSimulatedLocations");
		SEL ap = sel_registerName("appendSimulatedLocation:");
		if (![mgr respondsToSelector:ap]) {
			printf("simloc: CLSimulationManager has no -appendSimulatedLocation:\n");
			return 1;
		}
		((void (*)(id, SEL, id))objc_msgSend)(mgr, ap, l);
		call_u8("setLocationDeliveryBehavior:", 0);     /* deliver at once */
		call_u8("setLocationRepeatBehavior:", 1);       /* repeat (hold the last position) */
		call("startLocationSimulation");
		call("flush");
		printf("simloc: simulating %.6f, %.6f (+- %.0f m)\n", lat, lon, acc);
		/* the manager talks to locationd asynchronously: give the messages time to go out */
		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2]];
	}
	return 0;
}
