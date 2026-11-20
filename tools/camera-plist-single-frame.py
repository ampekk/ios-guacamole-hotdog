#!/usr/bin/env python3
"""camera-plist-single-frame.py IN OUT: the D73 AVCaptureSession.plist with Deep Fusion and (Smart) HDR off in every
format, so a photograph is one frame (the multi-frame fusion asks for 5 and the camera gives one), and
UsesPacked10BitFirmwareStillImageOutputPixelFormat off (pf20 stills: NRF binds them as MTLPixelFormat 588, which the
Adreno Metal plugin has not got). Prints what it changed."""
import plistlib, sys

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
data = plistlib.loads(raw)
changed = []


def walk(o, path):
    if isinstance(o, dict):
        for k in list(o):
            v = o[k]
            if k in ('DeepFusionSupported', 'HDRSupported', 'UsesPacked10BitFirmwareStillImageOutputPixelFormat') and v is True:
                o[k] = False
                changed.append(path + '/' + k)
            walk(v, path + '/' + str(k))
    elif isinstance(o, list):
        for i, v in enumerate(o):
            walk(v, '%s[%d]' % (path, i))


walk(data, '')
fmt = plistlib.FMT_BINARY if raw.startswith(b'bplist') else plistlib.FMT_XML
open(dst, 'wb').write(plistlib.dumps(data, fmt=fmt))
for c in changed:
    print('off:', c)
print('%d changed; scheme %s' % (len(changed), [d.get('stillImageNoiseReductionAndFusionScheme') for d in data.get('AVCaptureDevices', [])]))
