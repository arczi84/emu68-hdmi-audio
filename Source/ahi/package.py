from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import shutil
root = Path(__file__).resolve().parents[1]
archive_path = root / 'Emu68HDMI-AHI-4.16.zip'
with ZipFile(archive_path, 'w', ZIP_DEFLATED) as z:
    for src, dest in [
        ('dist/emu68hdmi.audio', 'Devs/AHI/emu68hdmi.audio'),
        ('dist/Emu68HDMI', 'Devs/AudioModes/Emu68HDMI'),
        ('dist/HDMIAHITest', 'HDMIAHITest'),
        ('dist/HDMIUnitTest', 'HDMIUnitTest'),
        ('dist/HDMIDiag', 'HDMIDiag'),
        ('dist/SetHDMIUnit0', 'SetHDMIUnit0'),
        ('ahi/README.txt', 'README.txt'), ('COPYING', 'COPYING')]:
        z.write(root / src, 'Emu68HDMI-AHI/' + dest)
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if path.is_file() and relative.parts[0] not in ('build', 'dist', '.git', '__pycache__') and path.suffix not in ('.zip', '.pyc'):
            z.write(path, Path('Emu68HDMI-AHI/Source') / relative)
print('Packaged ' + archive_path.name)
