# HakiosAudio.driver (iOS audio devices as a HAL plug-in)

`audio_plugin.c`, `audio_plugin.plist`, `hakios.audio.h`: [hakios-apollo](https://github.com/enesuzun2002/hakios-apollo)'s
AudioServerPlugIn (MIT, `LICENSE.hakios-apollo`), taken unchanged from
[ios-apollo](https://github.com/aTechGeekDev/ios-apollo) `userland/audio/` (commit 1f75f38), where it gives the Mi 10T
speakers and Bluetooth audio in iOS 16.7.16.

mediaserverd loads it from `/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver/`. It presents four devices under
the names VirtualAudio knows an iPhone X's built-in audio by (research/audio: the configuration factory's keys):

| UID | What | Here |
|---|---|---|
| `Speaker` | stereo out 48 kHz s16 + stereo in, volume control | speaker route; samples go to `/var/local/pcm.ring`, consumed by `adspd` and sent through the ADSP ASM stream to QUAT_MI2S and the TFA9874 amps; falls back to `/dev/apollo-audio` or `/dev/hakios-audio` if the ring is absent |
| `Hawking` | 1-ch silent input | VirtualAudio's mandatory 'vhaw' companion: without it everything stays on Null_Device |
| `Codec` | 1-ch input with data source `imic` | built-in mic port ('pmbi'); `/dev/hakios-mic` if present, else silence |
| `Actuator` | 1-ch out | Taptic Engine waveforms; `/dev/hakios-haptic` if present, else not felt; strength % in `/var/local/haptic-strength` (default 100), vibrations (>50 ms: ringtones, charger) in `/var/local/haptic-vibration-strength` (default 1, the softest) |

Replaces our kernel IOAudio2 stand-in (xnu 0226-0235/0254): boot with `sweetaudio=0` (no `sweetaudiotp/term/uid`),
or its "Codec" UID collides with the plug-in's.

Build (macOS, Xcode):

    sh userland/build.sh            # builds adspd, tfa, and this driver
    # or only the HAL bundle:
    sh userland/audio/build.sh      # -> userland/out/HakiosAudio.driver/{HakiosAudio,Info.plist}

Speaker playback also needs the Guacamole ADSP firmware in `/var/local/fw/adsp/` and the vendor
`tfa98xx.cnt` amp profile at `/var/local/fw/tfa98xx.cnt`. The root-image builder stages the ADSP
firmware from `research/ios27.2/extras/fw/modem-all/`; it only enables the `com.sweet.adspd` boot
job when the amp container is present, because running the DSP path without the TFA profile cannot
initialize the speakers safely. The profile is now available at
`research/ios27.2/extras/fw/tfa98xx.cnt`.

Install on the phone (root fs is rw):

    mkdir -p /System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver
    # copy HakiosAudio (0755) and Info.plist (0644!) there, then: killall -9 mediaserverd
    # mediaserverd runs as mobile: an Info.plist left 0600 by scp makes it skip the plug-in silently

Check: `oslog -l mediaserverd` shows VirtualAudio "creating a speaker port" for UID Speaker, ports pspk / prcv? /
pmbi, and no AQMEIO "Error -1 getting output port ID".
