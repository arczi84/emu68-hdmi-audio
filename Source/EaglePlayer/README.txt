EaglePlayer 2.06 - AHI Amplifier 10.20 HDMI compatibility patch
============================================================
This is an unofficial modification of AHI Amplifier 10.19.

Close EaglePlayer and back up "Engines/AHI Amplifier" outside Engines.
Replace it with the supplied module. Restart EaglePlayer, enable Global
in AHI Amplifier and select HDMI at 48000 Hz through Set AHI Mode.
Enable the amplifier in AmplifierManager and give it priority over
other compatible amplifiers. Restore the original module to roll back.

CHANGE
The preliminary CheckAmplifier filter requests MinMixFreq=22050 and
MaxMixFreq=44100. AHI rejects a fixed-48000-Hz device at the first check.
The patch changes MinMixFreq to 48000 and updates the version string and
EUS_Revision to 10.20. Playback instructions, hooks, buffers and relocations
are unchanged. Playback already queries AHI for a supported frequency.

TEST RESULTS
Hardware tests confirmed amplifier selection and HDMI playback.
A native m68k test runs the actual AHI-specific CheckAmplifier tail with
a controlled replacement for AHI_BestAudioIDA, using AHI 6.0 comparisons:
- Fixed 48000 Hz and fixed 44100 Hz: rejected before, accepted after.
- Range 4410-96000 Hz: accepted by both versions.
- Range 8000-28000 Hz: rejected by both versions.
The earlier feature checks and amplifier preferences remain in effect.

REPRODUCING THE PATCH
python3 patch_ahi.py "original/AHI Amplifier" "patched/AHI Amplifier"
Required original SHA256:
163ad51eafb09224069faafa29d55ffdb345a1b18014ad3572ed72aae13cb51d
The script refuses other input binaries and preserves the original.

To run the check from the supplied Source directory, place the original
at build/eagle-ahi-original and the patched module at build/eagle-ahi-patched.
With m68k-amigaos-gcc on PATH and Python amitools[vamos] installed:

  m68k-amigaos-gcc -std=gnu99 -O2 -m68020 -Wall -Wextra -Werror -Iinclude -Ivendor/ahi EaglePlayer/check_test.c EaglePlayer/check_test.S -o build/eagle_check_test
  python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/eagle_check_test"]))'

Original package: https://aminet.net/mus/play/Eagleplayer.lha
Original documentation: Help/english/EP_Engine.guide in that package.
Public 2.04 sources do not contain AHI Amplifier, which was added in 2.05.
This patch modifies the binary module shipped with EaglePlayer 2.06.
