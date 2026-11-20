# ISP.mediacapture (the camera, a test pattern for now)

iOS 27.2's capture stack (cameracaptured, CMCapture) makes its camera device with a function from a bundle in
`/System/Library/MediaCapture/`, picked by the camera driver service there is: `AppleH16CamIn` gives
`H16ISP.mediacapture` and `H16ISPCaptureDeviceCreate`, ..., `AppleCamera` gives `ISP.mediacapture` and
`ISPCaptureDeviceCreate`. Our kernel publishes `AppleCamera` (`xnu/ios27/xnu12377-camera.diff`, SweetCamera.cpp;
boot-arg `sweetcamera=0` leaves it out) and this is our `ISP.mediacapture`.

`camera_device.c`, `camera_isp.c`, `camera_isp.h`, `hakios.camera.h`: HakiOS's FigCaptureDevice (MIT,
`LICENSE.HakiOS`), from [wardevsa/ios-a5xelte](https://github.com/wardevsa/ios-a5xelte) `herolte` `gpu/camera/`
(commit d0da131), itself from danstronics/ios-s9plus-exynos. Changes marked "Note SWEET":

- 27.2's classes, as CMCapture's own CMIOExtensionFigCapture{Device,Stream} lay them out: the stream class is
  version 3 with a fourth function (EnqueueReactionEffect, refused here); the device class is used at version 1.
- The back and front cameras' formats by D73/AVCaptureSession.plist (Photo 4224x3024 and Video-30 4224x2640 at
  the back, 4032x3024 and 4032x2528 at the front).
- Frames from the kernel's `/dev/hakios-camera` when it has one (our `sweet_camhw.c`: the rear main camera, IMX586,
  4000x3000 RAW10, sensor 1), through `camera_source.c`; otherwise, and for the front camera (the kernel answers
  ENODEV to HAKIOS_CAMERA_SENSOR 2), and when `/var/tmp/sweetcam.synth` exists, from `synth_camera.c`: RAW10 at
  1600x1200, 30 a second, colour bars (back) or a chequer (front) with a bright square moving across, through the
  same ISP code (exposure, white balance, scaling) as a real sensor's.
- Its log goes to the unified log ("hakios: camera: ..."; `oslog -t 30 > /var/root/all.log`, unfiltered: the process filter
  catches nothing, and cameracaptured's sandbox refuses a log file).
- `capture_hooks.c`: HakiOS's inject.c replacements, made by this bundle's constructor inside cameracaptured (no injection
  needed): the portrait filter's prewarm skipped (CoreImage asks our Metal for function reflection it does not give, and
  cameracaptured died of SIGTRAP), video stabilization off, the audio source's 'nope' swallowed, its sample generation
  skipped, empty sample buffers not passed on, GNR passed through, filter thumbnails unprepared, photo quality at most speed.

Build (macOS, Xcode):

    sh userland/camera/build.sh     # -> userland/out/ISP.mediacapture

Install (root fs is rw), then restart cameracaptured; MobileGestalt must say the phone has cameras.
