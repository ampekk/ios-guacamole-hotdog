/*
 * synth_camera.c: a camera that is not there, giving frames the way the kernel's /dev/hakios-camera does
 * (hakios.camera.h): a read of the header's size learns the frame's size; a read of the whole gives the next
 * frame (RAW10, RGGB, black at 64), at 30 a second; a write takes the commands (the exposure and gain are
 * applied to the picture as a sensor's would be, so the ISP's exposure loop settles; another sensor changes
 * the pattern). The back camera's pattern is colour bars over a grey ramp, the front one's a chequer of
 * colours; a bright square moves across both, so a frozen preview is plain to see.
 */
#ifndef SYNTH_CAMERA_H
#define SYNTH_CAMERA_H

#include <sys/types.h>

int synth_cam_open(void);
ssize_t synth_cam_read(int handle, void *buffer, size_t bytes);
ssize_t synth_cam_write(int handle, const void *buffer, size_t bytes);
void synth_cam_close(int handle);

/*
 * camera_source.c: what camera_device.c reads its frames through -- the kernel's /dev/hakios-camera when there is
 * one (our kernel's sweet_camhw.c: the rear main camera, sensor 1), else the made-up camera above; a camera the
 * kernel does not have (HAKIOS_CAMERA_SENSOR refused with ENODEV: the front one) goes over to the made-up one.
 */
int cam_open(void);
ssize_t cam_read(int handle, void *buffer, size_t bytes);
ssize_t cam_write(int handle, const void *buffer, size_t bytes);
void cam_close(int handle);
extern int cam_open_errno;       /* why the last cam_open() gave the made-up camera (0: the kernel's) */

#endif
