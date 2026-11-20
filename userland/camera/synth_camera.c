/*
 * A camera that is not there (synth_camera.h): the frames /dev/hakios-camera would give, made up.
 * Part of ISP.mediacapture (userland/camera/README.md).
 */
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hakios.camera.h"
#include "synth_camera.h"

#define WIDTH           1600
#define HEIGHT          1200
#define STRIDE          (WIDTH * 5 / 4)
#define BLACK           64
#define LEVELS          959             /* 1023 less the black level */
#define LINE_NS         26667           /* 1250 lines: 30 frames a second */
#define FRAME_LINES     1250
#define MARGIN          8
#define GAIN_UNITY      256
#define RAD_ONE         4096            /* a scene radiance of 1 (the brightest white of the patterns) */
#define E_REF           960             /* lines x gain at which a radiance of 0.18 sits at the ISP's AE target (15%) */
#define BOX             160

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t *scene[2];              /* per Bayer site: 0 the back camera's, 1 the front one's */
static uint32_t sensor = 1;             /* as the kernel numbers them: 1 the wide (back), 2 the front */
static uint32_t exposure_lines = 800, gain = GAIN_UNITY, frame_lines = FRAME_LINES;
static uint64_t sequence, next_due;
static mach_timebase_info_data_t tb;
static int opens;

/* linear RGB (0..1) of the patterns */
static void
bars(uint32_t x, uint32_t y, float rgb[3])
{
	static const float bar[8][3] = { { .75f, .75f, .75f }, { .75f, .75f, 0 }, { 0, .75f, .75f }, { 0, .75f, 0 },
		{ .75f, 0, .75f }, { .75f, 0, 0 }, { 0, 0, .75f }, { .02f, .02f, .02f } };

	if (y < HEIGHT * 2 / 3) {
		memcpy(rgb, bar[x * 8 / WIDTH], sizeof(bar[0]));
	} else {
		float v = (float)x / (WIDTH - 1);       /* a grey ramp, black to white */

		rgb[0] = rgb[1] = rgb[2] = v * v;       /* (linear: looks even after the ISP's gamma) */
	}
}

static void
chequer(uint32_t x, uint32_t y, float rgb[3])
{
	static const float hue[6][3] = { { .6f, .15f, .1f }, { .55f, .45f, .1f }, { .15f, .5f, .15f }, { .1f, .4f, .5f },
		{ .15f, .15f, .6f }, { .5f, .15f, .5f } };
	uint32_t cx = x * 8 / WIDTH, cy = y * 6 / HEIGHT;

	if ((cx + cy) & 1) {
		rgb[0] = rgb[1] = rgb[2] = .18f;
	} else {
		memcpy(rgb, hue[(cx + cy * 3) % 6], sizeof(hue[0]));
	}
}

static uint16_t *
make_scene(int which)
{
	uint16_t *s = malloc((size_t)WIDTH * HEIGHT * sizeof(*s));

	if (s == NULL) {
		return NULL;
	}
	for (uint32_t y = 0; y < HEIGHT; y++) {
		for (uint32_t x = 0; x < WIDTH; x++) {
			float rgb[3];
			/* RGGB: R at (even, even), B at (odd, odd), G elsewhere */
			int c = (y & 1) == 0 ? ((x & 1) == 0 ? 0 : 1) : ((x & 1) == 0 ? 1 : 2);

			(which == 0 ? bars : chequer)(x, y, rgb);
			s[(size_t)y * WIDTH + x] = (uint16_t)(rgb[c] * RAD_ONE + 0.5f);
		}
	}
	return s;
}

static uint64_t
ns_to_abs(uint64_t ns)
{
	return ns * tb.denom / tb.numer;
}

int
synth_cam_open(void)
{
	pthread_mutex_lock(&lock);
	if (tb.denom == 0) {
		mach_timebase_info(&tb);
	}
	for (int i = 0; i < 2; i++) {
		if (scene[i] == NULL) {
			scene[i] = make_scene(i);
		}
	}
	opens++;
	sequence = 0;
	next_due = 0;
	pthread_mutex_unlock(&lock);
	if (scene[0] == NULL || scene[1] == NULL) {
		errno = ENOMEM;
		return -1;
	}
	return 3;
}

void
synth_cam_close(int handle)
{
	(void)handle;
	pthread_mutex_lock(&lock);
	opens--;
	pthread_mutex_unlock(&lock);
}

static void
header(struct hakios_camera_frame *h)
{
	memset(h, 0, sizeof(*h));
	memcpy(h->magic, HAKIOS_CAMERA_MAGIC, sizeof(h->magic));
	h->header_bytes = sizeof(*h);
	h->width = WIDTH;
	h->height = HEIGHT;
	h->stride = STRIDE;
	h->format = 1;
	h->bayer = 0;
	h->black = BLACK;
	h->sequence = sequence;
	h->exposure_lines = exposure_lines;
	h->line_ns = LINE_NS;
	h->gain = gain;
	h->gain_unity = GAIN_UNITY;
	h->gain_max = GAIN_UNITY * 16;
	h->frame_lines = frame_lines;
	h->frame_lines_min = FRAME_LINES;
	h->frame_lines_max = FRAME_LINES * 4;
	h->margin = MARGIN;
	h->sensor = sensor;
	h->focus = HAKIOS_CAMERA_NO_FOCUS;
}

static void
render(uint8_t *raw)
{
	const uint16_t *s = scene[sensor == 2 ? 1 : 0];
	uint64_t e = (uint64_t)exposure_lines * gain / GAIN_UNITY;
	uint32_t k = (uint32_t)(e * LEVELS * 65536 / ((uint64_t)E_REF * RAD_ONE));
	/* the square: across the middle and back, a 12 pixels a frame */
	uint32_t span = WIDTH - BOX, t = (uint32_t)(sequence * 12 % (2 * span));
	uint32_t bx = t < span ? t : 2 * span - t, by = HEIGHT / 2 - BOX / 2;

	if (k > 0xffffffffu / RAD_ONE) {
		k = 0xffffffffu / RAD_ONE;
	}
	for (uint32_t y = 0; y < HEIGHT; y++) {
		const uint16_t *line = s + (size_t)y * WIDTH;
		uint8_t *out = raw + (size_t)y * STRIDE;
		bool in_rows = y >= by && y < by + BOX;

		for (uint32_t x = 0; x < WIDTH; x += 4, out += 5) {
			uint32_t v[4];

			for (int i = 0; i < 4; i++) {
				uint32_t r = in_rows && x + i >= bx && x + i < bx + BOX ? RAD_ONE : line[x + i];
				uint32_t l = (r * k) >> 16;

				v[i] = BLACK + (l > LEVELS ? LEVELS : l);
			}
			out[0] = (uint8_t)(v[0] >> 2);
			out[1] = (uint8_t)(v[1] >> 2);
			out[2] = (uint8_t)(v[2] >> 2);
			out[3] = (uint8_t)(v[3] >> 2);
			out[4] = (uint8_t)((v[0] & 3) | (v[1] & 3) << 2 | (v[2] & 3) << 4 | (v[3] & 3) << 6);
		}
	}
}

ssize_t
synth_cam_read(int handle, void *buffer, size_t bytes)
{
	struct hakios_camera_frame *h = buffer;
	size_t whole = sizeof(*h) + (size_t)STRIDE * HEIGHT;

	(void)handle;
	if (bytes == sizeof(*h)) {
		pthread_mutex_lock(&lock);
		header(h);
		pthread_mutex_unlock(&lock);
		return (ssize_t)bytes;
	}
	if (bytes < whole) {
		errno = EINVAL;
		return -1;
	}

	/* the frame's time: one frame length after the last (frame_lines lines), or now if it is late */
	uint64_t now = mach_absolute_time(), period = ns_to_abs((uint64_t)frame_lines * LINE_NS);

	if (next_due == 0 || next_due + period < now) {
		next_due = now + period;
	}
	while ((now = mach_absolute_time()) < next_due) {
		uint64_t ns = (next_due - now) * tb.numer / tb.denom;
		struct timespec ts = { (time_t)(ns / 1000000000u), (long)(ns % 1000000000u) };

		nanosleep(&ts, NULL);
	}
	next_due += period;

	pthread_mutex_lock(&lock);
	sequence++;
	render((uint8_t *)buffer + sizeof(*h));
	header(h);
	h->time = mach_absolute_time();
	pthread_mutex_unlock(&lock);
	return (ssize_t)whole;
}

ssize_t
synth_cam_write(int handle, const void *buffer, size_t bytes)
{
	const struct hakios_camera_command *c = buffer;

	(void)handle;
	if (bytes < sizeof(*c)) {
		errno = EINVAL;
		return -1;
	}
	pthread_mutex_lock(&lock);
	switch (c->command) {
	case HAKIOS_CAMERA_EXPOSURE: {
		uint32_t lines = c->a < 1 ? 1 : c->a, fl = c->c;

		if (fl == 0) {
			fl = lines + MARGIN > FRAME_LINES ? lines + MARGIN : FRAME_LINES;
		}
		fl = fl < FRAME_LINES ? FRAME_LINES : fl > FRAME_LINES * 4 ? FRAME_LINES * 4 : fl;
		exposure_lines = lines > fl - MARGIN ? fl - MARGIN : lines;
		frame_lines = fl;
		gain = c->b < GAIN_UNITY ? GAIN_UNITY : c->b > GAIN_UNITY * 16 ? GAIN_UNITY * 16 : c->b;
		break;
	}
	case HAKIOS_CAMERA_SENSOR:
		sensor = c->a;
		sequence = 0;
		break;
	default:
		break;          /* (KEEP, FOCUS: nothing to keep, no lens) */
	}
	pthread_mutex_unlock(&lock);
	return (ssize_t)bytes;
}
