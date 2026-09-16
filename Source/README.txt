Emu68 HDMI driver 4.16 - source code
==================================
The source snapshot corresponds to the supplied emu68hdmi.audio 4.16.
The optional EaglePlayer binary patch and its test are in EaglePlayer/.
See ../README.txt for installation and current hardware test results.

BUILDING
Use a GCC m68k-amigaos cross compiler with AmigaOS headers and libnix.
Put its executables on PATH, then run:

  make

Alternatively, set TOOLCHAIN to your cross compiler's bin directory.
Executables and the audio mode file are written to dist/. The default
copy targets assemble local installation files under build/install/.
COPY_DIR can override that destination. No startup files are modified.

The tree also contains HDMITest, a standalone HDMI diagnostic and tone
generator used during driver development. It is separate from the AHI
subdriver and is not needed to install or use this release.

VALIDATION
Host tests use a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer:

  make test

Native m68k tests require Python amitools[vamos] (tested with amitools
0.8.1 and machine68k 0.3.0):

  make test-ahi test-watchdog test-ahi-buffering test-ahi-pacing test-ahi-refill

Tests cover the AHI ABI, PCM conversion, sample pacing, DMA buffer
publication and recovery after delayed servicing. Emulated tests do not
exercise physical HDMI or DMA. Playback was also tested on Amiga hardware.

CREDITS AND LICENSE
GPL-3.0-or-later; see COPYING. HDMI initialization and IEC958 packing
are adapted from Circle, copyright (C) 2021-2024 R. Stange. DMA reference
code: copyright (C) 2014-2025 R. Stange. See SOURCES.txt for upstream links.
AHI SDK headers are by Martin Blom; their copyright notices are preserved.
The resource interface files originate from Michal Schulz's projects;
upstream references and license notes are recorded in SOURCES.txt.
