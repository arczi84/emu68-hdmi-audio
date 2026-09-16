from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parent
with ZipFile(root / "HDMITest-0.6.zip", "w", ZIP_DEFLATED) as archive:
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or relative.parts[0] in ("build", ".git") or path.suffix == ".zip":
            continue
        archive.write(path, Path("HDMITest-0.6") / relative)
print("Created HDMITest-0.6.zip (Amiga executable and complete source).")
