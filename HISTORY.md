# History

## 4.17 (13.10.2026)

* DMA completion by **interrupt** (`gic400.library`) instead of a 1 kHz polling task: about 15 wake-ups per second instead of ~800,
  no `Disable()` around the copy, a 4 Hz watchdog for a lost interrupt.
* IEC958 packing rewritten as a table-driven loop (same words, about a quarter fewer 68k instructions).
* AHI **CPU overload protection**: `ahiac_PreTimer` / `ahiac_PostTimer` around each refill, as `paula.audio` does.
* Two **low latency** modes, `001f0012` (16 bit) and `001f0013` (HiFi 24 bit): ring of 3 x 16 ms instead of 2 x 96 ms. The mode comes
  from the tag list of `AllocAudio` / `GetAttr`; `AHIDB_Bits` and `AHIDB_MaxPlaySamples` follow it.
* Optional **master volume**: AHI's standard output volume (`AHIC_OutputVolume` / `AHIC_OutputVolume_Query`,
  `AHIDB_MinOutputVolume` / `AHIDB_MaxOutputVolume`), the Output volume of a unit in AHI Prefs. Bit exact at unity, 43 ms ramp.
  `HDMI_QUERY_FEATURES` bit 1.
* More counters in `HDMIDiag` (interrupts, wake-ups, longest refill burst, last resync, PreTimer calls, MAI / RAM status at a fault);
  two new faults, IRQ setup failed and interrupt lost.
* New host tests: `ring_test`, `pcm_fast_test`, `gain_test`.
* Build: `devicetree.resource`, `mailbox.resource` and `gic400.library` are git submodules instead of copied headers.
* Requires `gic400.library` 1.3+ in addition to what 4.16 needs.

## 4.16 (13.09.2026; published in this repository on 16.09.2026)

* Stereo output at 48000 Hz through HDMI0, AHI V4 subdriver.
* 16 bit mode `001f0010` and optional HiFi mode `001f0011` (AHI 32 bit mixing, HDMI 24 bit output), both selectable for Music Unit
  and Units 0-3.
* The DMA ring of two 96 ms halves is observed by a task that polls every millisecond.
* Resynchronisation after a delayed observation without resetting HDMI; buffer updates resume after a late observation or a
  temporary lack of PCM data instead of stopping playback.
* Optional EaglePlayer **AHI Amplifier 10.20** patch (unofficial binary patch of the 10.19 module).
* Requires Emu68 1.1 or later with its embedded ROM (`devicetree.resource`, `mailbox.resource`).

Earlier versions are not documented in this repository.
