# Emu68HDMI-AHI-Mixer 0.1

A MUI tool for the **Emu68 HDMI AHI driver** (4.16-dma-irq): it shows what the driver is doing, draws a live scope of the sound
leaving the Amiga, sets the driver's master volume and plays test sounds through it.

Requirements: Emu68 on a Raspberry Pi 4 (BCM2711), AHI, MUI 3.8, AmigaOS 3.1+. The master volume and the full Driver page need
the **4.16-dma-irq** driver; with the original 4.16 driver the Driver page shows fewer values and the volume is greyed out. The Scope
page works with any driver that plays through HDMI.

## Install

Copy `Emu68HDMI-AHI-Mixer` and its icon anywhere. Keep the icon: it carries a **stack of 16384**, which MUI needs to open its
settings window.

## Pages

| Page | What it gives you |
|---|---|
| **Driver** | Driver loaded or not, version, interrupt or polling driver, state, stage, last fault, mixer format, interrupt line, segments and their rate (96 ms or 16 ms ring), resyncs, AHI CPU protection calls, longest refill burst, worst IRQ delay |
| **Scope** | Stereo meters (peak, RMS, hold, clip lamp), oscilloscope, 64-band spectrum, for any program playing through the HDMI driver; refresh speed and master volume on top |
| **Test** | Pick a mode, a sound, a level and a time limit, press a button. Sounds: left, right, both, left/right/both, 20 Hz - 20 kHz sweep, pink noise, 8 voices (loads the mixer). Shows a summary of the run afterwards |

The program starts on the Scope page. Mode, duration, refresh speed and volume are remembered between sessions; the Test level is
not (a test sound always starts at -30 dB).

## Master volume

A slider (-60 to 0 dB) and a Mute button on the Scope page, also in the Audio menu (Mute `M`, Louder `+`, Quieter `-`). The driver
applies the gain while it packs the samples: exact at 0 dB, a 43 ms ramp when it moves. The driver forgets it when it is unloaded;
the Mixer gives it back (within half a second) as long as it is running. AHI's own volumes are not touched.

## Menus, colours, compact mode

- **Project:** About, About MUI, MUI Settings, Quit.
- **Audio:** Mute, Louder, Quieter.
- **Scope:** refresh speed (keys `1` to `6`), colour theme (Phosphor, Amber, Ice, Rack, Mono, Synth), window mode.
- **Compact** shows the meters, the oscilloscope and the spectrum alone; it takes effect at the next start. The menus still set the
  refresh speed and Mute. Theme and mode are kept in `ENVARC:Emu68HDMI-AHI-Mixer.prefs`.

## How the scope works

The program finds the DMA channel that feeds the HDMI audio FIFO (by its destination address), follows its chain of control blocks
to map the ring, and reads the 21 ms of audio just **behind** the DMA read pointer: sound that has already been sent. Nothing is
copied by the driver. Apart from the master volume, the program never writes to the driver, to the audio buffer or to the hardware.

## Refresh speed and CPU

Measured on a Raspberry Pi 4 at 1.8 GHz with music playing (cost of this program only):

| Refresh | CPU of one core |
|---|---:|
| 50 per second | not measured |
| 25 per second | 10.2 % |
| 12 per second (default) | 4.0 % |
| 6 per second | 3.1 % |
| 3 per second | 1.7 % |
| Paused | 0.3 % |

Only the page you look at is refreshed. Pause it when you measure something else.

## Test page notes

The HDMI output must be free: **one AHI client at a time**. The Scope page can watch another program, the Test page cannot play
while one is using HDMI.

## Shell arguments (all optional)

```
Emu68HDMI-AHI-Mixer [PAGE n] [PLAY n] [MODE n] [LEVEL n] [REFRESH n]
  PAGE     0 Driver, 1 Scope, 2 Test                      (default: Scope)
  PLAY     start a test sound: 0 left, 1 right, 2 both, 3 left/right/both, 4 sweep, 5 pink noise, 6 eight voices
  MODE     0 16 bit, 1 HiFi, 2 16 bit low latency, 3 HiFi low latency
  LEVEL    -60 .. -1 dB
  REFRESH  0 50 per second, 1 25, 2 12, 3 6, 4 3, 5 paused
```

MODE, LEVEL and REFRESH given this way are for that run only and are not saved.

Source code is distributed separately.

GPL-3.0-or-later.
