/*
 * Note SWEET: the capture stack's methods that need Apple's ISP or a Metal/CoreImage we do not have, replaced inside
 * cameracaptured. HakiOS needed a separate library injected into mediaserverd for these (wardevsa/ios-a5xelte herolte
 * gpu/camera/inject.c, MIT, LICENSE.HakiOS); on 27.2 our ISP.mediacapture is itself loaded into cameracaptured by
 * CMCapture, so its constructor makes the replacements. Each is checked by class and selector only; one that is not
 * there is left alone and said so.
 *
 * 2026-11-17 (outputs/all.log): cameracaptured died every ~6 s with SIGTRAP in CoreImage's
 * CIKernelReflection::consolidate, from +[CIKernel kernelWithString:fromMetalLibraryData:] under
 * -[CIImage imageByApplyingFilter:] in a filter outside the shared cache (PortraitFilters.cifilter): CoreImage asks the
 * library's MTLFunction for -returnType and -arguments, which our Metal (NativeAdrenoMetal) does not answer. The
 * portrait filter's prewarm (the first replacement here) is what HakiOS stubbed for the same reason.
 */
#include <objc/message.h>
#include <objc/runtime.h>
#include <objc/NSObjCRuntime.h>
#include <mach-o/dyld.h>
extern const void *_dyld_get_shared_cache_range(size_t *length);     /* (dyld_priv.h) */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>

extern void hakios_camera_say(const char *format, ...) __attribute__((format(printf, 1, 2)));
#define say hakios_camera_say

static void
prewarm_portrait_filter(id self, SEL selector, int version, BOOL styles)
{
	(void)self;
	(void)selector;
	(void)version;
	(void)styles;
	static bool said;
	if (!said) {
		said = true;
		say("hooks: the portrait filter's prewarm skipped (CoreImage reflection our Metal does not give)");
	}
}

static long
video_stabilization_method(id self, SEL selector)
{
	(void)self;
	(void)selector;
	return 0;
}

/* the type the graph acts on (not the method): a VIS pipeline needs Apple's ISP; without this the graph is torn down */
static long
video_stabilization_type(id self, SEL selector)
{
	(void)self;
	(void)selector;
	return 0;
}

/* no capture audio: the session's audio source fails with 'nope' and the session restarts for ever; that one error swallowed */
#define ERROR_NO_SUCH_THING 1852797029  /* 'nope' */
static IMP original_audio_source_start;
static BOOL
audio_source_start(id self, SEL selector, id *error)
{
	BOOL started = ((BOOL (*)(id, SEL, id *))original_audio_source_start)(self, selector, error);
	id made = error != NULL ? *error : NULL;
	static int said;

	long code = made != NULL ? ((long (*)(id, SEL))objc_msgSend)(made, sel_registerName("code")) : 0;

	/*
	 * Note SWEET (TEST 11): on 27.2 the deferred audio start fails with -10868 (kAudioUnitErr_FormatNotSupported, our
	 * HAL plug-in's input), not 'nope', and "graph finished starting with error -10868" stopped the whole session about
	 * a second after the first preview pictures. Any audio start error is swallowed: video runs without sound.
	 */
	if (started) {
		return started;
	}
	if (error != NULL) {
		*error = NULL;
	}
	if (said < 3) {
		said++;
		say("hooks: the session's audio source did not start (%ld%s): the camera runs without it", code,
		    code == ERROR_NO_SUCH_THING ? ", 'nope'" : "");
	}
	return true;
}

/* the silent audio source's generated samples (~820 MB buffers on 16.5) */
static void
audio_generate_samples(id self, SEL selector)
{
	(void)self;
	(void)selector;
	static bool said;
	if (!said) {
		said = true;
		say("hooks: the silent audio source's generated samples skipped");
	}
}

/* a node's output given no sample buffer (the silent audio source's) is not passed on */
static IMP original_emit_sample_buffer;
static void
emit_sample_buffer(id self, SEL selector, void *sample_buffer)
{
	static bool said;

	if (sample_buffer == NULL) {
		if (!said) {
			said = true;
			say("hooks: a node's output was given no sample buffer: not passed on");
		}
		return;
	}
	((void (*)(id, SEL, void *))original_emit_sample_buffer)(self, selector, sample_buffer);
}

/* a photograph's noise reduction is the ISP's and never hands the picture back: passed on as it came */
static void
gnr_render(id self, SEL selector, void *sample_buffer, id input)
{
	(void)selector;
	(void)input;
	id output = ((id (*)(id, SEL))objc_msgSend)(self, sel_registerName("output"));
	if (output != nil) {
		((void (*)(id, SEL, void *))objc_msgSend)(output, sel_registerName("emitSampleBuffer:"), sample_buffer);
	}
}

/* the live filter thumbnails need Metal/CoreImage that is not here: left unprepared (thumbnails stay empty) */
static void
thumbnail_prepare(id self, SEL selector)
{
	(void)self;
	(void)selector;
	static bool said;
	if (!said) {
		said = true;
		say("hooks: the filter thumbnails' node left unprepared");
	}
}

/* a photograph's quality prioritization at most 1 (speed): one frame, no bracket to fuse */
static IMP original_quality_prioritization;
static long
quality_prioritization(id self, SEL selector)
{
	long asked = ((long (*)(id, SEL))original_quality_prioritization)(self, selector);
	return asked > 1 ? 1 : asked;
}


/*
 * 2026-11-17 (TEST 6): the graph build failed -12780 in -[BWMultiStreamCameraSourceNode _updateOutputRequirements] (from
 * -configure:, BWMultiStreamCameraSourceNode.m:1781): an output the node's configuration enables has no stream output
 * here (its *StreamOutputID ivar nil, or the active formats' SupportedOutputs lack it). Our stream has PrimaryScaler,
 * SecondaryScaler and StillImage only: no sensor RAW, no depth. Those outputs are off.
 */
static BOOL
output_off(id self, SEL selector)
{
	(void)self;
	static bool said[8];
	const char *name = sel_getName(selector);
	unsigned h = (unsigned)(strlen(name) % 8);

	if (!said[h]) {
		said[h] = true;
		say("hooks: -[BWMultiStreamCameraSourceNodeConfiguration %s] answered NO", name);
	}
	return NO;
}

/* the same method, wrapped: what the configuration asks for and which output IDs the node found, when it fails */
static IMP original_update_output_requirements;
static int
update_output_requirements(id self, SEL selector)
{
	int r = ((int (*)(id, SEL))original_update_output_requirements)(self, selector);
	static int said;

	if (r != 0 && said++ < 3) {
		static const char *const flags[] = { "previewOutputEnabled", "secondaryPreviewOutputEnabled", "videoCaptureOutputEnabled",
			"secondaryVideoCaptureOutputEnabled", "stillImageOutputEnabled", "forceFirmwareStillImageOutputEnabled",
			"sensorRawStillImageOutputEnabled", "sensorRawOutputEnabled", "depthOutputEnabled", "keypointDescriptorDataOutputEnabled",
			"semanticMasksOutputEnabled", "hueMapOutputEnabled", "secureMetadataOutputEnabled" };
		static const char *const ivars[] = { "_previewStreamOutputID", "_secondaryPreviewStreamOutputID", "_videoCaptureStreamOutputID",
			"_secondaryVideoCaptureStreamOutputID", "_stillImageStreamOutputID", "_stillImageSensorRawStreamOutputID",
			"_sensorRawStreamOutputID", "_hueMapStreamOutputID", "_depthEnginePrimaryOutputID", "_secureVideoDataOutputID" };
		Ivar civ = class_getInstanceVariable(object_getClass(self), "_configuration");
		id config = civ ? object_getIvar(self, civ) : nil;
		char line[700];
		int o = snprintf(line, sizeof(line), "hooks: _updateOutputRequirements -> %d; enabled:", r);

		for (size_t i = 0; config && i < sizeof(flags) / sizeof(flags[0]); i++) {
			SEL sel = sel_registerName(flags[i]);
			if (((BOOL (*)(id, SEL, SEL))objc_msgSend)(config, sel_registerName("respondsToSelector:"), sel) &&
			    ((BOOL (*)(id, SEL))objc_msgSend)(config, sel)) {
				o += snprintf(line + o, sizeof(line) - (size_t)o, " %s", flags[i]);
			}
		}
		say("%s", line);
		o = snprintf(line, sizeof(line), "hooks: output IDs:");
		for (size_t i = 0; i < sizeof(ivars) / sizeof(ivars[0]) && o < (int)sizeof(line) - 80; i++) {
			Ivar iv = class_getInstanceVariable(object_getClass(self), ivars[i]);
			const char *type = iv ? ivar_getTypeEncoding(iv) : NULL;
			if (iv == NULL || type == NULL || type[0] != '@') {
				continue;
			}
			id v = object_getIvar(self, iv);
			const char *text = "nil";
			if (v != nil) {
				id d = ((id (*)(id, SEL))objc_msgSend)(v, sel_registerName("description"));
				text = d ? ((const char *(*)(id, SEL))objc_msgSend)(d, sel_registerName("UTF8String")) : "?";
			}
			o += snprintf(line + o, sizeof(line) - (size_t)o, " %s=%s", ivars[i] + 1, text);
		}
		say("%s", line);
	}
	return r;
}


/*
 * TEST 7: the still pipeline (_cs_stillImagePhotonicEngineSinkPipelineConfiguration, FigCaptureSession.m:10761
 * "sensorRawPixelFormat != 0") takes the sensor RAW pixel format from -[BWFigVideoCaptureDevice sensorRawPixelFormat],
 * which reads the stream's SensorRawOutputBufferPoolRequirements (PixelFormatType); we give no sensor RAW. A RAW
 * format is answered so the graph can be built ('rgg4', 14-bit Bayer RGGB); no RAW buffers will ever come.
 */
static int
sensor_raw_pixel_format(id self, SEL selector)
{
	(void)self;
	(void)selector;
	static bool said;
	if (!said) {
		said = true;
		say("hooks: -[BWFigVideoCaptureDevice sensorRawPixelFormat] answered 'rgg4'");
	}
	return 0x72676734;     /* 'rgg4' */
}


/*
 * TEST 8: the graph built down to the preview sink, then BWNodeConnection "Passthrough outputs are supposed to have a
 * non-nil format ... BWInferenceNode:Segmentation Node + Optical Flow" (BWGraph.m:1395, -16401): the filtered preview
 * pipeline (-[FigCapturePreviewSinkPipeline _appendFilteredPreviewPipeline:...]) adds person segmentation for
 * Photographic Styles (semanticStyleRenderingEnabled / smartStyleRenderingEnabled), whose networks (ANE,
 * /System/Library/ImagingNetworks) are not here. Styles are off.
 */
static BOOL
answer_no(id self, SEL selector)
{
	static int said;
	if (said++ < 12) {
		say("hooks: -[%s %s] answered NO", object_getClassName(self), sel_getName(selector));
	}
	return NO;
}


/*
 * TEST 12 (diagnostic): the preview is solid green while our pictures are sane. What the preview's image queue is
 * given: every 30th buffer's pixel format, size, IOSurface and the bytes at its middle (plane 0 and 1).
 */
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurfaceRef.h>

/*
 * TEST 13: the preview queue's buffers are 420f 828x1792 (portrait) and all zeros: the graph's scale-and-rotate went to
 * the kernel's CPU 2D scaler stand-in, which refuses 420f -> 420f. Until it does, each preview buffer is drawn here from
 * the newest PrimaryScaler picture (camera_device.c): turned a quarter clockwise (landscape sensor to portrait screen),
 * mirrored for the front camera, the middle of the picture cropped to the buffer's shape, nearest neighbour.
 */
extern CVPixelBufferRef sweet_camera_latest_picture(bool *front);

static void
preview_fill(CMSampleBufferRef sbuf)
{
	CVImageBufferRef dst = sbuf != NULL ? CMSampleBufferGetImageBuffer(sbuf) : NULL;
	bool front = false;
	CVPixelBufferRef src;

	if (dst == NULL || CFGetTypeID(dst) != CVPixelBufferGetTypeID() || CVPixelBufferGetPlaneCount(dst) != 2) {
		return;
	}
	OSType f = CVPixelBufferGetPixelFormatType(dst);
	if (f != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange && f != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
		return;
	}
	if ((src = sweet_camera_latest_picture(&front)) == NULL) {
		return;
	}
	if (CVPixelBufferGetPlaneCount(src) == 2 && CVPixelBufferLockBaseAddress(src, kCVPixelBufferLock_ReadOnly) == 0) {
		if (CVPixelBufferLockBaseAddress(dst, 0) == 0) {
			size_t sw = CVPixelBufferGetWidth(src), sh = CVPixelBufferGetHeight(src);
			size_t dw = CVPixelBufferGetWidth(dst), dh = CVPixelBufferGetHeight(dst);
			const uint8_t *sy = CVPixelBufferGetBaseAddressOfPlane(src, 0), *suv = CVPixelBufferGetBaseAddressOfPlane(src, 1);
			size_t sys = CVPixelBufferGetBytesPerRowOfPlane(src, 0), suvs = CVPixelBufferGetBytesPerRowOfPlane(src, 1);
			uint8_t *dy = CVPixelBufferGetBaseAddressOfPlane(dst, 0), *duv = CVPixelBufferGetBaseAddressOfPlane(dst, 1);
			size_t dys = CVPixelBufferGetBytesPerRowOfPlane(dst, 0), duvs = CVPixelBufferGetBytesPerRowOfPlane(dst, 1);

			/* turned: the buffer's height runs along the picture's width, its width along the picture's height */
			double want = (double)dh / dw;          /* picture width per picture height to show */
			size_t cw = sw, ch = sh;
			if ((double)sw / sh > want) {
				cw = (size_t)(sh * want);
			} else {
				ch = (size_t)(sw / want);
			}
			size_t x0 = (sw - cw) / 2, y0 = (sh - ch) / 2;

			if (sy && suv && dy && duv && dw && dh) {
				for (size_t y = 0; y < dh; y++) {
					size_t px = x0 + y * cw / dh;                         /* down the screen: along the picture */
					uint8_t *row = dy + y * dys;
					for (size_t x = 0; x < dw; x++) {
						size_t xx = front ? x : dw - 1 - x;              /* across the screen: up the picture */
						size_t py = y0 + xx * ch / dw;
						row[x] = sy[py * sys + px];
					}
				}
				for (size_t y = 0; y < dh / 2; y++) {
					size_t px = (x0 + (2 * y) * cw / dh) & ~(size_t)1;
					uint8_t *row = duv + y * duvs;
					for (size_t x = 0; x < dw / 2; x++) {
						size_t xx = front ? 2 * x : dw - 2 - 2 * x;
						size_t py = (y0 + xx * ch / dw) / 2;
						const uint8_t *c = suv + py * suvs + px;
						row[2 * x] = c[0];
						row[2 * x + 1] = c[1];
					}
				}
				static bool said;
				if (!said) {
					said = true;
					say("hooks: the preview drawn here: %zux%zu from the %s camera's %zux%zu (crop %zux%zu)", dw, dh,
					    front ? "front" : "back", sw, sh, cw, ch);
				}
			}
			CVPixelBufferUnlockBaseAddress(dst, 0);
		}
		CVPixelBufferUnlockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
	}
	CVPixelBufferRelease(src);
}

/*
 * TEST 26 (preview scaling): what the graph's pixel transfer nodes ("Preview Scaler" and others) are given, every 30th
 * buffer per node class: the picture, its PrimaryCaptureRect attachment and its metadata's ValidBufferRect -- the
 * Preview Scaler logged an input crop rect of NaNs and a 16x16 intermediate pass. And /var/tmp/sweetcam.nofill (if
 * there) turns the preview fill off, to see what the scaler gives by itself.
 */
static IMP original_transfer_render;

static void
rect_text(CFTypeRef v, char *out, size_t size)
{
	snprintf(out, size, "-");
	if (v == NULL) {
		return;
	}
	CFStringRef d = CFCopyDescription(v);
	if (d != NULL) {
		CFStringGetCString(d, out, (CFIndex)size, kCFStringEncodingUTF8);
		CFRelease(d);
	}
	for (char *c = out, *o = out; ; c++) {
		if (*c == '\n' || *c == '\t') {
			*c = ' ';
		}
		if (*c == ' ' && o > out && o[-1] == ' ') {
			continue;
		}
		*o++ = *c;
		if (*c == 0) {
			break;
		}
	}
}

static void
transfer_render(id self, SEL selector, CMSampleBufferRef sbuf, id input)
{
	static unsigned n;

	if (sbuf != NULL && n++ % 30 == 0 && n < 300) {
		CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sbuf);
		CFStringRef *k_primary = dlsym(RTLD_DEFAULT, "kFigCaptureSampleBufferAttachmentKey_PrimaryCaptureRect");
		CFStringRef *k_md = dlsym(RTLD_DEFAULT, "kFigCaptureSampleBufferAttachmentKey_MetadataDictionary");
		CFStringRef *k_valid = dlsym(RTLD_DEFAULT, "kFigCaptureStreamMetadata_ValidBufferRect");
		CFTypeRef primary = k_primary != NULL ? CMGetAttachment(sbuf, *k_primary, NULL) : NULL;
		CFTypeRef md = k_md != NULL ? CMGetAttachment(sbuf, *k_md, NULL) : NULL;
		CFTypeRef valid = md != NULL && CFGetTypeID(md) == CFDictionaryGetTypeID() && k_valid != NULL ?
		    CFDictionaryGetValue(md, *k_valid) : NULL;
		char name[96] = "?", tp[300], tv[300];
		id nm = ((id (*)(id, SEL))objc_msgSend)(self, sel_registerName("name"));
		if (nm != nil) {
			CFStringGetCString((CFStringRef)nm, name, sizeof(name), kCFStringEncodingUTF8);
		}
		rect_text(primary, tp, sizeof(tp));
		rect_text(valid, tv, sizeof(tv));
		OSType f = pb != NULL ? CVPixelBufferGetPixelFormatType(pb) : 0;
		say("hooks: transfer node %s (%s): in %c%c%c%c %zux%zu; PrimaryCaptureRect %s; ValidBufferRect %s",
		    name, object_getClassName(self), (int)(f >> 24), (int)(f >> 16 & 255), (int)(f >> 8 & 255), (int)(f & 255),
		    pb ? CVPixelBufferGetWidth(pb) : 0, pb ? CVPixelBufferGetHeight(pb) : 0, tp, tv);
	}
	((void (*)(id, SEL, CMSampleBufferRef, id))original_transfer_render)(self, selector, sbuf, input);
}

static IMP original_queue_render;
static void
queue_render(id self, SEL selector, CMSampleBufferRef sbuf, id input)
{
	static unsigned n;

	if (access("/var/tmp/sweetcam.nofill", F_OK) != 0) {
		preview_fill(sbuf);
	}
	if (sbuf != NULL && n++ % 30 == 0 && n < 400) {
		CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sbuf);
		if (pb != NULL && CFGetTypeID(pb) == CVPixelBufferGetTypeID()) {
			OSType f = CVPixelBufferGetPixelFormatType(pb);
			IOSurfaceRef surf = CVPixelBufferGetIOSurface(pb);
			char m0[40] = "", m1[40] = "";

			if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) == 0) {
				size_t planes = CVPixelBufferGetPlaneCount(pb);
				for (size_t p = 0; p < (planes ? planes : 1) && p < 2; p++) {
					uint8_t *b = planes ? CVPixelBufferGetBaseAddressOfPlane(pb, p) : CVPixelBufferGetBaseAddress(pb);
					size_t h = planes ? CVPixelBufferGetHeightOfPlane(pb, p) : CVPixelBufferGetHeight(pb);
					size_t r = planes ? CVPixelBufferGetBytesPerRowOfPlane(pb, p) : CVPixelBufferGetBytesPerRow(pb);
					if (b != NULL) {
						uint8_t *q = b + (h / 2) * r + r / 2;
						snprintf(p ? m1 : m0, 40, "%02x %02x %02x %02x %02x %02x", q[0], q[1], q[2], q[3], q[4], q[5]);
					}
				}
				CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
			}
			say("hooks: preview queue %s: %c%c%c%c %zux%zu surface %u; middle [%s] [%s]", object_getClassName(self),
			    (int)(f >> 24), (int)(f >> 16 & 255), (int)(f >> 8 & 255), (int)(f & 255), CVPixelBufferGetWidth(pb),
			    CVPixelBufferGetHeight(pb), surf ? IOSurfaceGetID(surf) : 0, m0, m1);
		}
	}
	((void (*)(id, SEL, CMSampleBufferRef, id))original_queue_render)(self, selector, sbuf, input);
}

/*
 * TEST 18: with the styles answered off, the session still passed the preview's semantic style on a configuration
 * change (captureSession_updateGraphForVideoPreviewSinkConfigurationChanges), and the streaming filter node, not built
 * for styles, threw "... is not configured for semantic style rendering" (an abort in cameracaptured): the change is let go.
 */
static void
change_semantic_style(id self, SEL selector, id style, bool animated)
{
	(void)selector;
	(void)style;
	(void)animated;
	static bool said;
	if (!said) {
		said = true;
		say("hooks: %s: a semantic style change let go (no style rendering here)", object_getClassName(self));
	}
}

static struct replacement {
	const char *class_name, *selector;
	IMP replacement, *original;
	bool instance, done;
} replacements[] = {
	{ "BWCoreImageFilterRenderer", "prewarmPortraitFilterVersion:semanticStyleFilters:", (IMP)prewarm_portrait_filter, NULL, false, false },
	{ "FigVideoCaptureConnectionConfiguration", "videoStabilizationMethod", (IMP)video_stabilization_method, NULL, true, false },
	{ "FigVideoCaptureConnectionConfiguration", "videoStabilizationType", (IMP)video_stabilization_type, NULL, true, false },
	{ "BWAudioSourceNode", "start:", (IMP)audio_source_start, &original_audio_source_start, true, false },
	{ "BWAudioSourceNode", "_generateSamples", (IMP)audio_generate_samples, NULL, true, false },
	{ "BWNodeOutput", "emitSampleBuffer:", (IMP)emit_sample_buffer, &original_emit_sample_buffer, true, false },
	{ "BWGNRNode", "renderSampleBuffer:forInput:", (IMP)gnr_render, NULL, true, false },
	{ "BWMultiFilterThumbnailNode", "prepareForCurrentConfigurationToBecomeLive", (IMP)thumbnail_prepare, NULL, true, false },
	{ "FigCaptureStillImageSettings", "qualityPrioritization", (IMP)quality_prioritization, &original_quality_prioritization, true, false },
	{ "BWMultiStreamCameraSourceNodeConfiguration", "sensorRawStillImageOutputEnabled", (IMP)output_off, NULL, true, false },
	{ "BWMultiStreamCameraSourceNodeConfiguration", "sensorRawOutputEnabled", (IMP)output_off, NULL, true, false },
	{ "BWMultiStreamCameraSourceNodeConfiguration", "depthOutputEnabled", (IMP)output_off, NULL, true, false },
	{ "BWMultiStreamCameraSourceNode", "_updateOutputRequirements", (IMP)update_output_requirements,
	  &original_update_output_requirements, true, false },
	{ "BWFigVideoCaptureDevice", "sensorRawPixelFormat", (IMP)sensor_raw_pixel_format, NULL, true, false },
	{ "BWImageQueueSinkNode", "renderSampleBuffer:forInput:", (IMP)queue_render, &original_queue_render, true, false },
	{ "FigCaptureVideoPreviewSinkConfiguration", "semanticStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCaptureIrisSinkConfiguration", "semanticStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCapturePhotonicEngineSinkPipelineConfiguration", "semanticStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCapturePhotonicEngineSinkPipelineConfiguration", "smartStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCaptureSessionConfiguration", "smartStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCaptureSessionParsedConfiguration", "smartStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "FigCaptureMovieFileSinkPipelineConfiguration", "smartStyleRenderingEnabled", (IMP)answer_no, NULL, true, false },
	{ "BWStreamingFilterNode", "changeToSemanticStyle:animated:", (IMP)change_semantic_style, NULL, true, false },
	{ "BWPixelTransferNode", "renderSampleBuffer:forInput:", (IMP)transfer_render, &original_transfer_render, true, false },
};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void
replace_all(void)
{
	pthread_mutex_lock(&lock);
	for (size_t i = 0; i < sizeof(replacements) / sizeof(replacements[0]); i++) {
		struct replacement *r = &replacements[i];
		Class cls;

		if (r->done || (cls = objc_getClass(r->class_name)) == NULL) {
			continue;
		}
		r->done = true;
		Method m = r->instance ? class_getInstanceMethod(cls, sel_registerName(r->selector)) :
		    class_getClassMethod(cls, sel_registerName(r->selector));
		if (m == NULL) {
			say("hooks: %c[%s %s] not there; left alone", r->instance ? '-' : '+', r->class_name, r->selector);
			continue;
		}
		IMP was = method_setImplementation(m, r->replacement);
		if (r->original) {
			*r->original = was;
		}
		say("hooks: %c[%s %s] replaced", r->instance ? '-' : '+', r->class_name, r->selector);
	}
	pthread_mutex_unlock(&lock);
}


/*
 * TEST 14: the shutter reaches the photonic engine's noise reduction (BWNRFProcessorController) and cameracaptured dies:
 * "-[CMMTLDevice newBufferWithIOSurface:]: unrecognized selector" (CMImaging's device wrapper forwards to the system
 * Metal device, our NativeAdrenoMetal, which lacks this SPI). Added here to that device's class until the Metal driver has
 * it: a no-copy buffer over the surface's whole allocation (its pages are CPU and GPU memory alike), the surface kept alive
 * by the buffer.
 */
extern void *MTLCreateSystemDefaultDevice(void);

static id
new_buffer_with_iosurface(id self, SEL selector, IOSurfaceRef surface)
{
	(void)selector;
	static int said;

	if (surface == NULL) {
		return nil;
	}
	void *base = IOSurfaceGetBaseAddress(surface);
	size_t page = (size_t)getpagesize(), length = (IOSurfaceGetAllocSize(surface) + page - 1) & ~(page - 1);

	if (base == NULL || length == 0 || ((uintptr_t)base & (page - 1)) != 0) {
		say("hooks: newBufferWithIOSurface: surface %u not mappable (base %p, %zu bytes)", IOSurfaceGetID(surface), base, length);
		return nil;
	}
	CFRetain(surface);
	void (^release)(void *, NSUInteger) = ^(void *pointer, NSUInteger bytes) {
		(void)pointer;
		(void)bytes;
		CFRelease(surface);
	};
	id buffer = ((id (*)(id, SEL, void *, NSUInteger, NSUInteger, id))objc_msgSend)(self,
	    sel_registerName("newBufferWithBytesNoCopy:length:options:deallocator:"), base, length, (NSUInteger)0, (id)release);
	if (buffer == nil) {
		CFRelease(surface);
	}
	if (said++ < 3) {
		say("hooks: newBufferWithIOSurface: surface %u, %zu bytes -> %s", IOSurfaceGetID(surface), length, buffer ? "made" : "refused");
	}
	return buffer;
}

static void
metal_device_additions(void)
{
	id device = (id)MTLCreateSystemDefaultDevice();
	Class cls = device ? object_getClass(device) : Nil;
	SEL sel = sel_registerName("newBufferWithIOSurface:");

	if (cls == Nil) {
		say("hooks: no Metal device: newBufferWithIOSurface: not added");
		return;
	}
	if (class_respondsToSelector(cls, sel)) {
		say("hooks: %s has newBufferWithIOSurface: already", class_getName(cls));
		return;
	}
	class_addMethod(cls, sel, (IMP)new_buffer_with_iosurface, "@@:^{__IOSurface=}");
	say("hooks: -[%s newBufferWithIOSurface:] added", class_getName(cls));
}


/*
 * TEST 17: photos fail in the photonic engine's noise reduction (NRFV3 UBProcessorV3 -> ToneMapping -> STF):
 * -[STFLinearAlgebra _allocateMemory:commandBuffer:] asks an sgetrf workspace query for its scratch size and gets 0.0
 * ("workSpaceLengthFloat > 0", -73465). The query is answered only by STF's AMX or SME getrf (optimized_getrf: SME when
 * the commpage says SME2, AMX when its AMX version field, _get_cpu_capabilities bits 27-30, is 1 or more); every Apple
 * GPU-era CPU has one of them, so the plain LAPACK fallback (lapack::reference::sgetrf, which takes no workspace) never
 * writes the size and nothing on Apple hardware ever sees this. The Kryo has neither. The fallback needs no workspace, so
 * the size read there is made 1.0: `ldr s0, [sp, #12]` at STF 0x2c6429e68 (27.2, 24B119) becomes `fmov s0, #1.0`, in this
 * process's copy of the shared cache, only if the instruction there is the one expected.
 */
/*
 * (TEST 17b: two sites read that size: -[STFLinearAlgebra _allocateMemory:commandBuffer:] +0xf8, "sgetrfWorkSpaceLength > 0",
 * and +[STFLinearAlgebra prewarm:nWeights:nPoly:] +0x1b4, "workSpaceLengthFloat > 0" -- the one the photograph failed on;
 * the first try patched only the first.)
 */
static const struct stf_site {
	uint64_t address;               /* unslid, 24B119 */
	uint32_t expected;              /* ldr s0, [sp, #n] */
	const char *what;
} stf_sites[] = {
	{ 0x2c6429e68ull, 0xbd400fe0u, "-[STFLinearAlgebra _allocateMemory:commandBuffer:]" },
	{ 0x2c642a6b4ull, 0xbd4033e0u, "+[STFLinearAlgebra prewarm:nWeights:nPoly:]" },
};
static bool stf_patched;

static void
patch_stf_workspace(void)
{
	size_t length = 0;
	const char *cache = (const char *)_dyld_get_shared_cache_range(&length);
	uintptr_t slide;
	uint32_t *at;
	Dl_info di = { 0 };

	if (stf_patched || cache == NULL) {
		return;
	}
	slide = (uintptr_t)cache - 0x180000000ul;
	at = (uint32_t *)(uintptr_t)(stf_sites[0].address + slide);
	if (!dladdr(at, &di) || di.dli_fname == NULL || strstr(di.dli_fname, "/STF.bundle/STF") == NULL) {
		static bool said;
		if (!said) {
			said = true;
			say("hooks: STF not loaded yet at %p (%s): tried again as images are added", (void *)at,
			    di.dli_fname != NULL ? di.dli_fname : "-");
		}
		return;
	}
	stf_patched = true;
	for (size_t i = 0; i < sizeof(stf_sites) / sizeof(stf_sites[0]); i++) {
		at = (uint32_t *)(uintptr_t)(stf_sites[i].address + slide);
		if (*at != stf_sites[i].expected) {
			say("hooks: STF workspace load in %s not where 24B119 has it (%#x): left alone", stf_sites[i].what, *at);
			continue;
		}
		vm_address_t page = (vm_address_t)at & ~(vm_address_t)(vm_page_size - 1);
		kern_return_t kr = vm_protect(mach_task_self(), page, vm_page_size, false,
		    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
		if (kr != KERN_SUCCESS) {
			say("hooks: STF workspace patch in %s: page not writable (%d)", stf_sites[i].what, kr);
			continue;
		}
		*at = 0x1e2e1000u;     /* fmov s0, #1.0 */
		vm_protect(mach_task_self(), page, vm_page_size, false, VM_PROT_READ | VM_PROT_EXECUTE);
		sys_icache_invalidate(at, 4);
		say("hooks: STF's getrf workspace size made 1 in %s (now %#x; no AMX/SME here: the LAPACK fallback needs none)",
		    stf_sites[i].what, *at);
	}
}

static void
image_added(const struct mach_header *header, intptr_t slide)
{
	(void)header;
	(void)slide;
	replace_all();
	patch_stf_workspace();
}

__attribute__((constructor)) static void
hooks_init(void)
{
	const char *name = getprogname();

	if (name == NULL || strcmp(name, "cameracaptured") != 0) {
		say("hooks: in %s, not cameracaptured: none made", name ? name : "?");
		return;
	}
	replace_all();
	metal_device_additions();
	patch_stf_workspace();
	_dyld_register_func_for_add_image(image_added);
}
