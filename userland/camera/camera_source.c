/*
 * Where the frames come from (synth_camera.h): /dev/hakios-camera (our kernel's sweet_camhw.c, the rear main
 * camera), else synth_camera.c's made-up ones; and the made-up ones for a camera the kernel refuses (ENODEV to
 * HAKIOS_CAMERA_SENSOR: the front one), from then on for that handle. Part of ISP.mediacapture (README.md).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "hakios.camera.h"
#include "synth_camera.h"

#define SYNTH_HANDLE    0x40000000      /* handles from here up are synth_camera.c's */
#define MAX_FD          1024

static int synth_of[MAX_FD];            /* a kernel handle gone over to synth_camera.c: its handle + 1 */
int cam_open_errno;                     /* why the last cam_open() is not the kernel's (0: it is) */

int
cam_open(void)
{
	cam_open_errno = EEXIST;
	if (access("/var/tmp/sweetcam.synth", F_OK) != 0) {     /* (that file: the made-up camera even with the kernel's) */
		int fd = open(HAKIOS_CAMERA_DEVICE, O_RDWR);
		if (fd >= 0 && fd < MAX_FD) {
			synth_of[fd] = 0;
			cam_open_errno = 0;
			return fd;
		}
		cam_open_errno = fd < 0 ? errno : EMFILE;
		if (fd >= 0) {
			close(fd);
		}
	}
	int h = synth_cam_open();
	return h < 0 ? h : SYNTH_HANDLE + h;
}

static int
synth_handle(int handle)
{
	if (handle >= SYNTH_HANDLE) {
		return handle - SYNTH_HANDLE;
	}
	return handle >= 0 && handle < MAX_FD && synth_of[handle] ? synth_of[handle] - 1 : -1;
}

ssize_t
cam_read(int handle, void *buffer, size_t bytes)
{
	int s = synth_handle(handle);
	return s >= 0 ? synth_cam_read(s, buffer, bytes) : read(handle, buffer, bytes);
}

ssize_t
cam_write(int handle, const void *buffer, size_t bytes)
{
	int s = synth_handle(handle);
	if (s >= 0) {
		return synth_cam_write(s, buffer, bytes);
	}
	ssize_t n = write(handle, buffer, bytes);
	const struct hakios_camera_command *c = buffer;
	if (n < 0 && errno == ENODEV && bytes >= sizeof(*c) && c->command == HAKIOS_CAMERA_SENSOR) {
		/* not a camera the kernel has: made-up frames for this handle from now on */
		int h = synth_cam_open();
		if (h >= 0 && handle < MAX_FD) {
			synth_of[handle] = h + 1;
			return synth_cam_write(h, buffer, bytes);
		}
	}
	return n;
}

void
cam_close(int handle)
{
	int s = synth_handle(handle);
	if (s >= 0) {
		synth_cam_close(s);
	}
	if (handle < SYNTH_HANDLE) {
		if (handle >= 0 && handle < MAX_FD) {
			synth_of[handle] = 0;
		}
		close(handle);
	}
}
