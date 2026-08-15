from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
svg = root / "resources" / "logo.svg"
out = root / "resources" / "app.ico"

# GitHub's Windows 2022 runner ships ImageMagick. It can rasterize the SVG and
# write a Windows ICO without a native Cairo dependency.
subprocess.run([
    "magick", str(svg),
    "-background", "none",
    "-define", "icon:auto-resize=256,128,64,48,32,24,16",
    str(out),
], check=True)
print(out)
