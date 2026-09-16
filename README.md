# Emu68 HDMI driver for AHI — Coded with AI

Driver version **4.16**. Includes the optional EaglePlayer **AHI Amplifier 10.20** engine patched to work with this driver.

HDMI audio for classic Amiga with PiStorm, Raspberry Pi 4 and Emu68.
Tested with Emu68 1.1 and AHI 6.6.

## Installation
Back up the existing driver and mode file. From this package directory:

```text
Copy Devs/AHI/emu68hdmi.audio DEVS:AHI/
Copy Devs/AudioModes/Emu68HDMI DEVS:AudioModes/
```

Restart the Amiga. In Prefs/AHI, select Emu68 HDMI:16 bit stereo 48 kHz
for Music Unit and Units 0-3, then save. Applications with their own
AHI mode selector also need HDMI selected in their settings.
An optional HiFi 24 bit stereo 48 kHz mode is included.

## Bonus: EaglePlayer 2.06
AHI Amplifier 10.20 is an unofficial patch to the original 10.19 module.
It accepts audio modes that support only 48 kHz. Hardware tests confirmed
that EaglePlayer selects the patched amplifier and plays through HDMI.

## Test results and notes
Hardware playback tests passed; no crackling was reported in the latest
tests. This does not establish compatibility with every application.
Output is stereo at 48 kHz, with approximately 192 ms of startup buffering.
One HDMI client can run at a time. Paula audio is not redirected.
Heavy CPU stalls can cause repeated audio while the driver resynchronizes.
For diagnostics, run Tools/HDMIDiag before starting another audio test.
To roll back, restore the backed-up files and restart the Amiga.

Driver license: GPL-3.0-or-later; see COPYING and Source/SOURCES.txt.
Source code is in Source. Bonus patch details are in Source/EaglePlayer.
Original EaglePlayer: https://aminet.net/mus/play/Eagleplayer.lha

## Source and build

See [Source/README.txt](Source/README.txt) for build instructions and tests.
Driver source: [Source/ahi](Source/ahi). EaglePlayer patch: [Source/EaglePlayer](Source/EaglePlayer).

The HDMI driver is licensed under GPL-3.0-or-later; see [COPYING](COPYING).
Third-party notices remain in their respective files. The EaglePlayer bonus is an unofficial binary patch.
# emu68-hdmi-audio
