from pathlib import Path
import io

from PIL import Image
import cairosvg

root = Path(__file__).resolve().parents[1]
svg = root / "resources" / "logo.svg"
out = root / "resources" / "app.ico"

png = cairosvg.svg2png(url=str(svg), output_width=256, output_height=256)
img = Image.open(io.BytesIO(png)).convert("RGBA")
img.save(out, format="ICO", sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print(out)
