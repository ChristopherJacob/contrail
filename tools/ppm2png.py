#!/usr/bin/env python3
"""Convert PPM golden images to PNG for viewing: tools/.venv/bin/python tools/ppm2png.py in.ppm [out.png] [scale]"""
import sys
from PIL import Image

src = sys.argv[1]
dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 2
img = Image.open(src)
img.resize((img.width * scale, img.height * scale), Image.NEAREST).save(dst)
print(dst)
