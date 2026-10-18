// Games are played in the home screen's arena, the attract scene's stadium (API.md "The attract
// scene"), instead of the stock rim and floor. It is the default; a player can still pick the
// classic arena (Settings, GRAPHICS). While a session runs (a room or the local game) the client gets the
// scene's look as game settings: the low rim and no sky the stadium needs, the scene's floor
// detail, floor reflection and short explosion rays. Back at the home screen the player's own
// values return and are saved again, because every session end saves user.cfg with whatever
// was in force, and user.cfg is shared with the main page (same origin).
//
// Two choices on the Settings sheet's GRAPHICS tab (settings/schema.ts), kept here in
// localStorage because the game saves neither: the arena (STADIUM: 1 stadium, the default; 0
// classic, the player's own look untouched) and the arena lights (STADIUM_INTENSITY 1/0, with
// the floor's reflection of them, STADIUM_REFLECT; they light the home screen's stadium too).
import type { Aa, AaState } from "./aa/types";

// games use the stadium arena unless the player picks the classic one
export const ARENA_TEST = true;

const IN_SESSION = new Set(["connecting", "syncing", "playing", "fetching", "local"]);
const KEY_ARENA = "rc.arena"; // "stadium" | "classic"
const KEY_LIGHTS = "rc.arenaLights"; // "on" | "off"
const REFLECT_ON = "0.5"; // STADIUM_REFLECT's default

// [game setting, the scene setting it takes its value from, or a fixed value]. Not the floor:
// games keep the player's FLOOR_DETAIL and the tiers' FLOOR_REFLECT (no trail mirror in play)
const LOOK: Array<[string, string]> = [
  ["HIGH_RIM", "=0"],
  ["UPPER_SKY", "=0"],
  ["LOWER_SKY", "=0"],
  ["EXPLOSION_RAY_LENGTH", "SCENE_EXPLOSION_RAYS"]
];

function read(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function write(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    // private mode or blocked storage: the choice lasts until the reload
  }
}

// the running build's hooks (installArenaTest), for the Settings store's noteArena
let current: { aa: Aa; stadium(): boolean; apply(): void; restore(): void; inSession(): boolean } | null = null;

export function installArenaTest(aa: Aa): () => void {
  // the player's values, read the first time a session starts (the scene has stopped by then,
  // so getConfig answers with them, not with the scene's)
  let own: Record<string, string> | null = null;
  let applied = false;

  const lights = read(KEY_LIGHTS) !== "off";
  aa.setConfig("STADIUM", read(KEY_ARENA) === "classic" ? "0" : "1");
  aa.setConfig("STADIUM_INTENSITY", lights ? "1" : "0");
  aa.setConfig("STADIUM_REFLECT", lights ? REFLECT_ON : "0");

  const stadium = () => aa.getConfig("STADIUM")?.value !== "0";
  const inSession = () => {
    try {
      return IN_SESSION.has(aa.getState().state);
    } catch {
      return false;
    }
  };

  const value = (from: string): string | null => {
    if (from.startsWith("=")) return from.slice(1);
    return aa.getConfig(from)?.value ?? null;
  };

  const apply = () => {
    if (!stadium()) return;
    if (!own) {
      own = {};
      for (const [name] of LOOK) {
        const item = aa.getConfig(name);
        if (item) own[name] = item.value;
      }
    }
    for (const [name, from] of LOOK) {
      const v = value(from);
      if (v !== null && name in own) aa.setConfig(name, v);
    }
    applied = true;
  };

  const restore = () => {
    if (!applied || !own) return;
    applied = false;
    for (const [name, v] of Object.entries(own)) aa.setConfig(name, v);
    aa.saveConfig();
  };

  const off = aa.on("state", (s: AaState) => {
    if (IN_SESSION.has(s.state)) {
      if (!applied) apply();
    } else restore();
  });
  // the rounds of a session: again, in case anything set them back meanwhile
  const offRound = aa.on("round", (e: { state: string }) => {
    if (e.state === "started" && applied) apply();
  });
  // a closed tab: the player's values first (the leave's disconnect ends the session after this)
  window.addEventListener("pagehide", restore);
  current = { aa, stadium, apply, restore, inSession };
  console.info("arena: games use the home screen's arena (STADIUM)");
  return () => {
    off();
    offRound();
    window.removeEventListener("pagehide", restore);
    current = null;
  };
}

// the Settings store tells us about every value it writes: the arena choices are remembered,
// and a change of arena during a game takes effect at once
export function noteArena(name: string, value: string): void {
  if (!ARENA_TEST || !current) return;
  const n = name.toUpperCase();
  if (n === "STADIUM") {
    write(KEY_ARENA, value === "0" ? "classic" : "stadium");
    if (current.inSession()) {
      if (current.stadium()) current.apply();
      else current.restore();
    }
  } else if (n === "STADIUM_INTENSITY") {
    const on = Number(value) > 0;
    write(KEY_LIGHTS, on ? "on" : "off");
    current.aa.setConfig("STADIUM_REFLECT", on ? REFLECT_ON : "0");
  }
}
