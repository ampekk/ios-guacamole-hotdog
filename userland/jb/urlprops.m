/*
 * urlprops: what a process sees of a path's data protection, kernel and Foundation side (wallpaper store
 * debugging, NOTES 2026-11-14). For each PATH: statfs f_flags (MNT_CPROTECT 0x80), getattrlist
 * ATTR_CMN_DATA_PROTECT_FLAGS, fcntl F_GETPROTECTIONCLASS, and -[NSURL resourceValuesForKeys:] for
 * NSURLFileProtectionKey, NSURLIsReadableKey, NSURLVolumeSupportsFileProtectionKey (errors printed).
 *   urlprops [-set] PATH...     -set first sets NSURLFileProtectionNone through Foundation (prints its error)
 * Run as mobile (ssh mobile@...) to see what PosterBoard sees. MIT, as userland/.
 */
#import <Foundation/Foundation.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <unistd.h>

#ifndef F_GETPROTECTIONCLASS
#define F_GETPROTECTIONCLASS 63
#endif
#ifndef ATTR_CMN_DATA_PROTECT_FLAGS
#define ATTR_CMN_DATA_PROTECT_FLAGS 0x40000000
#endif

int
main(int argc, char **argv)
{
	@autoreleasepool {
		int set = 0, i = 1;
		if (argc > 1 && strcmp(argv[1], "-set") == 0) {
			set = 1;
			i = 2;
		}
		printf("uid %d euid %d\n", getuid(), geteuid());
		for (; i < argc; i++) {
			const char *p = argv[i];
			printf("== %s\n", p);
			struct statfs sf;
			if (statfs(p, &sf) == 0) {
				printf("statfs: type %s flags %#x MNT_CPROTECT %s\n", sf.f_fstypename, sf.f_flags,
				    (sf.f_flags & MNT_CPROTECT) ? "yes" : "no");
			} else {
				printf("statfs: %s\n", strerror(errno));
			}
			struct attrlist al = { .bitmapcount = ATTR_BIT_MAP_COUNT, .commonattr = ATTR_CMN_RETURNED_ATTRS |
			    ATTR_CMN_DATA_PROTECT_FLAGS };
			struct { uint32_t len; attribute_set_t ret; uint32_t cls; } __attribute__((packed)) ab;
			memset(&ab, 0, sizeof(ab));
			if (getattrlist(p, &al, &ab, sizeof(ab), 0) == 0) {
				printf("getattrlist: returned common %#x, class %u%s\n", ab.ret.commonattr, ab.cls,
				    (ab.ret.commonattr & ATTR_CMN_DATA_PROTECT_FLAGS) ? "" : " (NOT returned)");
			} else {
				printf("getattrlist: %s\n", strerror(errno));
			}
			int fd = open(p, O_RDONLY);
			if (fd >= 0) {
				int c = fcntl(fd, F_GETPROTECTIONCLASS);
				printf("F_GETPROTECTIONCLASS: %d%s%s\n", c, c < 0 ? " " : "", c < 0 ? strerror(errno) : "");
				close(fd);
			} else {
				printf("open: %s\n", strerror(errno));
			}
			NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:p]];
			if (set) {
				NSError *e = nil;
				BOOL ok = [u setResourceValue:NSURLFileProtectionNone forKey:NSURLFileProtectionKey error:&e];
				printf("set NSURLFileProtectionNone: %s %s\n", ok ? "ok" : "FAILED",
				    e ? e.description.UTF8String : "");
				[u removeAllCachedResourceValues];
			}
			NSError *e = nil;
			NSDictionary *d = [u resourceValuesForKeys:@[NSURLFileProtectionKey, NSURLIsReadableKey,
			    NSURLVolumeSupportsFileProtectionKey] error:&e];
			printf("NSURL: %s%s\n", d.description.UTF8String, e ? e.description.UTF8String : "");
		}
	}
	return 0;
}
