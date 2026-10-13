Emu68 HDMI driver 4.17 for AHI
======================================
HDMI audio for classic Amiga with PiStorm, Raspberry Pi 4 and Emu68.
Tested with Emu68 1.1 and AHI 6.6.
See README.md for what is new in 4.17 (DMA interrupt, low latency modes, master volume).

INSTALLATION
Back up the existing driver and mode file. From this package directory:

  Copy Devs/AHI/emu68hdmi.audio DEVS:AHI/
  Copy Devs/AudioModes/Emu68HDMI DEVS:AudioModes/

Restart the Amiga (AHI reads its mode database at start-up). In Prefs/AHI,
select Emu68 HDMI:16 bit stereo 48 kHz for Music Unit and Units 0-3, then
save. Applications with their own AHI mode selector also need HDMI selected
in their settings. Optional modes: HiFi 24 bit, and a low latency twin of
each (better for games, less margin against stalls).
Requires AHI 6.x (tested with 6.6) and Emu68 1.1 or later with its embedded ROM, which provides
devicetree.resource, mailbox.resource and gic400.library (1.3 or later,
used by 4.17). Emu68 1.0 and earlier are not compatible.
See HISTORY.md for the version history.

BONUS: EAGLEPLAYER 2.06
AHI Amplifier 10.20 is an unofficial patch to the original 10.19 module.
It accepts audio modes that support only 48 kHz. Hardware tests confirmed
that EaglePlayer selects the patched amplifier and plays through HDMI.

TEST RESULTS AND NOTES
Hardware playback tests passed; no crackling was reported in the latest
tests. This does not establish compatibility with every application.
Output is stereo at 48 kHz, with approximately 192 ms of buffering (about 48 ms in the low latency modes).
One HDMI client can run at a time. Paula audio is not redirected.
Heavy CPU stalls can cause repeated audio while the driver resynchronizes.
For diagnostics, run Tools/HDMIDiag before starting another audio test.
To roll back, restore the backed-up files and restart the Amiga.

Driver license: GPL-3.0-or-later; see COPYING and Source/SOURCES.txt.
Source code is in Source. Bonus patch details are in Source/EaglePlayer.
Original EaglePlayer: https://aminet.net/mus/play/Eagleplayer.lha
