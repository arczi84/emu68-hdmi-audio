"""Generate the big-endian AHI mode database (AHI SDK Drivers/Common/MODEFILE.s)."""
from pathlib import Path
import struct

def chunk(name, data):
    return name + struct.pack('>I', len(data)) + data + bytes(len(data) & 1)

# AHI SDK reserves hardware ID 0x1f for experiments; mode 0x10 avoids VOID 1..5.
def mode(mode_id, name, hifi):
    values = [(0x80000064, mode_id), (0x80000067, 1), (0x80000068, 1),
              (0x80000069, 1), (0x8000006a, hifi), (0x8000006c, 0)]
    # Name uses the relocatable tag range, offset from the start of AUDM data.
    values.append((0x8000806d, (len(values) + 1) * 8 + 4))
    tags = b''.join(struct.pack('>II', *pair) for pair in values) + struct.pack('>I', 0)
    return chunk(b'AUDM', tags + name + b'\0')

# Preserve the existing ID and flags for saved prefs and games that require
# a non-HiFi mode. HiFi uses AHI's 32-bit mixer and 24-bit HDMI samples.
payload = (b'AHIM' + chunk(b'AUDN', b'emu68hdmi\0') +
           mode(0x001f0010, b'Emu68 HDMI:16 bit stereo 48 kHz', 0) +
           mode(0x001f0011, b'Emu68 HDMI:HiFi 24 bit stereo 48 kHz', 1) +
           # Low latency ring (3 x 16 ms instead of 2 x 96 ms); see driver.c mode_geom().
           mode(0x001f0012, b'Emu68 HDMI:16 bit stereo 48 kHz (low latency)', 0) +
           mode(0x001f0013, b'Emu68 HDMI:HiFi 24 bit stereo 48 kHz (low latency)', 1))
path = Path('dist/Emu68HDMI')
path.parent.mkdir(exist_ok=True)
path.write_bytes(chunk(b'FORM', payload))
