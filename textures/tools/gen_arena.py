#!/usr/bin/env python3
"""Generates the dark arena texture set:
  textures/floor_tron.png    the floor of FLOOR_STYLE 1
  textures/dir_wall_tron.png the trail wall of WALL_STYLE 1

    python3 textures/tools/gen_arena.py [--out textures] [--size 256]

Pure python (zlib + struct write the PNGs), runs in the aa-dev container. Both
are greyscale: the colour comes from FLOOR_RED/GREEN/BLUE and from the player
colour the wall is drawn with.

## The floor

One grid cell, tiled: eDisplay.cpp's rFLOOR_TEXTURE path draws the infinite
floor plane with this texture scaled by 1/GRID_SIZE in both axes, multiplied by
FLOOR_RED/GREEN/BLUE. The stock floor.png is a flat black cell with a thin
light border, which reads as a wireframe grid on nothing. This one is built to
be looked at up close and from a shallow angle:

  * a **panel** base, not flat black: a value noise field (two octaves, wrapped
    so the tile repeats seamlessly) around a dark mid tone, so the ground has a
    surface for a glow to land on.
  * a **bevel** towards the cell edge - the outer few percent lift slightly,
    the way a floor tile's chamfer catches light.
  * the **grid line** itself on the edge, a narrow core with a wide soft
    shoulder: sharp enough to read as a line, soft enough not to alias into
    dotted rubbish at distance (the game builds mipmaps for it).
  * four short **tick** marks reaching in from the middle of each edge, the
    only ornament: they give the eye something to measure speed against without
    adding a second full grid.

The values stay low (base ~0.13 of full) because the floor is meant to be dark
and the line is meant to be the brightest thing on it.

## The trail wall

gWall.cpp lays the wall texture out with u along the trail (one repeat per
SEGLEN = 2.5 grid units) and v = 0 at the *top* edge of the wall, 1 at the
ground. The stock dir_wall.png is a flat 0.5 grey with a white zigzag, which
reads as painted sheet metal. This one is a light ribbon instead:

  * a bright **crest** in the top few percent - the edge of the ribbon is the
    brightest part of it, and it is also the edge a player reads a wall's
    position from;
  * a **gradient** down from there to a dimmer foot, so the sheet looks lit
    from its own rim rather than uniformly painted;
  * faint **ribs** across it at a regular spacing along u, dim enough not to
    break the block of colour but enough to give the eye a speed reference
    (what the zigzag was for).

Its mean value is close to the stock texture's 0.5, so a wall is about as
bright overall as before - the contrast is redistributed, not added.
"""
import argparse
import math
import os
import struct
import zlib


def write_png(path, width, height, rows, alpha=False):
    """rows: list of bytearrays, one greyscale byte per pixel, or two
    (grey, alpha) with alpha=True"""
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 4 if alpha else 0, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def value_noise(cells, seed):
    """a cells x cells lattice of pseudo random values in 0..1, wrapping"""
    grid = []
    for j in range(cells):
        row = []
        for i in range(cells):
            h = (i * 374761393 + j * 668265263 + seed * 2246822519) & 0xffffffff
            h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
            row.append(((h ^ (h >> 16)) & 0xffff) / 65535.0)
        grid.append(row)
    return grid


def sample(grid, u, v):
    """smoothstep interpolated lookup, u/v in 0..1, wrapping"""
    n = len(grid)
    x, y = u * n, v * n
    i, j = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - i, y - j
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    a = grid[j % n][i % n]
    b = grid[j % n][(i + 1) % n]
    c = grid[(j + 1) % n][i % n]
    d = grid[(j + 1) % n][(i + 1) % n]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


BASE = 0.115        # the panel's mid value
NOISE = 0.055       # peak to peak of the surface mottle
BEVEL = 0.055       # how far in from the edge the chamfer reaches
BEVEL_LIFT = 0.075  # how much the chamfer lifts
LINE_CORE = 0.008   # the grid line's solid half width, in cell fractions
LINE_SOFT = 0.020   # its shoulder
LINE_VALUE = 0.78   # the line itself
TICK_LEN = 0.10     # the tick marks, from the middle of each edge inwards
TICK_HALF = 0.006
TICK_VALUE = 0.34


# The wet variant, FLOOR_STYLE 2 (floor_tron_wet.png): a much darker panel with
# a brighter, slightly wider line, which also survives mipmapping into the
# distance.
WET = dict(BASE=0.050, NOISE=0.050, BEVEL=0.040, BEVEL_LIFT=0.030,
           LINE_CORE=0.010, LINE_SOFT=0.026, LINE_VALUE=1.00,
           TICK_VALUE=0.22)


def build(size, wet=False):
    p = WET if wet else {}
    base = p.get('BASE', BASE)
    noise_amp = p.get('NOISE', NOISE)
    bevel = p.get('BEVEL', BEVEL)
    bevel_lift = p.get('BEVEL_LIFT', BEVEL_LIFT)
    line_core = p.get('LINE_CORE', LINE_CORE)
    line_soft = p.get('LINE_SOFT', LINE_SOFT)
    line_value = p.get('LINE_VALUE', LINE_VALUE)
    tick_value = p.get('TICK_VALUE', TICK_VALUE)
    coarse = value_noise(8, 1)
    fine = value_noise(32, 2)
    rows = []
    for py in range(size):
        v = (py + 0.5) / size
        row = bytearray(size)
        for px in range(size):
            u = (px + 0.5) / size

            # distance to the nearest cell edge, 0 on it, 0.5 in the middle
            du = min(u, 1 - u)
            dv = min(v, 1 - v)
            d = min(du, dv)

            n = 0.62 * sample(coarse, u, v) + 0.38 * sample(fine, u, v)
            value = base + noise_amp * (n - 0.5)

            if d < bevel:
                t = 1 - d / bevel
                value += bevel_lift * t * t

            # the grid line: a core plus a squared falloff shoulder
            if d <= line_core:
                value = line_value
            elif d < line_core + line_soft:
                t = 1 - (d - line_core) / line_soft
                value = max(value, line_value * t * t)

            # ticks: inwards from the middle of each edge
            for along, across in ((u, dv), (v, du)):
                if abs(along - 0.5) <= TICK_HALF and across < TICK_LEN:
                    t = 1 - across / TICK_LEN
                    value = max(value, tick_value * t)

            row[px] = max(0, min(255, int(round(value * 255))))
        rows.append(row)
    return rows


CREST = 0.055       # the top band, in texture height
CREST_VALUE = 0.98
FOOT_VALUE = 0.30   # the wall at the ground
MID_VALUE = 0.58    # just below the crest, before the gradient to the foot
RIB_PERIOD = 8      # ribs per texture repeat (one repeat = SEGLEN grid units)
RIB_HALF = 0.010
RIB_LIFT = 0.13


def build_wall(size):
    rows = []
    for py in range(size):
        v = (py + 0.5) / size
        if v < CREST:
            t = v / CREST
            value = CREST_VALUE + (MID_VALUE - CREST_VALUE) * t * t
        else:
            t = (v - CREST) / (1 - CREST)
            value = MID_VALUE + (FOOT_VALUE - MID_VALUE) * t
        row = bytearray(size)
        for px in range(size):
            u = (px + 0.5) / size
            phase = (u * RIB_PERIOD) % 1.0
            d = min(phase, 1.0 - phase) / RIB_PERIOD
            value_here = value
            if d < RIB_HALF:
                value_here += RIB_LIFT * (1 - d / RIB_HALF)
            row[px] = max(0, min(255, int(round(value_here * 255))))
        rows.append(row)
    return rows


# The glass ribbon, WALL_STYLE 2 (dir_wall_glass.png): grey + alpha, the only
# texture of the set with an alpha channel: emissive and semi transparent, the
# floor grid shows through the trail while the zigzag reads as structure inside
# it rather than a decal on it.
# The alpha keeps both edges solid: the crest is the line a player reads a
# wall's position from and the foot is where it meets the floor, so the
# silhouette a gameplay read needs is as hard as it ever was; only the body
# between them lets the ground through.
GLASS_CREST = 0.04        # the solid top band
GLASS_CREST_VALUE = 1.0
GLASS_BODY_VALUE = 0.86   # the body is bright: it is light, not painted sheet
GLASS_FOOT_VALUE = 0.95   # and lifts again where it meets the floor
GLASS_FOOT = 0.93         # where that lift starts
GLASS_BODY_ALPHA = 0.42
GLASS_FOOT_ALPHA = 0.88
ZIG_PERIOD = 2            # zigzags per texture repeat (SEGLEN = 2.5 units)
ZIG_LOW = 0.18            # the zigzag's travel across the ribbon
ZIG_HIGH = 0.86
ZIG_HALF = 0.055          # its half width, in v
ZIG_VALUE = 1.0
ZIG_ALPHA = 0.55          # how much it closes the ribbon up where it runs


def build_wall_glass(size):
    rows = []
    for py in range(size):
        v = (py + 0.5) / size
        if v < GLASS_CREST:
            t = v / GLASS_CREST
            value = GLASS_CREST_VALUE + (GLASS_BODY_VALUE - GLASS_CREST_VALUE) * t * t
            alpha = 1.0 + (GLASS_BODY_ALPHA - 1.0) * t * t
        elif v > GLASS_FOOT:
            t = (v - GLASS_FOOT) / (1 - GLASS_FOOT)
            value = GLASS_BODY_VALUE + (GLASS_FOOT_VALUE - GLASS_BODY_VALUE) * t
            alpha = GLASS_BODY_ALPHA + (GLASS_FOOT_ALPHA - GLASS_BODY_ALPHA) * t * t
        else:
            value = GLASS_BODY_VALUE
            alpha = GLASS_BODY_ALPHA
        row = bytearray(size * 2)
        for px in range(size):
            u = (px + 0.5) / size
            phase = (u * ZIG_PERIOD) % 1.0
            # a triangle wave across the ribbon: the zigzag of the stock wall,
            # here as the structure inside the glass
            target = ZIG_LOW + (ZIG_HIGH - ZIG_LOW) * (1 - abs(2 * phase - 1))
            d = abs(v - target)
            value_here, alpha_here = value, alpha
            if d < ZIG_HALF:
                t = 1 - d / ZIG_HALF
                value_here = max(value_here, ZIG_VALUE * t + value * (1 - t))
                alpha_here = alpha + (1 - alpha) * ZIG_ALPHA * t * t
            row[px * 2] = max(0, min(255, int(round(value_here * 255))))
            row[px * 2 + 1] = max(0, min(255, int(round(alpha_here * 255))))
        rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='textures')
    ap.add_argument('--size', type=int, default=256)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, rows, alpha in (("floor_tron.png", build(args.size), False),
                              ("floor_tron_wet.png", build(args.size, wet=True), False),
                              ("dir_wall_tron.png", build_wall(args.size), False),
                              ("dir_wall_glass.png", build_wall_glass(args.size), True)):
        path = os.path.join(args.out, name)
        write_png(path, args.size, args.size, rows, alpha)
        print('wrote ' + path)


if __name__ == '__main__':
    main()
