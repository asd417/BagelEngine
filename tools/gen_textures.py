"""Generate solid-color PNG textures for use in BagelEngine materials."""
from PIL import Image
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "materials")

def solid(path, r, g, b, size=64):
    img = Image.new("RGB", (size, size), (r, g, b))
    img.save(path)
    print(f"  wrote {path}")

print("Generating purple material textures...")
solid(os.path.join(OUT, "purple_albedo.png"),    110,  20, 180)   # muted purple surface
solid(os.path.join(OUT, "purple_emission.png"),  200,  50, 255)   # bright purple emission
print("Done.")
