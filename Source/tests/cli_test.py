"""Run the actual Amiga executable under vamos (amitools[vamos]).

This verifies DOS argument handling; no PiStorm hardware is emulated.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
runner = [sys.executable, "-c",
          "from amitools.tools.vamos import main; import sys; sys.exit(main())",
          "-C", "68020"]
cases = [
    ([], "Mode: diagnostic", 20),
    (["tone"], "Mode: TONE", 20),
    (["TONE"], "Mode: TONE", 20),
    (["ToNe"], "Mode: TONE", 20),
    (["DMA"], "Mode: DMA", 20),
    (["dma"], "Mode: DMA", 20),
    (["TONE", "DMA"], "Mode: DMA", 20),
    (["dma", "BVBHZ=75000000"], "Mode: DMA", 20),
    (["STREAM"], "Mode: STREAM", 20),
    (["stream"], "Mode: STREAM", 20),
    (["STREAM", "BVBHZ=75000000"], "Mode: STREAM", 20),
    (["TONE", "DMA", "STREAM"], "Mode: STREAM", 20),
    (["tone", "bvbhz=75000000"], "Mode: TONE", 20),
    (["BVBHZ", "75000000", "TONE"], "Mode: TONE", 20),
    (["HELP"], "Usage: HDMITest", 0),
    (["BVBHZ=0"], "BVBHZ must be", 20),
    (["BVBHZ=-1"], "Argument parsing failed", 20),
    (["BVBHZ=340000001"], "BVBHZ must be", 20),
    (["BVBHZ=abc"], "Argument parsing failed", 20),
    (["typo"], "Argument parsing failed", 20),
]
with tempfile.TemporaryDirectory(prefix="HDMI CLI ") as temporary:
    binary = Path(temporary) / "HDMITest"
    shutil.copyfile(root / "dist/HDMITest", binary)
    for arguments, expected, code in cases:
        result = subprocess.run(runner + [str(binary)] + arguments,
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == code and expected in result.stdout, (
            arguments, result.returncode, result.stdout, result.stderr)
        if expected.startswith("Mode:"):
            assert "Missing devicetree.resource." in result.stdout
print(f"PASS: {len(cases)} native DOS CLI cases, executable path containing spaces.")
