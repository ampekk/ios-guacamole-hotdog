#!/bin/sh
# Builds ISP.mediacapture (userland/camera/README.md) into userland/out/ISP.mediacapture.
set -e
cd "$(dirname "$0")/.."
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
mkdir -p out
xcrun --sdk iphoneos clang -arch arm64e -miphoneos-version-min=16.0 -isysroot "$SDK" \
	-O2 -Wall -Wno-unused-function -bundle \
	-framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework IOSurface -framework Metal \
	-o out/ISP.mediacapture camera/camera_device.c camera/camera_isp.c camera/synth_camera.c camera/camera_source.c camera/capture_hooks.c -lobjc
codesign -s - -f out/ISP.mediacapture
chmod 0755 out/ISP.mediacapture
echo out/ISP.mediacapture
