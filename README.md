# Emu68 HDMI driver for AHI — Coded with AI

Driver version **4.17**. Includes the optional EaglePlayer **AHI Amplifier 10.20** engine patched to work with this driver.

HDMI audio for classic Amiga with PiStorm, Raspberry Pi 4 and Emu68.
Tested with Emu68 1.1 and AHI 6.6.

## What is new in 4.17

4.16 wakes a task 1000 times per second to read the DMA registers and guess which half of the ring is playing. On a PiStorm
every wake-up is expensive, and a game that renders on the same CPU feels it. The cost is the wake-ups, not the mixing.

| Change | Effect |
|---|---|
| DMA completion by **interrupt** (`gic400.library`, one interrupt per ring segment) | no polling loop, ~15 wake-ups per second instead of ~800; no more `Disable()` around the copy |
| IEC958 packing rewritten as a table-driven loop | same words as before, about a quarter fewer 68k instructions for the driver |
| AHI **CPU overload protection** (`ahiac_PreTimer` / `ahiac_PostTimer`, AHI V4) | AHI can throttle the mixer, as with `paula.audio` |
| Two **low latency** modes | see below |
| Mode taken from the tag list (`AllocAudio`, `GetAttr`) | each mode has its own buffer geometry; `AHIDB_Bits` is right even before an audio control exists |
| Optional **master volume**, AHI's standard output volume (`AHIC_OutputVolume`) | a gain applied while the samples are packed: bit exact at unity (no cost), a 43 ms ramp when it moves, so a volume control does not click |
| More counters in `HDMIDiag`; host tests (`ring_test`, `pcm_fast_test`, `gain_test`) | the ring arithmetic is unit-tested, the packer is checked bit for bit against the old `iec958()`, and the gain against exact values |

The HDMI/Circle initialisation, the mailbox use and the process lifetime code are unchanged. The driver stays an AHI **V4**
driver. This release requires AHI 6.x; tested with AHI 6.6. AHI 4.18 compatibility is unverified.

## Audio modes

| Mode ID | Name in AHI Prefs | Mixer | DMA ring | Mix to speaker | Interrupts | Use it for |
|---|---|---|---|---|---|---|
| `001f0010` | 16 bit stereo 48 kHz | AHI 16 bit | 2 x 96 ms | ~100-190 ms | ~10 / s | music, radio, players, default |
| `001f0011` | HiFi 24 bit stereo 48 kHz | AHI 32 bit, interpolating | 2 x 96 ms | ~100-190 ms | ~10 / s | best quality, resampled voices |
| `001f0012` | 16 bit stereo 48 kHz (low latency) | AHI 16 bit | 3 x 16 ms | ~30-50 ms | ~62 / s | games, sound effects |
| `001f0013` | HiFi 24 bit stereo 48 kHz (low latency) | AHI 32 bit, interpolating | 3 x 16 ms | ~30-50 ms | ~62 / s | games that also want HiFi |

* **HiFi** uses AHI's 32-bit mixing routines (linear interpolation when a voice is resampled, no rounding between voices);
  the driver sends the top 24 bits to the HDMI sub-frames.
* **Low latency** shortens the ring, so effects are heard roughly 100 ms earlier. The price is more interrupts and a
  smaller safety margin: a system stall longer than ~32 ms (disk access when a program starts, for example) can cost one
  short glitch, where the long ring tolerates ~96 ms. The first mixer run is slow while Emu68 translates its code, so
  the low latency modes mix the whole ring before the DMA starts.
* These are **not** AHI "realtime" (~10 ms). `AHIDB_Realtime` stays TRUE on all four so that they remain selectable on
  Units 0-3 of AHI Prefs, as in 4.16.
* 48 kHz stereo only, one HDMI client at a time, no recording: unchanged.

## Master volume

The driver implements AHI's standard output volume: `AHIC_OutputVolume` and `AHIC_OutputVolume_Query` in `AHIsub_HardwareControl`
(a `Fixed` value, 0x10000 = unity; `AHIDB_MinOutputVolume` / `AHIDB_MaxOutputVolume` give about -60 dB up to unity). It starts at
unity. AHI Prefs shows it as the **Output volume** of the unit (Mode settings), saves it in `ahi.prefs`, and AHI applies it when a
program opens the mode; any program can also set it with `AHI_ControlAudio(AHIC_OutputVolume)`. `HDMI_QUERY_FEATURES` has bit 1 set
when the driver has it. AHI's own volumes are untouched, and nothing changes for a program that does not use it. The value lives as
long as the driver stays in memory.
[Emu68HDMI-AHI-Mixer](https://github.com/flype44/Emu68HDMI-AHI-Mixer) is a MUI tool with a slider for it.

## Performance

Whole-machine cost of playing audio, read from the Emu68 performance counters over 14 s with an 8-voice AHI load,
Raspberry Pi 4 at 1.8 GHz, compared with the same machine playing nothing. One core is 1800 M cycles/s.

| | 4.16 | 4.17 16 bit | HiFi | 16 bit low latency | HiFi low latency |
|---|---:|---:|---:|---:|---:|
| Extra ARM cycles per second | +329 M | **+33 M** | +39 M | +65 M | +71 M |
| Share of one core | 18.3 % | **1.8 %** | 2.2 % | 3.6 % | 4.0 % |
| Driver wake-ups per second | ~830 | **~15** | ~15 | ~65 | ~65 |
| Resyncs in a 22 s run | 1-3 | **0** | 0-1 | 1 | 1 |

PiStorm3D / MiniGL users reported that the frame rate lost to the audio driver came back, and games benefit clearly from the
low latency modes. Frame rates are not part of the table because they depend on the game; the table is the CPU time given back.

## Installation
Back up the existing driver and mode file. From this package directory:

```text
Copy Devs/AHI/emu68hdmi.audio DEVS:AHI/
Copy Devs/AudioModes/Emu68HDMI DEVS:AudioModes/
```

**Reboot.** AHI reads its mode database at start-up, so the two new modes only appear after a restart, and a driver already
in memory is not replaced by copying a file. In Prefs/AHI, select an Emu68 HDMI mode (16 bit stereo 48 kHz for music) for
Music Unit and Units 0-3, then save. Applications with their own AHI mode selector also need HDMI selected in their settings.
Roll back by restoring the two files you backed up and rebooting.

Requirements (4.16 and 4.17): Raspberry Pi 4 / CM4, an HDMI display on **HDMI0**, AHI 6.x (tested with 6.6), and **Emu68 1.1 or later with
its embedded Emu68 ROM**. The driver does not load anything itself: it opens `devicetree.resource`, `mailbox.resource` and (4.17)
`gic400.library` (version 1.3 or later; `Version gic400.library` must answer 1.3+), which the ROM of Emu68 1.1+ exposes. **Emu68 1.0
and earlier versions are not compatible.**
Check with `Tools/HDMIDiag` after a run: expect `fault=0`, `In-place resyncs=0`, an `IRQ:` line and
`PreTimer ... asked to skip the mixer 0 times`.

## Bonus: EaglePlayer 2.06
AHI Amplifier 10.20 is an unofficial patch to the original 10.19 module.
It accepts audio modes that support only 48 kHz. Hardware tests confirmed
that EaglePlayer selects the patched amplifier and plays through HDMI.

## Test results and notes
On a Raspberry Pi 4 with Emu68 1.1+ under AHI 6.6, a TV on HDMI0: all four modes play a tone and an 8-voice load with no
fault (checked with the counters). Internet radio and MP3 files were listened to, left / right / centre checked, and the master
volume was set from AHI Prefs (Output volume) and from a MUI mixer tool, with internet radio playing. Not tested: AHI 4.18 itself, CM4, long game sessions.
Compatibility with every application has not been established. Please report.

Output is stereo at 48 kHz. One HDMI client can run at a time. Paula audio is not redirected.
Heavy CPU stalls can cause repeated audio while the driver resynchronizes; the low latency modes have less margin.
For diagnostics, run Tools/HDMIDiag before starting another audio test.

Driver license: GPL-3.0-or-later; see COPYING and Source/SOURCES.txt.
Source code is in Source. Bonus patch details are in Source/EaglePlayer.
Original EaglePlayer: https://aminet.net/mus/play/Eagleplayer.lha

## History

See [HISTORY.md](HISTORY.md).

## Source and build

See [Source/README.txt](Source/README.txt) for build instructions and tests.
Driver source: [Source/ahi](Source/ahi). EaglePlayer patch: [Source/EaglePlayer](Source/EaglePlayer).

The driver uses `mailbox.resource` (required at run time, no direct mailbox fallback), `devicetree.resource` and `gic400.library`.
Their sources are git submodules, pinned to the revisions in `Source/SOURCES.txt`:

```text
git clone --recurse-submodules https://github.com/arczi84/emu68-hdmi-audio
cd emu68-hdmi-audio/Source
make
```

With an existing clone: `git submodule update --init` (not `--recursive`: `mailbox.resource` has a `devicetree.resource` of its own, not
needed here). `make` needs the m68k-amigaos GCC toolchain with its `sfdc`, which generates the `devicetree` and `mailbox` stubs from the
SFD files of the submodules into `Source/build/include`; `gic400.library` ships its headers ready made.

The HDMI driver is licensed under GPL-3.0-or-later; see [COPYING](COPYING).
Third-party notices remain in their respective files. The EaglePlayer bonus is an unofficial binary patch.
