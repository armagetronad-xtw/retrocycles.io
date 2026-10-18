// The GRAPHICS tab's look preset: MODERN (the quality tiers' look, the default) or CLASSIC, the
// game's own look for players who knew it before the browser fork: no bloom or wall glow, the
// stock textured trails and per vertex cycle shading, the high rim, and the classic arena
// (arenaTest.ts). CLASSIC takes the quality to CUSTOM, which is what keeps a
// tier from writing its values back. Some of those values are not saved by the game
// (CYCLE_LIGHTING, BROWSER_WALL_GLOW, WALL_CONTACT_GLOW: not in user.cfg nor in the browser's
// autoexec list), so the choice is kept here (localStorage) and applied again on every load
// while the quality is still CUSTOM; picking a quality tier ends it.
import type { Aa } from "../aa/types";
import { ARENA_TEST } from "../arenaTest";

const KEY = "rc.look";

export const CLASSIC_VALUES: Record<string, string> = {
  BROWSER_QUALITY: "-1",
  BROWSER_BLOOM: "0",
  BROWSER_WALL_GLOW: "0",
  CYCLE_LIGHTING: "0",
  WALL_STYLE: "0",
  WALL_EMIT: "0",
  WALL_CONTACT_GLOW: "0",
  TRAIL_GLASS: "0",
  HIGH_RIM: "1",
  ...(ARENA_TEST ? { STADIUM: "0" } : {})
};

// set once when CLASSIC is picked, as the old game played: no antialiasing, frames as fast as the
// GPU gives them. Not part of the look itself (lookOf, the reload's
// applyClassicLook), so a player can turn either back without leaving CLASSIC; both are saved
export const CLASSIC_START_VALUES: Record<string, string> = {
  BROWSER_MSAA: "0",
  UNCAPPED_FPS: "1"
};

// MODERN: back to the automatic tier (it writes its own look on its next frame)
export const MODERN_VALUES: Record<string, string> = {
  BROWSER_QUALITY: "0",
  CYCLE_LIGHTING: "1",
  ...(ARENA_TEST ? { STADIUM: "1" } : {})
};

const same = (a: string, b: string) => (Number.isFinite(Number(a)) && Number.isFinite(Number(b)) ? Math.abs(Number(a) - Number(b)) < 1e-3 : a === b);

// which look the values are: classic (all of its values), modern (any quality tier), else null
// (CUSTOM: the player's own mix)
export function lookOf(get: (name: string) => string): "classic" | "modern" | null {
  if (Object.entries(CLASSIC_VALUES).every(([n, v]) => same(get(n), v))) return "classic";
  return get("BROWSER_QUALITY") !== "-1" ? "modern" : null;
}

function remember(classic: boolean) {
  try {
    if (classic) localStorage.setItem(KEY, "classic");
    else localStorage.removeItem(KEY);
  } catch {
    // private mode or blocked storage: the unsaved values return to the game's on reload
  }
}

// the row's choice (Settings.tsx); the values themselves go through the store
export function noteLook(look: "classic" | "modern"): void {
  remember(look === "classic");
}

// the Settings store tells us about every value it writes: a quality tier ends CLASSIC
export function noteLookSetting(name: string, value: string): void {
  if (name.toUpperCase() === "BROWSER_QUALITY" && value !== "-1") remember(false);
}

// on load: CLASSIC again, while the quality is still the CUSTOM it left
export function applyClassicLook(aa: Aa): void {
  let classic = false;
  try {
    classic = localStorage.getItem(KEY) === "classic";
  } catch {
    return;
  }
  if (!classic) return;
  if (aa.getConfig("BROWSER_QUALITY")?.value !== "-1") {
    remember(false);
    return;
  }
  for (const [name, value] of Object.entries(CLASSIC_VALUES)) {
    const item = aa.getConfig(name);
    if (item && item.writable && item.value !== value) aa.setConfig(name, value);
  }
}
