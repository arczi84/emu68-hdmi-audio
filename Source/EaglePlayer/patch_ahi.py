"""Local compatibility patch for EaglePlayer 2.06's AHI Amplifier 10.19.

The original CheckAmplifier uses AHI_BestAudioIDA with MinMixFreq=22050
and MaxMixFreq=44100, even when a specific output mode was configured.
Allow a 48000-Hz-only device through that preliminary capability check.
Playback already asks AHI for a supported frequency before allocation.
No playback instructions, hooks, buffer sizes or relocations are changed.
"""
from pathlib import Path
import argparse
import hashlib
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('original', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
data = args.original.read_bytes()
expected_sha256 = '163ad51eafb09224069faafa29d55ffdb345a1b18014ad3572ed72aae13cb51d'
if hashlib.sha256(data).hexdigest() != expected_sha256:
    raise SystemExit('Unsupported original binary: expected EaglePlayer 2.06 AHI Amplifier 10.19')
assert len(data) == 40956, 'Unexpected amplifier binary size'
pattern = b''.join(struct.pack('>II', k, v) for k, v in [
    (0x80000068, 1), (0x80000069, 1), (0x8000006f, 1),
    (0x80000070, 22050), (0x80000071, 44100), (0x8000006e, 16)
]) + struct.pack('>I', 0)
offset = 0x28 + 0x7e28
assert data[offset:offset+len(pattern)] == pattern, 'Unexpected capability filter'
assert data.count(pattern) == 1
old = b'$VER: EP_AHI_Amplifier 10.19 (05.07.19)'
new = b'$VER: EP_AHI_Amplifier 10.20 (14.09.26)'
assert data.count(old) == 1 and len(old) == len(new)
assert args.original.resolve() != args.output.resolve(), 'Keep original intact'
out = bytearray(data.replace(old, new))
# Engine manager uses EUS_Revision rather than the DOS $VER string.
assert struct.unpack_from('>HH', data, 0x28+0x4c) == (10, 19)
struct.pack_into('>H', out, 0x28+0x4e, 20)
struct.pack_into('>I', out, offset+3*8+4, 48000)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(out)
print('Local modified build 10.20; not an official EaglePlayer release')
print('Original SHA256:', hashlib.sha256(data).hexdigest())
print('Patched SHA256: ', hashlib.sha256(out).hexdigest())
