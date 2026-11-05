# History

## 4.18 (05.11.2026)

* Up to **four AHI clients** play at once: the HDMI setup, DMA ring, interrupt and worker are shared; each client keeps its own mix
  buffer, PCM cursor and Pre/PostTimer pair. The hardware starts with the first client that plays and stops with the last one.
* Clients are summed in a 24 bit accumulator (`pcm_mix`), saturated and packed once (`pcm_mix_finish`); a single client is bit exact
  with 4.17. A client whose hooks fail is muted until its next START instead of stopping the others.
* `HDMI_QUERY_FEATURES` bit 2, `HDMI_QUERY_STATE` bits 12-14 (allocated) and 16-18 (playing); `HDMIDiag` prints them.
* New host test `mix_test` (also run natively in vamos); the ABI test covers two clients, freeing one while the other stays, and the limit.

## 4.17 (13.10.2026)

The 4.17 changes below are the work of flype44 (pull request #1, irq-lowlatency-volume).

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
