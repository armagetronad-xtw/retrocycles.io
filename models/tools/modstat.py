#!/usr/bin/env python3
"""Measures .mod files the way src/render/rModel.cpp reads them.

    python3 models/tools/modstat.py models/cycle_body.mod [more.mod ...]

Prints per file: vertex and triangle counts, the bounding box (x forward, y left,
z up, model units; the game draws the cycle at scale 0.5), the side projection
rModel uses for generated texture coordinates (u from x, v from z), whether any
vertex carries an explicit `vt`, degenerate triangles and, for the winding check,
how many face normals point away from the part's centroid (a closed convex-ish
part should have all of them pointing outward: counter-clockwise faces are the
front faces, rModel renders with GL_CULL_FACE on). Pure python, no modules.
"""
import sys


def load(path):
    verts = {}
    faces = []
    tex = {}
    with open(path) as f:
        for line in f:
            line = line.split('#', 1)[0].split()
            if not line:
                continue
            if line[0] == 'v':
                verts[int(line[1])] = tuple(float(t) for t in line[2:5])
            elif line[0] == 'vt':
                tex[int(line[1])] = tuple(float(t) for t in line[2:4])
            elif line[0] == 'f':
                faces.append(tuple(int(t) for t in line[1:4]))
    return verts, faces, tex


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def stat(path):
    verts, faces, tex = load(path)
    xs = [v[0] for v in verts.values()]
    ys = [v[1] for v in verts.values()]
    zs = [v[2] for v in verts.values()]
    lo = (min(xs), min(ys), min(zs))
    hi = (max(xs), max(ys), max(zs))
    c = tuple((lo[i] + hi[i]) / 2 for i in range(3))
    degenerate = 0
    inward = 0
    for a, b, d in faces:
        A, B, C = verts[a], verts[b], verts[d]
        n = cross(sub(B, A), sub(C, A))
        if dot(n, n) < 1e-12:
            degenerate += 1
            continue
        mid = tuple((A[i] + B[i] + C[i]) / 3 for i in range(3))
        if dot(n, sub(mid, c)) < 0:
            inward += 1
    print(path)
    print("  vertices %d (indices %d..%d), triangles %d, explicit vt %d"
          % (len(verts), min(verts), max(verts), len(faces), len(tex)))
    print("  bounds x %.4f..%.4f  y %.4f..%.4f  z %.4f..%.4f  (size %.4f x %.4f x %.4f)"
          % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
    print("  generated uv: u = (x - %.4f) / %.4f, v = (%.4f - z) / %.4f"
          % (lo[0], hi[0] - lo[0], hi[2], hi[2] - lo[2]))
    print("  degenerate faces %d, faces whose normal points towards the box centre %d" % (degenerate, inward))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        stat(p)
