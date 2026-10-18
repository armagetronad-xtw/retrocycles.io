"""Build a web font from the game's bitmap font atlas.

textures/font.png is a 256x256 grayscale+alpha atlas: 16 columns x 8 rows of 16x32
cells, cell index = character code (ASCII 0x00-0x7F), the glyph in the alpha
channel. This turns every lit pixel into a square contour (runs merged into
rectangles), so the page can set the game's own face as a TrueType/WOFF2 font:
monospace, one pixel = 64 font units on a 24 px em (1536 units); measured from the
atlas, capitals sit on rows 7..22 (cap height 16), the x-height is 12, descenders
reach row 26, the ink spans columns 2..13, so the baseline is row 23.

With the game's second atlas (textures/font_extra.png, the same grid for codes
0x80-0xFF, as rFont.cpp's lowerPart) the font also gets Latin-1 0xA1-0xFF, the accents
the page's translations use (de, fr, es, pt). Polish letters the atlases do
not have (ą ć ę ł ń ś ź ż and capitals) are composed from the ASCII letter and an accent
drawn in the same pixels (the acute as ó's, the dot as i's, an ogonek, the l's stroke).

usage: python atlas2font.py <font.png> <out.ttf> [<out.woff2>] [<font_extra.png>]
"""
import sys
from PIL import Image
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

CELL_W = 16
CELL_H = 32
PX = 64  # font units per atlas pixel
EM = 24 * PX  # 1536: a 24 px line for a 16 px cap
BASELINE_ROW = 23  # rows from here down are descenders
CAP = 16
XH = 12


def cell_pixels(img, code):
    cx, cy = (code % 16) * CELL_W, (code // 16) * CELL_H
    rows = []
    for y in range(CELL_H):
        row = [img.getpixel((cx + x, cy + y)) > 127 for x in range(CELL_W)]
        rows.append(row)
    return rows


def rectangles(rows):
    """greedy merge: horizontal runs, then equal runs stacked vertically"""
    runs = []
    for y, row in enumerate(rows):
        x = 0
        while x < CELL_W:
            if row[x]:
                x0 = x
                while x < CELL_W and row[x]:
                    x += 1
                runs.append([x0, y, x - x0, 1])
            else:
                x += 1
    merged = []
    for r in runs:
        for m in merged:
            if m[0] == r[0] and m[2] == r[2] and m[1] + m[3] == r[1]:
                m[3] += 1
                break
        else:
            merged.append(r)
    return merged


def glyph(rows):
    pen = TTGlyphPen(None)
    for x, y, w, h in rectangles(rows):
        # atlas y grows down; font y grows up from the baseline row
        top = (BASELINE_ROW - y) * PX
        bottom = (BASELINE_ROW - y - h) * PX
        left, right = x * PX, (x + w) * PX
        pen.moveTo((left, bottom))
        pen.lineTo((left, top))
        pen.lineTo((right, top))
        pen.lineTo((right, bottom))
        pen.closePath()
    return pen.glyph()


def ink_cols(rows, y0=0, y1=CELL_H):
    xs = [x for y in range(y0, y1) for x, on in enumerate(rows[y]) if on]
    return (min(xs), max(xs)) if xs else (0, CELL_W - 1)


def compose(base, mark, capital):
    """a Polish letter: the ASCII base and one mark, in the atlas's pixels"""
    rows = [list(r) for r in base]
    left, right = ink_cols(rows)
    cx = (left + right) // 2

    def put(y, x0, x1):
        for x in range(x0, x1 + 1):
            if 0 <= x < CELL_W:
                rows[y][x] = True

    top = 4 if capital else 7  # the accent's first row: over the cap (row 7) or the x-height (row 11)
    if mark == "acute":  # as the atlas draws ó / Á: three steps of two pixels, rising right
        put(top, cx + 1, cx + 2)
        put(top + 1, cx, cx + 1)
        put(top + 2, cx - 1, cx)
    elif mark == "dot":  # as i's dot: three by three
        put(top, cx - 1, cx + 1)
        put(top + 1, cx - 1, cx + 1)
        put(top + 2, cx - 1, cx + 1)
    elif mark == "ogonek":  # a hook under the right end of the last row
        _, r = ink_cols(rows, 21, 23)
        put(23, r - 1, r)
        put(24, r - 2, r - 1)
        put(25, r - 2, r - 1)
        put(26, r - 1, r + 1)
    elif mark == "stroke":  # through the stem, rising right
        sl, sr = ink_cols(rows, 13, 18)
        mid = 15
        put(mid + 1, sl - 2, sl - 1)
        put(mid, sl - 1, sl - 1)
        put(mid - 1, sr + 1, sr + 1)
        put(mid - 2, sr + 1, sr + 2)
    return rows


POLISH = {
    0x0104: ("A", "ogonek"), 0x0105: ("a", "ogonek"),
    0x0106: ("C", "acute"), 0x0107: ("c", "acute"),
    0x0118: ("E", "ogonek"), 0x0119: ("e", "ogonek"),
    0x0141: ("L", "stroke"), 0x0142: ("l", "stroke"),
    0x0143: ("N", "acute"), 0x0144: ("n", "acute"),
    0x015A: ("S", "acute"), 0x015B: ("s", "acute"),
    0x0179: ("Z", "acute"), 0x017A: ("z", "acute"),
    0x017B: ("Z", "dot"), 0x017C: ("z", "dot"),
}


def main(src, out_ttf, out_woff2=None, extra=None):
    img = Image.open(src)
    alpha = img.getchannel("A") if "A" in img.getbands() else img.convert("L")
    names = [".notdef", "space"]
    cmap = {0x20: "space"}
    empty = [[False] * CELL_W] * CELL_H
    glyphs = {".notdef": glyph(empty), "space": glyph(empty)}
    ink_left, ink_right = CELL_W, 0
    cells = {code: cell_pixels(alpha, code) for code in range(0x21, 0x7F)}
    if extra:
        ex = Image.open(extra)
        ex_alpha = ex.getchannel("A") if "A" in ex.getbands() else ex.convert("L")
        for code in range(0xA1, 0x100):
            cells[code] = cell_pixels(ex_alpha, code - 0x80)
        # the atlas's Ñ cell holds the tilde alone: lay it over the N
        cells[0xD1] = [[t or n for t, n in zip(tr, nr)] for tr, nr in zip(cells[0xD1], cells[ord("N")])]
        for code, (base, mark) in POLISH.items():
            cells[code] = compose(cells[ord(base)], mark, base.isupper())
    for code, rows in cells.items():
        if not any(any(r) for r in rows):
            continue
        name = "uni%04X" % code
        names.append(name)
        cmap[code] = name
        glyphs[name] = glyph(rows)
        if code > 0x7E:
            continue
        for r in rows:
            for x, on in enumerate(r):
                if on:
                    ink_left, ink_right = min(ink_left, x), max(ink_right, x + 1)
    # the game draws the atlas as fixed 16 px cells: the advance is the cell and the
    # glyphs keep their place in it (letters sit at columns 2..13)
    advance = CELL_W * PX
    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (advance, 0) for n in names})
    ascent = (CAP + 2) * PX  # one pixel of air over the cap, the 24 px line's share
    descent = -(EM // PX - CAP - 2) * PX
    fb.setupHorizontalHeader(ascent=ascent, descent=descent)
    fb.setupNameTable({"familyName": "Armagetron", "styleName": "Regular", "fullName": "Armagetron Regular", "psName": "Armagetron-Regular"})
    fb.setupOS2(sTypoAscender=ascent, sTypoDescender=descent, usWinAscent=ascent, usWinDescent=-descent, sCapHeight=CAP * PX, sxHeight=XH * PX)
    fb.setupPost(isFixedPitch=1)
    fb.save(out_ttf)
    if out_woff2:
        from fontTools.ttLib import TTFont
        f = TTFont(out_ttf)
        f.flavor = "woff2"
        f.save(out_woff2)
    print("advance", advance, "ink", ink_left, ink_right, "glyphs", len(names))


if __name__ == "__main__":
    main(*sys.argv[1:])
