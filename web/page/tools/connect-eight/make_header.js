// A throwaway gSceneReplayData.h: one white cycle running a square figure 8 (two loops
// of side S log units crossing at the arena centre), for recording the connecting
// screen's clip from the engine. Never committed.
const fs = require("fs");
const out = process.argv[2];
const S = 6, C = 60, SPEED = 18, T = 120;
const lap = [[C, C + S], [C - S, C + S], [C - S, C], [C + S, C], [C + S, C - S], [C, C - S]];
const corners = [[C, C]];
const laps = Math.ceil((SPEED * T) / (8 * S)) + 2;
for (let i = 0; i < laps; i++) corners.push(...lap);
corners.push([C, C + S]);
const h = `// THROWAWAY recording data (connecting screen clip), not the shipped replay.
#ifndef ArmageTron_GSCENEREPLAYDATA_H
#define ArmageTron_GSCENEREPLAYDATA_H

#define SG_SCENE_REPLAY_SCALE 1.5f
#define SG_SCENE_REPLAY_CENTER_X 60.0f
#define SG_SCENE_REPLAY_CENTER_Y 60.0f
#define SG_SCENE_REPLAY_ZONE_RADIUS 50.0f
#define SG_SCENE_REPLAY_ZONE_SHRINK 0.5f
#define SG_SCENE_REPLAY_ZONE_COLLAPSE 2.0f
#define SG_SCENE_REPLAY_WALLS_LENGTH ${(3 * S * 1.5).toFixed(2)}f
#define SG_SCENE_REPLAY_WALLS_STAY_UP 7.6f
#define SG_SCENE_REPLAY_CONQUEST_STEP 0.25f
#define SG_SCENE_REPLAY_HOLE_RADIUS 2.0f

struct gSceneReplayCycle
{
    float r, g, b;
    float death;
    int corner, corners;
    int clock, clocks;
};

struct gSceneReplayZone
{
    float r, g, b;
    float fall;
    int conquest, conquests;
};

struct gSceneReplayClip
{
    float start, end;
    int cycle, cycles;
    int zone, zones;
};

static gSceneReplayClip const sg_sceneReplayClips[] =
{
    { 20.0f, ${T - 5}.0f, 0, 1, 0, 0 },
};

static gSceneReplayCycle const sg_sceneReplayCycles[] =
{
    { 1.0f, 1.0f, 1.0f, -1.0f, 0, ${corners.length}, 0, 2 },
};

static gSceneReplayZone const sg_sceneReplayZones[] =
{
    { 1.0f, 1.0f, 1.0f, -1.0f, 0, 1 },
};

static float const sg_sceneReplayConquest[] =
{
    0.0f,
};

static float const sg_sceneReplayCorners[] =
{
${corners.map(([x, y]) => `    ${x.toFixed(2)}f, ${y.toFixed(2)}f,`).join("\n")}
};

static float const sg_sceneReplayClock[] =
{
    0.0f, 0.0f,
    ${T}.0f, ${(SPEED * T).toFixed(2)}f,
};

#endif
`;
fs.writeFileSync(out, h);
console.log("corners", corners.length, "lap seconds", (8 * S) / SPEED);
