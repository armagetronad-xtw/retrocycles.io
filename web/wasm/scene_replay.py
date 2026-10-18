#!/usr/bin/env python3
"""Turn recorded Team Sumo rounds into the attract scene's replay data.

The play page's backdrop (src/tron/gScene.cpp) replays real TST rounds: the
"late chaos" moments of the tronstats highlight reel (the whole lobby still
squeezed together in the shrinking zone). This script reads the tronstats match
archives, rebuilds every cycle's wall and writes src/tron/gSceneReplayData.h.

    py web/wasm/scene_replay.py --archives <tronstats>/TronLogs/match-logs/tst \
        <matchId>:<roundId> [<matchId>:<roundId> ...]
    py web/wasm/scene_replay.py --archives ... --find 10     # list late-chaos rounds

What it does, per round:
  * Walls: the archive holds ~12 Hz position samples (TstGridposLog), so turns fall
    between samples. The wall is rebuilt with a 1:1 port of tronstats'
    TrailReconstruction.ReconstructWall (one missed turn: snap to the corner; two
    missed turns: a jog placed where it crosses no wall, or the U-turn staple), and
    kept as its corners only.
  * Timing: a sample's timestamp is when the log line was written, not the game
    tick its position belongs to, so the distance between two samples jitters by
    +-0.9 units around the reported speed. Over a second the two agree, so the
    distance driven is fitted against time (local linear least squares over
    +-FIT_SAMPLES samples, forced monotonic): the corners stay exact, the motion is
    smooth and the round's clock (deaths, zone) is unchanged.
  * Deaths: the death line's time and position; the cycle drives on to it when it
    lies ahead on its last heading.
  * Zones: Team Sumo puts every team's fortress zone on the arena centre (60, 60),
    radius 50 shrinking 0.5/s. A zone is conquered by
    conquered += dt * (0.3 - 0.6 * owners inside) (clamped to [0, 1.01], alive
    owners only) and collapses over 2 s once it reaches 1, which kills the team.

Late chaos (tronstats TstHighlightAnchors, builder version 6): every team alive at
65 s (zone r = 17.5), else three teams alive at 75 s (r = 12.5), and no cycle
nearest to more than max(40%, 2/n) of the zone disc then (a settled round is not
chaos). The clip runs from the trigger - 3 s until fewer than three cycles are
alive (+2 s), at most 20 s; the replay adds LEAD seconds before and TAIL after.
"""

import argparse
import bisect
import gzip
import json
import math
import os
import sys
from collections import OrderedDict, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "src", "tron", "gSceneReplayData.h"))

# zone geometry and conquest (tronstats SumoZoneConquest)
CX, CY, R0, SHRINK = 60.0, 60.0, 50.0, 0.5
CONQUEST_DECAY, DEFEND_RATE, COLLAPSE = 0.3, 0.6, 2.0

# walls: 400 units of travel (tronstats' TRAIL_LEN, the game's CYCLE_WALLS_LENGTH on
# these servers). A dead cycle's wall stands WALLS_STAY_UP s: the logs bound it, a
# wall killed a cycle 7.74 s after its owner's death and another was driven through
# 7.93 s after (the game drops a dead wall 0.2 s after the delay). Explosions blow
# holes of 2 log units (tronstats' HOLE_RADIUS for tst), 3 game units.
WALLS_LENGTH, WALLS_STAY_UP, HOLE_RADIUS = 400, 7.6, 2.0

# late chaos (tronstats TstHighlightAnchors)
TRIGGERS = [(65.0, 4), (75.0, 3)]
CHAOS_THIN, CHAOS_PRE, CHAOS_MAX, FINISH_PAD, DEATH_EPS = 3, 3.0, 20.0, 2.0, 0.3

LEAD = 3.0   # replay seconds before the chaos clip starts
TAIL = 2.5   # and after it ends

FIT_SAMPLES = 4
AXIS_EPS = 0.01
JOG_FRACTIONS = [0.5, 0.25, 0.75, 0.0, 1.0]

# team colours: the game's own where the name is one (gold, ugly green), the
# tronstats palette's hue for the server's extra names (orange, purple)
TEAM_COLORS = {
    "team_gold": (1.0, 0.87, 0.0),
    "team_orange": (1.0, 0.5, 0.1),
    "team_purple": (0.72, 0.38, 1.0),
    "team_ugly": (0.27, 1.0, 1.0),
    "team_blue": (0.27, 0.53, 1.0),
    "team_red": (1.0, 0.07, 0.07),
    "team_green": (0.0, 1.0, 0.27),
    "team_violet": (1.0, 0.27, 1.0),
}


def team_color(team, mate):
    r, g, b = TEAM_COLORS.get(team, (0.8, 0.8, 0.8))
    if mate:  # the second player of a team a shade lighter, so the two read apart
        r, g, b = (c + (1 - c) * 0.35 for c in (r, g, b))
    return r, g, b


# ---------------------------------------------------------------------------
# archive
# ---------------------------------------------------------------------------

def is_type(entry, name):
    return name in entry.get("$type", "")


def load_rounds(path):
    data = json.load(gzip.open(path))
    rounds = defaultdict(list)
    for entry in data:
        rounds[entry.get("RoundId")].append(entry)
    return rounds


class Round:
    def __init__(self, logs):
        gp = sorted((x for x in logs if is_type(x, "GridposLog")), key=lambda g: g["ElapsedTime"])
        self.gridpos = gp
        self.t0 = gp[0]["ElapsedTime"] if gp else 0
        self.end = gp[-1]["ElapsedTime"] if gp else 0
        self.by = OrderedDict()
        for g in gp:
            self.by.setdefault(g["Username"], []).append(g)
        self.times = {p: [s["ElapsedTime"] for s in v] for p, v in self.by.items()}
        self.team = {p: v[0]["Team"] for p, v in self.by.items()}
        self.deaths = sorted((x for x in logs if is_type(x, "CycleDestroyLog")), key=lambda d: d["ElapsedTime"])
        self.death = {}
        for d in self.deaths:
            self.death.setdefault(d["Username"], d)
        self.last = {p: v[-1]["ElapsedTime"] for p, v in self.by.items()}
        for d in self.deaths:
            if d["Username"] in self.last:
                self.last[d["Username"]] = min(self.last[d["Username"]], d["ElapsedTime"] + DEATH_EPS)

    def sample_at(self, player, t):
        i = bisect.bisect_left(self.times[player], t)
        return self.by[player][min(i, len(self.by[player]) - 1)]


# ---------------------------------------------------------------------------
# late chaos
# ---------------------------------------------------------------------------

def zone_share_max(rnd, crowd, t):
    r = R0 - SHRINK * (t - rnd.t0)
    if r <= 1 or not crowd:
        return 1.0
    pos = [(s["PosX"], s["PosY"]) for s in (rnd.sample_at(p, t) for p in crowd)]
    counts = [0] * len(pos)
    total = 0
    n = 16
    for i in range(n):
        for j in range(n):
            x = CX - r + (i + .5) * (2 * r / n)
            y = CY - r + (j + .5) * (2 * r / n)
            if math.hypot(x - CX, y - CY) > r:
                continue
            total += 1
            best = min(range(len(pos)), key=lambda k: math.hypot(pos[k][0] - x, pos[k][1] - y))
            counts[best] += 1
    return 1.0 if total == 0 else max(counts) / total


def late_chaos(rnd):
    """The late-chaos clip of a round as (start, end) round seconds, or None."""
    if len(rnd.gridpos) < 20 or len(rnd.by) < 2:
        return None
    for time, need in TRIGGERS:
        at = rnd.t0 + time
        crowd = [p for p in rnd.by if rnd.last[p] >= at]
        if len({rnd.team[p] for p in crowd}) < need:
            continue
        if zone_share_max(rnd, crowd, at) >= max(0.4, 2.0 / max(len(crowd), 1)):
            continue
        thin = sorted((rnd.last[p] for p in crowd), reverse=True)
        thin_at = thin[CHAOS_THIN - 1] if len(thin) >= CHAOS_THIN else rnd.end
        end = min(rnd.end, at - CHAOS_PRE + CHAOS_MAX, thin_at + FINISH_PAD)
        return at - CHAOS_PRE - rnd.t0, end - rnd.t0, len(crowd)
    return None


# ---------------------------------------------------------------------------
# walls (port of tronstats TrailReconstruction.ReconstructWall)
# ---------------------------------------------------------------------------

def segments_cross(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2):
    a_h = abs(ay1 - ay2) <= AXIS_EPS
    b_h = abs(by1 - by2) <= AXIS_EPS
    if a_h != b_h:
        hx1, hx2, hy = (ax1, ax2, ay1) if a_h else (bx1, bx2, by1)
        vy1, vy2, vx = (by1, by2, bx1) if a_h else (ay1, ay2, ax1)
        return (min(hx1, hx2) + AXIS_EPS < vx < max(hx1, hx2) - AXIS_EPS and
                min(vy1, vy2) + AXIS_EPS < hy < max(vy1, vy2) - AXIS_EPS)
    if a_h:
        if abs(ay1 - by1) > AXIS_EPS:
            return False
        return min(max(ax1, ax2), max(bx1, bx2)) - max(min(ax1, ax2), min(bx1, bx2)) > AXIS_EPS
    if abs(ax1 - bx1) > AXIS_EPS:
        return False
    return min(max(ay1, ay2), max(by1, by2)) - max(min(ay1, ay2), min(by1, by2)) > AXIS_EPS


def crosses_wall(poly, x1, y1, x2, y2):
    return any(segments_cross(x1, y1, x2, y2, poly[i][0], poly[i][1], poly[i + 1][0], poly[i + 1][1])
               for i in range(len(poly) - 2))


def pick_jog(poly, along_a, along_b, ax, ay, bx, by, vertical):
    for f in JOG_FRACTIONS:
        jog = along_a + (along_b - along_a) * f
        c1 = (ax, jog) if vertical else (jog, ay)
        c2 = (bx, jog) if vertical else (jog, by)
        if not (crosses_wall(poly, ax, ay, *c1) or crosses_wall(poly, *c1, *c2) or crosses_wall(poly, *c2, bx, by)):
            return jog
    return along_a + (along_b - along_a) * .5


def reconstruct(samples):
    """Orthogonal wall vertices [x, y, dirx, diry, sampleIndex or None]."""
    poly = []
    stats = {"corner": 0, "jog": 0, "uturn": 0}
    for i, s in enumerate(samples):
        x, y, dx, dy = s["PosX"], s["PosY"], s["DirX"], s["DirY"]
        if not poly:
            poly.append([x, y, dx, dy, i])
            continue
        px, py, pdx, pdy, _ = poly[-1]
        if pdy != 0 and dx != 0:
            poly.append([px, y, dx, dy, i]); stats["corner"] += 1
            continue
        if pdx != 0 and dy != 0:
            poly.append([x, py, dx, dy, i]); stats["corner"] += 1
            continue
        mx, my = x - px, y - py
        if abs(mx) <= AXIS_EPS or abs(my) <= AXIS_EPS:
            poly.append([x, y, dx, dy, i])
            continue
        vertical = pdy != 0 or (pdx == 0 and abs(my) >= abs(mx))
        along_a, along_b = (py, y) if vertical else (px, x)
        reverse = (pdx != 0 and dx == -pdx) or (pdy != 0 and dy == -pdy)
        if reverse:
            sign = pdy if vertical else pdx
            jog = max(along_a, along_b) if sign > 0 else min(along_a, along_b)
            stats["uturn"] += 1
        else:
            jog = pick_jog(poly, along_a, along_b, px, py, x, y, vertical)
            stats["jog"] += 1
        lat_dx = int(math.copysign(1, mx)) if vertical else 0
        lat_dy = 0 if vertical else int(math.copysign(1, my))
        if abs(jog - along_a) > 1e-9:
            poly.append([px, jog, lat_dx, lat_dy, None] if vertical else [jog, py, lat_dx, lat_dy, None])
        if abs(along_b - jog) > 1e-9:
            poly.append([x, jog, dx, dy, None] if vertical else [jog, y, dx, dy, None])
        poly.append([x, y, dx, dy, i])
    return poly, stats


def path_lengths(poly):
    s = [0.0]
    for a, b in zip(poly, poly[1:]):
        s.append(s[-1] + abs(b[0] - a[0]) + abs(b[1] - a[1]))
    return s


def fit_distance(times, dist):
    """Local linear fit of distance against time at every sample, made monotonic."""
    n = len(times)
    out = []
    for i in range(n):
        lo, hi = max(0, i - FIT_SAMPLES), min(n, i + FIT_SAMPLES + 1)
        ts, ds = times[lo:hi], dist[lo:hi]
        mt, md = sum(ts) / len(ts), sum(ds) / len(ds)
        var = sum((t - mt) ** 2 for t in ts)
        slope = sum((t - mt) * (d - md) for t, d in zip(ts, ds)) / var if var > 0 else 0
        out.append(md + slope * (times[i] - mt))
    out[0], out[-1] = dist[0], dist[-1]  # spawn and last sample stay where they were
    for i in range(1, n):
        out[i] = min(max(out[i], out[i - 1]), dist[-1])
    return out


def track(rnd, player):
    samples = rnd.by[player]
    poly, stats = reconstruct(samples)
    dist = path_lengths(poly)

    times = [s["ElapsedTime"] - rnd.t0 for s in samples]
    sample_dist = [None] * len(samples)
    for v, d in zip(poly, dist):
        if v[4] is not None:
            sample_dist[v[4]] = d
    fitted = fit_distance(times, sample_dist)
    clock = list(zip(times, fitted))

    death = rnd.death.get(player)
    death_t = -1.0
    if death is not None:
        death_t = death["ElapsedTime"] - rnd.t0
        x, y, dx, dy, _ = poly[-1]
        ahead = (death["PosX"] - x) * dx + (death["PosY"] - y) * dy
        side = abs((death["PosX"] - x) * dy - (death["PosY"] - y) * dx)
        if 0 < ahead < 5 and side < 0.5:
            poly.append([x + dx * ahead, y + dy * ahead, dx, dy, None])
            dist.append(dist[-1] + ahead)
        if death_t > clock[-1][0]:
            clock.append((death_t, dist[-1]))

    # corners only: drop the vertices that continue straight on
    corners = [poly[0]]
    for v in poly[1:-1]:
        if (v[2], v[3]) != (corners[-1][2], corners[-1][3]):
            corners.append(v)
    corners.append(poly[-1])
    corners, stats["squared"] = square_corners(corners)
    return corners, clock, death_t, stats


def square_corners(corners):
    """Every leg on an axis, as a cycle drives: a diagonal leg (two samples whose turns the
    reconstruction could not place) becomes an L that goes on along the heading it arrived with,
    then turns. The clock measures legs as |dx| + |dy| (path_lengths) and the engine as straight
    lines (gScene.cpp ReplayTrack), so a diagonal put the replayed cycle that much ahead of its
    clock from there on (one attract-scene cycle 2 units: through a wall before its death).
    Returns the corners and how many legs were squared."""
    out, squared = [corners[0]], 0
    for v in corners[1:]:
        a = out[-1]
        if abs(v[0] - a[0]) > 1e-6 and abs(v[1] - a[1]) > 1e-6:
            if len(out) > 1:
                p = out[-2]
                horizontal = abs(a[0] - p[0]) >= abs(a[1] - p[1])
            else:
                horizontal = abs(v[0] - a[0]) >= abs(v[1] - a[1])
            if horizontal:
                out.append([v[0], a[1], 1 if v[0] > a[0] else -1, 0, None])
            else:
                out.append([a[0], v[1], 0, 1 if v[1] > a[1] else -1, None])
            squared += 1
        out.append(v)
    return out, squared


# ---------------------------------------------------------------------------
# zones
# ---------------------------------------------------------------------------

CONQUEST_STEP = 0.25   # seconds between the exported conquest samples


def zone_falls(rnd):
    """Per team: the round second its zone starts to collapse (or None) and its
    conquest level every CONQUEST_STEP s up to the fall (the zone's spin follows it)."""
    falls = {}
    teams = sorted(set(rnd.team.values()))
    dt = 0.02
    conquered = {t: 0.0 for t in teams}
    levels = {t: [] for t in teams}
    t = 0.0
    step = 0
    duration = rnd.end - rnd.t0
    while t <= duration and len(falls) < len(teams):
        r = R0 - SHRINK * t
        inside = defaultdict(int)
        for p in rnd.by:
            if rnd.last[p] - rnd.t0 < t:
                continue
            s = rnd.sample_at(p, rnd.t0 + t)
            if math.hypot(s["PosX"] - CX, s["PosY"] - CY) <= r:
                inside[rnd.team[p]] += 1
        for team in teams:
            if team in falls:
                continue
            if step % round(CONQUEST_STEP / dt) == 0:
                levels[team].append(conquered[team])
            conquered[team] = min(1.01, max(0.0, conquered[team] + dt * (CONQUEST_DECAY - DEFEND_RATE * inside[team])))
            if conquered[team] >= 1:
                falls[team] = t
        step += 1
        t += dt
    return {team: (falls.get(team), levels[team]) for team in teams}


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------

def build_clip(archives, match_id, round_id):
    rounds = load_rounds(os.path.join(archives, match_id + ".json.gz"))
    rnd = Round(rounds[round_id])
    chaos = late_chaos(rnd)
    if chaos is None:
        sys.exit(f"{match_id}:{round_id} has no late-chaos moment")
    start, end, crowd = chaos
    falls = zone_falls(rnd)

    cycles = []
    mates = defaultdict(int)
    for player in rnd.by:
        corners, clock, death_t, stats = track(rnd, player)
        team = rnd.team[player]
        color = team_color(team, mates[team] > 0)
        mates[team] += 1
        cycles.append(dict(player=player, team=team, color=color, corners=corners, clock=clock,
                           death=death_t, stats=stats))

    zones = [dict(team=team, color=team_color(team, False), fall=-1.0 if fall is None else fall, levels=levels)
             for team, (fall, levels) in falls.items()]

    logged = {d["Username"]: d["ElapsedTime"] - rnd.t0 for d in rnd.deaths if d["Predator"] == "zone"}
    for z in zones:
        team_zone = [logged[p] for p in logged if rnd.team.get(p) == z["team"]]
        z["logged"] = min(team_zone) if team_zone else None

    return dict(match=match_id, round=round_id, start=max(0.0, start - LEAD),
                end=min(rnd.end - rnd.t0, end + TAIL), chaos=(start, end), crowd=crowd,
                cycles=cycles, zones=zones)


def f(v):
    """A number as the shortest text with 3 decimals at most, always with a '.'
    (it is followed by 'f' as a C++ float literal)."""
    text = f"{v:.3f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


def write_header(clips, path):
    lines = []
    w = lines.append
    w("// Generated by web/wasm/scene_replay.py -- do not edit by hand.")
    w("//")
    w("// The attract scene's replay (gScene.cpp): recorded Team Sumo rounds from the")
    w("// tronstats archives, their late-chaos moments. Units are the log's (the arena")
    w("// centre is 60,60; the game's own units are SG_SCENE_REPLAY_SCALE times larger),")
    w("// times are seconds since the round started.")
    w("//")
    for c in clips:
        w(f"//   match {c['match']} round {c['round']}: chaos {f(c['chaos'][0])}..{f(c['chaos'][1])} s, "
          f"{c['crowd']} alive at the trigger; replay {f(c['start'])}..{f(c['end'])} s")
    w("")
    w("#ifndef ArmageTron_GSCENEREPLAYDATA_H")
    w("#define ArmageTron_GSCENEREPLAYDATA_H")
    w("")
    w("#define SG_SCENE_REPLAY_SCALE 1.5f   // logged positions advance 2/3 as fast as the reported speed")
    w("#define SG_SCENE_REPLAY_CENTER_X 60.0f")
    w("#define SG_SCENE_REPLAY_CENTER_Y 60.0f")
    w(f"#define SG_SCENE_REPLAY_ZONE_RADIUS {f(R0)}f")
    w(f"#define SG_SCENE_REPLAY_ZONE_SHRINK {f(SHRINK)}f")
    w(f"#define SG_SCENE_REPLAY_ZONE_COLLAPSE {f(COLLAPSE)}f")
    w(f"#define SG_SCENE_REPLAY_WALLS_LENGTH {f(WALLS_LENGTH)}f  // game units (tronstats' TRAIL_LEN)")
    w(f"#define SG_SCENE_REPLAY_WALLS_STAY_UP {f(WALLS_STAY_UP)}f  // seconds a dead cycle's wall stands")
    w(f"#define SG_SCENE_REPLAY_CONQUEST_STEP {f(CONQUEST_STEP)}f")
    w(f"#define SG_SCENE_REPLAY_HOLE_RADIUS {f(HOLE_RADIUS)}f  // log units an explosion blows out of the walls")
    w("")
    w("struct gSceneReplayCycle")
    w("{")
    w("    float r, g, b;")
    w("    float death;           //!< round second of the death, < 0: alive at the end")
    w("    int corner, corners;   //!< its run of sg_sceneReplayCorners (x, y pairs; the first is the spawn)")
    w("    int clock, clocks;     //!< its run of sg_sceneReplayClock (t, distance driven pairs)")
    w("};")
    w("")
    w("struct gSceneReplayZone")
    w("{")
    w("    float r, g, b;")
    w("    float fall;            //!< round second the zone starts to collapse, < 0: never")
    w("    int conquest, conquests; //!< its run of sg_sceneReplayConquest: the conquest level every")
    w("                             //!< SG_SCENE_REPLAY_CONQUEST_STEP s from the round's start to the fall")
    w("};")
    w("")
    w("struct gSceneReplayClip")
    w("{")
    w("    float start, end;      //!< the round seconds shown")
    w("    int cycle, cycles;     //!< its run of sg_sceneReplayCycles")
    w("    int zone, zones;       //!< its run of sg_sceneReplayZones")
    w("};")
    w("")

    corners, clock, cyc, zon, clp, conquest = [], [], [], [], [], []
    for c in clips:
        first_cycle, first_zone = len(cyc), len(zon)
        for cy in c["cycles"]:
            first_corner, first_clock = len(corners), len(clock)
            corners.extend((v[0], v[1]) for v in cy["corners"])
            clock.extend(cy["clock"])
            r, g, b = cy["color"]
            cyc.append(f"    {{ {f(r)}f, {f(g)}f, {f(b)}f, {f(cy['death'])}f, {first_corner}, {len(cy['corners'])}, "
                       f"{first_clock}, {len(cy['clock'])} }}, // {cy['player']} ({cy['team']})")
        for z in c["zones"]:
            r, g, b = z["color"]
            zon.append(f"    {{ {f(r)}f, {f(g)}f, {f(b)}f, {f(z['fall'])}f, {len(conquest)}, {len(z['levels'])} }}, // {z['team']}")
            conquest.extend(z["levels"])
        clp.append(f"    {{ {f(c['start'])}f, {f(c['end'])}f, {first_cycle}, {len(c['cycles'])}, "
                   f"{first_zone}, {len(c['zones'])} }}, // {c['match']}:{c['round']}")

    w("static gSceneReplayClip const sg_sceneReplayClips[] =")
    w("{")
    lines.extend(clp)
    w("};")
    w("")
    w("static gSceneReplayCycle const sg_sceneReplayCycles[] =")
    w("{")
    lines.extend(cyc)
    w("};")
    w("")
    w("static gSceneReplayZone const sg_sceneReplayZones[] =")
    w("{")
    lines.extend(zon)
    w("};")
    w("")
    w("static float const sg_sceneReplayConquest[] =")
    w("{")
    for i in range(0, len(conquest), 16):
        w("    " + ", ".join(f"{f(round(v, 3))}f" for v in conquest[i:i + 16]) + ",")
    w("};")
    w("")
    for name, pairs in (("sg_sceneReplayCorners", corners), ("sg_sceneReplayClock", clock)):
        w(f"static float const {name}[] =")
        w("{")
        row = []
        for a, b in pairs:
            row.append(f"{f(round(a, 3))}f,{f(round(b, 3))}f")
            if len(row) == 8:
                w("    " + ", ".join(row) + ",")
                row = []
        if row:
            w("    " + ", ".join(row) + ",")
        w("};")
        w("")
    w("#endif")
    with open(path, "w", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def find(archives, limit):
    """Rank the archive's late-chaos rounds: whole lobby, long, many kills, open zone."""
    found = []
    for name in sorted(os.listdir(archives)):
        if not name.endswith(".json.gz"):
            continue
        for rid, logs in load_rounds(os.path.join(archives, name)).items():
            rnd = Round(logs)
            chaos = late_chaos(rnd)
            if chaos is None or len(rnd.by) < 8:
                continue
            start, end, crowd = chaos
            kills = sum(1 for d in rnd.deaths if d["Predator"] not in ("zone", d["Username"])
                        and start <= d["ElapsedTime"] - rnd.t0 <= end)
            hz = len(rnd.gridpos) / (len(rnd.by) * max(rnd.end - rnd.t0, 1))
            found.append((crowd + (end - start) / 4 + kills, name[:-8], rid, crowd, end - start, kills, hz))
    found.sort(reverse=True)
    for row in found[:limit]:
        print(f"{row[1]}:{row[2]}  alive {row[3]}  clip {row[4]:.1f}s  kills {row[5]}  {row[6]:.1f} Hz")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--archives", required=True, help="tronstats TronLogs/match-logs/tst")
    ap.add_argument("--find", type=int, metavar="N", help="list the N best late-chaos rounds and stop")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("rounds", nargs="*", metavar="match:round")
    args = ap.parse_args()
    if args.find:
        find(args.archives, args.find)
        return
    if not args.rounds:
        ap.error("name at least one match:round")
    clips = []
    for spec in args.rounds:
        match_id, round_id = spec.split(":")
        clip = build_clip(args.archives, match_id, round_id)
        clips.append(clip)
        print(f"{spec}: replay {clip['start']:.1f}..{clip['end']:.1f} s (chaos {clip['chaos'][0]:.1f}..{clip['chaos'][1]:.1f}, {clip['crowd']} alive)")
        for cy in clip["cycles"]:
            print(f"  {cy['player']:28s} {cy['team']:12s} corners {len(cy['corners']):4d}  "
                  f"missed turns rebuilt: {cy['stats']['corner']} corners, {cy['stats']['jog']} jogs, "
                  f"{cy['stats']['uturn']} u-turns  death {cy['death']:.2f}")
        for z in clip["zones"]:
            logged = "-" if z["logged"] is None else f"{z['logged']:.2f}"
            print(f"  zone {z['team']:12s} collapses at {z['fall']:.2f} (team's logged zone death {logged})")
    write_header(clips, args.out)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
