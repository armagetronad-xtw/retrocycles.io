#!/usr/bin/env python3
"""Packs the stadium's rendered textures for the game (art/stadium/README.md):
art/stadium/build/crowd_rgba.png (written by build_stadium.py) -> textures/stadium/crowd.png,
a 256 colour palette PNG with alpha (about a ninth of the RGBA file; the game's PNG
loaders expand palettes to RGBA). Needs Pillow:  python3 art/stadium/pack_textures.py [repo]"""
import os
import sys

from PIL import Image

root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
src = os.path.join(root, "art", "stadium", "build", "crowd_rgba.png")
dst = os.path.join(root, "textures", "stadium", "crowd.png")
os.makedirs(os.path.dirname(dst), exist_ok=True)
image = Image.open(src).convert("RGBA")
packed = image.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
packed.save(dst, optimize=True)
print("STADIUM: packed %s -> %s (%d -> %d bytes)" % (src, dst, os.path.getsize(src), os.path.getsize(dst)))
