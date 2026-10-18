# Builds the stadium's pieces in Blender, bakes their lighting and exports them for
# the game (art/stadium/README.md). Run headless:
#   blender -b --factory-startup --python art/stadium/build_stadium.py -- <repo root>
# Writes models/stadium/{stand,flood,board}.stm, textures/stadium/crowd.png and saves
# art/stadium/stadium.blend + art/stadium/crowd.blend for hand editing.
#
# Space: x along the stand (a module is LENGTH long), y outwards from the arena's rim
# (the game's "d"), z up; one unit is one bowl unit (the game scales it to the arena).

import bmesh
import bpy
import math
import os
import random
import struct
import sys

import numpy as np

ROOT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.getcwd()
ART = os.path.join(ROOT, "art", "stadium")
OUT_MODELS = os.path.join(ROOT, "models", "stadium")
OUT_TEXTURES = os.path.join(ROOT, "textures", "stadium")
SAMPLES = int(os.environ.get("STADIUM_SAMPLES", "48"))

LENGTH = 24.0          # one module along x
NX = 6                 # subdivisions along x (the game bends modules round the corners)
AISLE = 1.6            # stairway at the start of every module
CROWD_STRIPS = 8       # crowd atlas rows
CROWD_W, CROWD_H = 1024, 64
CARD_H = 1.4           # a crowd card's height (one seated row)

random.seed(7)

# ------------------------------------------------------------------------------------
# scene helpers
# ------------------------------------------------------------------------------------


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, kind="structure", texture="", glow=0.0, emit=None):
    """kind: structure (baked, opaque), crowd (baked, alpha tested, textured),
    glow (additive, not baked)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nodes = m.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    if kind == "glow":
        bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1)
        bsdf.inputs["Emission Color"].default_value = (*(emit or color), 1)
        bsdf.inputs["Emission Strength"].default_value = 3.0
    else:
        # lighting only: the bake sees white, the export multiplies the albedo in
        bsdf.inputs["Base Color"].default_value = (0.8, 0.8, 0.8, 1)
        bsdf.inputs["Roughness"].default_value = 0.9
    m["albedo"] = list(color)
    m["kind"] = kind
    m["texture"] = texture
    m["glow"] = glow
    return m


class Builder:
    """quads into one bmesh, with a material slot, uvs and the x subdivision"""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.mats = []

    def slot(self, mat):
        if mat not in self.mats:
            self.mats.append(mat)
        return self.mats.index(mat)

    def quad(self, a, b, c, d, mat, uvs=((0, 0), (1, 0), (1, 1), (0, 1))):
        vs = [self.bm.verts.new(p) for p in (a, b, c, d)]
        f = self.bm.faces.new(vs)
        f.material_index = self.slot(mat)
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        return f

    def strip(self, x0, x1, p0, p1, mat, n=None, uv=None):
        """a quad between two (y, z) points swept from x0 to x1 in n pieces; uv(x, t)"""
        n = n or max(1, round((x1 - x0) / LENGTH * NX))
        for i in range(n):
            xa = x0 + (x1 - x0) * i / n
            xb = x0 + (x1 - x0) * (i + 1) / n
            uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
            if uv:
                uvs = (uv(xa, 0), uv(xb, 0), uv(xb, 1), uv(xa, 1))
            self.quad((xa, p0[0], p0[1]), (xb, p0[0], p0[1]), (xb, p1[0], p1[1]), (xa, p1[0], p1[1]), mat, uvs)

    def box(self, center, size, mat, rot=None):
        """an axis aligned box (optionally rotated about x by rot radians)"""
        cx, cy, cz = center
        sx, sy, sz = (s / 2 for s in size)
        corners = [(x, y, z) for x in (-sx, sx) for y in (-sy, sy) for z in (-sz, sz)]

        def tr(p):
            x, y, z = p
            if rot:
                c, s = math.cos(rot), math.sin(rot)
                y, z = y * c - z * s, y * s + z * c
            return (cx + x, cy + y, cz + z)

        c = [tr(p) for p in corners]
        # index = xi*4 + yi*2 + zi
        faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
        for f in faces:
            self.quad(c[f[0]], c[f[1]], c[f[2]], c[f[3]], mat)

    def beam(self, p0, p1, w, mat):
        """a square beam from p0 to p1"""
        dx, dy, dz = (p1[i] - p0[i] for i in range(3))
        length = math.sqrt(dx * dx + dy * dy + dz * dz)
        ux, uy, uz = dx / length, dy / length, dz / length
        # two perpendiculars
        ax = (0, 0, 1) if abs(uz) < 0.9 else (1, 0, 0)
        px = (uy * ax[2] - uz * ax[1], uz * ax[0] - ux * ax[2], ux * ax[1] - uy * ax[0])
        pl = math.sqrt(sum(v * v for v in px))
        px = tuple(v / pl * w / 2 for v in px)
        py = (uy * px[2] - uz * px[1], uz * px[0] - ux * px[2], ux * px[1] - uy * px[0])
        pl = math.sqrt(sum(v * v for v in py))
        py = tuple(v / pl * w / 2 for v in py)

        def at(p, sx, sy):
            return tuple(p[i] + px[i] * sx + py[i] * sy for i in range(3))

        ring = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
        for k in range(4):
            s0, s1 = ring[k], ring[(k + 1) % 4]
            self.quad(at(p0, *s0), at(p0, *s1), at(p1, *s1), at(p1, *s0), mat)

    def object(self):
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.mats:
            me.materials.append(m)
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


# ------------------------------------------------------------------------------------
# the stand module
# ------------------------------------------------------------------------------------

M = {}


def materials():
    M["concrete"] = material("concrete", (0.15, 0.16, 0.19))
    M["concrete_dark"] = material("concrete_dark", (0.085, 0.09, 0.105))
    M["seat"] = material("seat", (0.025, 0.032, 0.065))
    M["aisle"] = material("aisle", (0.11, 0.115, 0.13))
    M["metal"] = material("metal", (0.17, 0.18, 0.21))
    M["roof"] = material("roof", (0.10, 0.105, 0.12))
    M["tunnel"] = material("tunnel", (0.02, 0.02, 0.025))
    M["crowd"] = material("crowd", (1.0, 1.0, 1.0), "crowd", "textures/stadium/crowd.png")
    M["led_cyan"] = material("led_cyan", (0.30, 0.85, 1.0), "glow", "@dash", 0.8)
    M["led_white"] = material("led_white", (0.75, 0.80, 0.90), "glow", "@dash", 0.6)
    M["warm"] = material("warm", (1.0, 0.62, 0.30), "glow", "", 0.0)  # too small to bloom
    M["step"] = material("step", (1.0, 0.55, 0.22), "glow", "", 0.0)
    M["flood_panel"] = material("flood_panel", (0.95, 0.97, 1.0), "glow", "@panel", 1.0)
    M["board_screen"] = material("board_screen", (1.0, 1.0, 1.0), "glow", "@board", 0.7)


LOWER = dict(d0=8.0, z0=3.0, d1=34.0, z1=19.0, rows=14)
UPPER = dict(d0=37.0, z0=23.0, d1=66.0, z1=48.0, rows=16)
ROOF_FRONT, ROOF_BACK, ROOF_Z = 40.0, 72.0, 66.0
TUNNEL_X0, TUNNEL_X1 = 10.0, 14.0


def tier(b, t, variants):
    rows = t["rows"]
    rd = (t["d1"] - t["d0"]) / rows
    rh = (t["z1"] - t["z0"]) / rows
    for i in range(rows):
        d = t["d0"] + i * rd
        zb = t["z0"] + i * rh
        zt = zb + rh
        # riser and tread; the stairway part lighter
        b.strip(0, AISLE, (d, zb), (d, zt), M["aisle"], n=1)
        b.strip(0, AISLE, (d, zt), (d + rd, zt), M["aisle"], n=1)
        b.strip(AISLE, LENGTH, (d, zb), (d, zt), M["concrete_dark"])
        b.strip(AISLE, LENGTH, (d, zt), (d + rd, zt), M["seat"])
        # the stairway's step light at the riser's top
        b.quad((AISLE * 0.35, d - 0.02, zt - 0.18), (AISLE * 0.65, d - 0.02, zt - 0.18),
               (AISLE * 0.65, d - 0.02, zt - 0.05), (AISLE * 0.35, d - 0.02, zt - 0.05), M["step"])
        # the row's spectators: one card, a random strip of the atlas
        v = random.randrange(variants)
        v0 = (v + 1) / variants  # the card's foot (v = 0 is the image's top in the game)
        v1 = v / variants
        cd = d + rd * 0.45
        span = LENGTH - AISLE - 0.2

        def uv(x, t, v0=v0, v1=v1):
            return ((x - AISLE - 0.1) / span, v0 + (v1 - v0) * t)

        b.strip(AISLE + 0.1, LENGTH - 0.1, (cd, zt), (cd + 0.25, zt + CARD_H), M["crowd"], uv=uv)
    return rd, rh


def stand():
    b = Builder("stand")
    # podium wall, its LED ribbon, the walkway behind
    b.strip(0, LENGTH, (5.0, 0.0), (5.0, 3.0), M["concrete_dark"])
    b.strip(0, LENGTH, (4.95, 1.35), (4.95, 2.35), M["led_cyan"],
            uv=lambda x, t: (x / 1.7, t))
    b.strip(0, LENGTH, (5.0, 3.0), (8.0, 3.0), M["concrete"])
    tier(b, LOWER, CROWD_STRIPS)
    # the concourse with a vomitory in the middle of the module
    z0, z1, zt = LOWER["z1"], 23.0, 22.0
    d = LOWER["d1"]
    b.strip(0, TUNNEL_X0, (d, z0), (d, z1), M["concrete"])
    b.strip(TUNNEL_X1, LENGTH, (d, z0), (d, z1), M["concrete"])
    b.strip(TUNNEL_X0, TUNNEL_X1, (d, zt), (d, z1), M["concrete"], n=1)
    b.strip(TUNNEL_X0, TUNNEL_X0, (d, z0), (d, zt), M["concrete"], n=1)
    # the tunnel: floor, sides, ceiling, a warm lit back
    b.quad((TUNNEL_X0, d, z0), (TUNNEL_X1, d, z0), (TUNNEL_X1, d + 5, z0), (TUNNEL_X0, d + 5, z0), M["tunnel"])
    b.quad((TUNNEL_X0, d, zt), (TUNNEL_X0, d + 5, zt), (TUNNEL_X1, d + 5, zt), (TUNNEL_X1, d, zt), M["tunnel"])
    b.quad((TUNNEL_X0, d, z0), (TUNNEL_X0, d + 5, z0), (TUNNEL_X0, d + 5, zt), (TUNNEL_X0, d, zt), M["tunnel"])
    b.quad((TUNNEL_X1, d, z0), (TUNNEL_X1, d, zt), (TUNNEL_X1, d + 5, zt), (TUNNEL_X1, d + 5, z0), M["tunnel"])
    b.quad((TUNNEL_X0, d + 5, z0), (TUNNEL_X1, d + 5, z0), (TUNNEL_X1, d + 5, zt), (TUNNEL_X0, d + 5, zt), M["warm"])
    # concourse lamps either side of the tunnel
    for x in (TUNNEL_X0 - 4, TUNNEL_X1 + 4):
        b.quad((x - 0.5, d - 0.03, 21.6), (x + 0.5, d - 0.03, 21.6), (x + 0.5, d - 0.03, 22.2),
               (x - 0.5, d - 0.03, 22.2), M["warm"])
    b.strip(0, LENGTH, (d, z1), (UPPER["d0"], z1), M["concrete"])
    tier(b, UPPER, CROWD_STRIPS)
    # fascia with its LED ribbon, the top walkway, the back wall
    d, z = UPPER["d1"], UPPER["z1"]
    b.strip(0, LENGTH, (d, z), (d, z + 4), M["concrete_dark"])
    b.strip(0, LENGTH, (d - 0.05, z + 1.4), (d - 0.05, z + 2.3), M["led_white"],
            uv=lambda x, t: (x / 2.4, t))
    b.strip(0, LENGTH, (d, z + 4), (ROOF_BACK, z + 4), M["concrete"])
    b.strip(0, LENGTH, (ROOF_BACK, z + 4), (ROOF_BACK, ROOF_Z + 1), M["concrete_dark"])
    b.strip(0, LENGTH, (ROOF_BACK, 0.0), (ROOF_BACK, z + 4), M["concrete_dark"])
    # the roof: underside, top, front edge with a white LED line
    b.strip(0, LENGTH, (ROOF_FRONT, ROOF_Z), (ROOF_BACK, ROOF_Z + 1), M["roof"])
    b.strip(0, LENGTH, (ROOF_FRONT, ROOF_Z + 1.2), (ROOF_BACK, ROOF_Z + 2.2), M["roof"])
    b.strip(0, LENGTH, (ROOF_FRONT, ROOF_Z), (ROOF_FRONT, ROOF_Z + 1.2), M["metal"])
    b.strip(0, LENGTH, (ROOF_FRONT - 0.05, ROOF_Z + 0.35), (ROOF_FRONT - 0.05, ROOF_Z + 0.55), M["led_white"],
            uv=lambda x, t: (x / 1.2, t))
    # two cantilever trusses under the roof and a column at the back
    for x in (6.0, 18.0):
        top_back, top_front = (x, ROOF_BACK - 1, ROOF_Z), (x, ROOF_FRONT + 1, ROOF_Z)
        bottom_back = (x, ROOF_BACK - 1, ROOF_Z - 7)
        b.beam(top_back, top_front, 0.5, M["metal"])
        b.beam(bottom_back, top_front, 0.5, M["metal"])
        for k in range(1, 6):
            f = k / 6
            p_top = (x, ROOF_BACK - 1 + (ROOF_FRONT + 1 - ROOF_BACK + 1) * f, ROOF_Z)
            f2 = (k - 0.5) / 6
            p_bot = (x, ROOF_BACK - 1 + (ROOF_FRONT + 1 - ROOF_BACK + 1) * f2, ROOF_Z - 7 * (1 - f2))
            b.beam(p_top, p_bot, 0.3, M["metal"])
        b.beam((x, ROOF_BACK - 0.6, UPPER["z1"] + 4), bottom_back, 0.9, M["metal"])
    return b.object()


def flood():
    """a floodlight bank: a housing and a panel of lamps, facing -y, tilted down
    (the game aims the whole bank; origin at its mounting point)"""
    b = Builder("flood")
    tilt = math.radians(-35)
    b.box((0, 0.6, -1.4), (7.0, 1.2, 3.0), M["metal"], rot=tilt)
    c, s = math.cos(tilt), math.sin(tilt)

    def tr(x, y, z):
        return (x, y * c - z * s, y * s + z * c)

    y, z0, z1 = -0.02, -2.6, -0.2
    b.quad(tr(-3.3, y, z0 + 0.3), tr(3.3, y, z0 + 0.3), tr(3.3, y, z1 - 0.3), tr(-3.3, y, z1 - 0.3),
           M["flood_panel"], ((0, 0), (1, 0), (1, 1), (0, 1)))
    b.beam((-2.5, 1.0, 0.6), (-2.5, 1.0, -0.4), 0.3, M["metal"])
    b.beam((2.5, 1.0, 0.6), (2.5, 1.0, -0.4), 0.3, M["metal"])
    return b.object()


def board():
    """a scoreboard housing, 1 wide and 1 high (the game scales it), the screen
    facing -y at y=0"""
    b = Builder("board")
    b.box((0, 0.06, 0), (1.06, 0.1, 1.18), M["metal"])
    b.quad((-0.5, -0.002, -0.5), (0.5, -0.002, -0.5), (0.5, -0.002, 0.5), (-0.5, -0.002, 0.5),
           M["board_screen"], ((0, 0), (1, 0), (1, 1), (0, 1)))
    for x in (-0.35, 0.35):
        b.beam((x, 0.08, 0.59), (x, 0.08, 1.6), 0.02, M["metal"])
    return b.object()


# ------------------------------------------------------------------------------------
# lighting bake
# ------------------------------------------------------------------------------------


def bake_scene(stand_ob):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = SAMPLES
    world = bpy.data.worlds.new("night")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.012, 0.018, 0.035, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = world

    # neighbours for occlusion at the module's ends
    for dx in (-LENGTH, LENGTH):
        n = bpy.data.objects.new("stand_neighbour", stand_ob.data)
        n.location.x = dx
        scene.collection.objects.link(n)
    # the arena floor (it bounces the floodlights into the stands)
    fb = Builder("floor")
    fb.quad((-60, -160, 0), (84, -160, 0), (84, 5, 0), (-60, 5, 0), material("floor", (0.1, 0.1, 0.12)))
    fb.object()

    def light(name, kind, loc, rot, energy, color, size=None, spot=None):
        data = bpy.data.lights.new(name, kind)
        data.energy = energy
        data.color = color
        if size is not None:
            data.size = size
            if kind == "AREA":
                data.shape = "RECTANGLE"
                data.size_y = size * 0.4
        if spot:
            data.spot_size = spot
            data.spot_blend = 0.6
        ob = bpy.data.objects.new(name, data)
        ob.location = loc
        ob.rotation_euler = rot
        scene.collection.objects.link(ob)

    # the floodlit arena seen from the stand: a big soft cool source over the floor
    light("arena", "AREA", (12, -70, 70), (math.radians(45), 0, 0), 2.6e5, (0.80, 0.88, 1.0), size=160)
    # this side's floodlights aim at the floor; their spill catches the front rows
    for x in (0.0, 12.0, 24.0):
        light("flood", "SPOT", (x, ROOF_FRONT + 1, ROOF_Z - 3), (math.radians(-40), 0, 0), 2.2e5,
              (0.9, 0.95, 1.0), spot=math.radians(70))
    # warm concourse light
    light("concourse", "POINT", (12, LOWER["d1"] + 1.5, 21.5), (0, 0, 0), 2500, (1.0, 0.6, 0.3))

    me = stand_ob.data
    attr = me.color_attributes.new("light", "FLOAT_COLOR", "CORNER")
    me.color_attributes.active_color = attr
    for ob in scene.objects:
        ob.select_set(False)
    stand_ob.select_set(True)
    bpy.context.view_layer.objects.active = stand_ob
    scene.render.bake.target = "VERTEX_COLORS"
    bpy.ops.object.bake(type="DIFFUSE", pass_filter={"DIRECT", "INDIRECT"}, target="VERTEX_COLORS")
    return attr


# ------------------------------------------------------------------------------------
# export: STM1 (src/tron/gStadium.cpp reads it)
# ------------------------------------------------------------------------------------
# u8[4] "STM1", u32 version 1, f32 module length, u32 submesh count; per submesh:
# u8 blend (0 opaque, 1 alpha tested, 2 additive), u8[3] 0, f32 glow, char[64]
# texture ("" none, "@name" one the game makes, else a data path), u32 vertex
# count, u32 index count, vertices (f32 x y z u v, u8 r g b a), u32 indices.


def srgb(c):
    c = max(0.0, min(1.0, c))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def export(ob, path, light=None, exposure=1.0):
    me = ob.data
    me.calc_loop_triangles()
    uvl = me.uv_layers.active.data if me.uv_layers else None
    submeshes = {}
    for tri in me.loop_triangles:
        mat = me.materials[tri.material_index]
        sm = submeshes.setdefault(mat.name, {"mat": mat, "verts": [], "index": {}, "idx": []})
        kind = mat["kind"]
        alb = mat["albedo"]
        for li in tri.loops:
            vi = me.loops[li].vertex_index
            co = me.vertices[vi].co
            uv = tuple(uvl[li].uv) if uvl else (0.0, 0.0)
            if kind == "glow":
                col = (srgb(alb[0]), srgb(alb[1]), srgb(alb[2]), 1.0)
            else:
                lit = light.data[li].color if light else (1, 1, 1, 1)
                col = tuple(srgb(alb[k] * lit[k] * exposure) for k in range(3)) + (1.0,)
            key = (vi, round(uv[0], 5), round(uv[1], 5), tuple(round(c, 3) for c in col))
            idx = sm["index"].get(key)
            if idx is None:
                idx = len(sm["verts"])
                sm["index"][key] = idx
                sm["verts"].append((co.x, co.y, co.z, uv[0], uv[1], col))
            sm["idx"].append(idx)
    blend = {"structure": 0, "crowd": 1, "glow": 2}
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"STM1")
        f.write(struct.pack("<IfI", 1, LENGTH, len(submeshes)))
        for sm in submeshes.values():
            mat = sm["mat"]
            tex = mat["texture"].encode()[:63]
            f.write(struct.pack("<B3xf", blend[mat["kind"]], float(mat["glow"])))
            f.write(tex + b"\0" * (64 - len(tex)))
            f.write(struct.pack("<II", len(sm["verts"]), len(sm["idx"])))
            for x, y, z, u, v, c in sm["verts"]:
                f.write(struct.pack("<5f4B", x, y, z, u, v, *(int(round(k * 255)) for k in c)))
            f.write(struct.pack("<%dI" % len(sm["idx"]), *sm["idx"]))
    n = sum(len(s["verts"]) for s in submeshes.values())
    print("STADIUM: wrote %s (%d submeshes, %d vertices, %d bytes)" % (path, len(submeshes), n, os.path.getsize(path)))


def exposure_for(attr):
    """scale the baked light so its bright end sits near white"""
    lum = np.array([0.2126 * d.color[0] + 0.7152 * d.color[1] + 0.0722 * d.color[2] for d in attr.data])
    top = np.percentile(lum, 97) if len(lum) else 1.0
    return 0.55 / top if top > 0 else 1.0


# ------------------------------------------------------------------------------------
# the crowd atlas: rows of low-poly spectators rendered from the arena side
# ------------------------------------------------------------------------------------

SHIRTS = [(0.10, 0.11, 0.13), (0.06, 0.07, 0.12), (0.18, 0.06, 0.06), (0.05, 0.10, 0.08),
          (0.30, 0.30, 0.32), (0.03, 0.03, 0.04), (0.20, 0.14, 0.05), (0.07, 0.07, 0.16),
          (0.02, 0.25, 0.32), (0.35, 0.16, 0.02), (0.20, 0.05, 0.25), (0.25, 0.25, 0.05)]
SKIN = [(0.55, 0.38, 0.28), (0.35, 0.22, 0.15), (0.70, 0.52, 0.42), (0.22, 0.14, 0.10)]


def flat(name, color, emit=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = 0.8
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*color, 1)
        bsdf.inputs["Emission Strength"].default_value = emit
    return m


def person(b, x, mats, rng):
    lean = rng.uniform(-0.04, 0.04)
    h = rng.uniform(0.92, 1.06)
    shirt = mats["shirt"][rng.randrange(len(mats["shirt"]))]
    skin = mats["skin"][rng.randrange(len(mats["skin"]))]
    # seat back behind, torso, head, legs on the row
    b.box((x, 0.35, 0.55), (0.62, 0.08, 0.55), mats["seat"])
    b.box((x + lean, 0.12, 0.42 + 0.30 * h), (0.44, 0.26, 0.60 * h), shirt)
    b.box((x + lean, 0.05, 0.42 + 0.66 * h), (0.22, 0.22, 0.24), skin)
    b.box((x, -0.16, 0.38), (0.40, 0.40, 0.16), mats["legs"])
    r = rng.random()
    if r < 0.12:
        # arms up, a phone light
        for sx in (-0.2, 0.2):
            b.beam((x + sx, 0.12, 0.42 + 0.55 * h), (x + sx * 1.3, 0.05, 1.32 * h), 0.09, shirt)
        b.box((x + 0.26, 0.0, 1.38 * h), (0.07, 0.02, 0.12), mats["phone"])
    elif r < 0.2:
        # a scarf held up in a team colour
        b.box((x, 0.0, 1.22 * h), (0.75, 0.03, 0.10), mats["scarf"][rng.randrange(len(mats["scarf"]))])
        for sx in (-0.3, 0.3):
            b.beam((x + sx * 0.6, 0.12, 0.42 + 0.55 * h), (x + sx, 0.02, 1.22 * h), 0.09, shirt)


def crowd_atlas():
    reset()
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = SAMPLES
    scene.render.film_transparent = True
    scene.render.resolution_x = CROWD_W
    scene.render.resolution_y = CROWD_H
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("night")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.025, 0.04, 1)
    scene.world = world

    mats = {
        "shirt": [flat("shirt%d" % i, c) for i, c in enumerate(SHIRTS)],
        "skin": [flat("skin%d" % i, c) for i, c in enumerate(SKIN)],
        "seat": flat("seat", (0.04, 0.06, 0.12)),
        "legs": flat("legs", (0.04, 0.04, 0.05)),
        "phone": flat("phone", (0.9, 0.95, 1.0), emit=25.0),
        "scarf": [flat("scarf_c", (0.0, 0.7, 1.0), 2.0), flat("scarf_o", (1.0, 0.45, 0.0), 2.0),
                  flat("scarf_p", (0.7, 0.2, 1.0), 2.0)],
    }
    width = LENGTH - AISLE - 0.2
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = width
    cam = bpy.data.objects.new("cam", cam_data)
    # from the arena, slightly above: the frame is width x CARD_H
    cam.location = (width / 2, -30, CARD_H / 2 + 30 * math.tan(math.radians(4)))
    cam.rotation_euler = (math.radians(90 - 4), 0, 0)
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam_data.shift_y = 0

    def light(name, kind, loc, rot, energy, color, size=10):
        data = bpy.data.lights.new(name, kind)
        data.energy = energy
        data.color = color
        if kind == "AREA":
            data.size = size
        ob = bpy.data.objects.new(name, data)
        ob.location = loc
        ob.rotation_euler = rot
        scene.collection.objects.link(ob)

    light("key", "AREA", (width / 2, -12, 10), (math.radians(50), 0, 0), 2600, (0.85, 0.9, 1.0), size=30)
    light("rim", "AREA", (width / 2, 6, 6), (math.radians(-50), 0, 0), 900, (1.0, 0.8, 0.6), size=30)

    strips = []
    for v in range(CROWD_STRIPS):
        rng = random.Random(100 + v)
        for ob in [o for o in scene.objects if o.name.startswith("row")]:
            bpy.data.objects.remove(ob, do_unlink=True)
        b = Builder("row%d" % v)
        x = 0.4
        while x < width - 0.3:
            if rng.random() > 0.12:
                person(b, x, mats, rng)
            x += rng.uniform(0.74, 0.86)
        b.object()
        path = os.path.join(ART, "build", "crowd_%d.png" % v)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(path)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(CROWD_H, CROWD_W, 4)
        strips.append(px)
        bpy.data.images.remove(img)
    # strip 0 at the top of the atlas (images are stored bottom-up)
    atlas = np.concatenate(list(reversed(strips)), axis=0)
    out = bpy.data.images.new("crowd", CROWD_W, CROWD_H * CROWD_STRIPS, alpha=True)
    out.pixels = atlas.ravel()
    # the RGBA atlas; pack_textures.py turns it into the shipped palette PNG
    out.filepath_raw = os.path.join(ART, "build", "crowd_rgba.png")
    out.file_format = "PNG"
    out.save()
    print("STADIUM: wrote crowd atlas %s (%d bytes)" % (out.filepath_raw, os.path.getsize(out.filepath_raw)))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ART, "crowd.blend"))


def main():
    reset()
    materials()
    s = stand()
    f = flood()
    f.location.x = -40
    bd = board()
    bd.location.x = -60
    attr = bake_scene(s)
    export(s, os.path.join(OUT_MODELS, "stand.stm"), attr, exposure_for(attr))
    export(f, os.path.join(OUT_MODELS, "flood.stm"), exposure=0.45)
    export(bd, os.path.join(OUT_MODELS, "board.stm"), exposure=0.3)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ART, "stadium.blend"))
    if os.environ.get("STADIUM_SKIP_CROWD") != "1":
        crowd_atlas()


main()
