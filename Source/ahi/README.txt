emu68hdmi.audio 4.16
===================
AHI HDMI subdriver for PiStorm with Raspberry Pi 4 and Emu68.
Tested with Emu68 1.1 and AHI 6.6; implements the AHI V4 subdriver ABI.
Version 4.16 is the driver version, not the version of AHI.

FEATURES
- Stereo output at 48000 Hz through HDMI0.
- 16-bit mode (ID 001f0010).
- Optional HiFi mode (ID 001f0011): AHI 32-bit mixing, HDMI 24-bit output.
- Both modes selectable for Music Unit and Units 0-3.
- Resynchronization after delayed DMA servicing without resetting HDMI.

Version 4.16 resumes buffer updates after a late observation or temporary
lack of PCM data, instead of stopping playback for that condition alone.
DMA, cache and unsafe-copy errors still stop playback.

Hardware playback tests passed, including EaglePlayer with the supplied
amplifier patch. No crackling was reported in the latest hardware tests.
Compatibility with every game or application has not been established.

The DMA ring has two 96 ms halves and starts with approximately 192 ms
of silence. Heavy CPU stalls can cause repeated audio during recovery.
Only one client can own HDMI at a time; selecting HDMI in several Units
does not enable simultaneous use. Recording and Paula redirection are
not supported. HiFi mode increases mixing cost and must be selected
explicitly. Mode visibility does not imply a 10 ms latency guarantee.

INSTALLATION AND DIAGNOSTICS
See the package README.txt for installation. Both the .audio driver and
the AudioModes file are required. Restart after replacing the driver.
Run HDMIDiag during a problem, before starting another audio application.
It reports the resident driver version and stream/resynchronization state.

Source tests cover the native AHI ABI, signed PCM conversion, IEC958
packing, sample pacing and recovery from delayed buffer servicing.
See ../README.txt for build instructions and ../SOURCES.txt for credits.
