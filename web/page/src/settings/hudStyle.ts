// The HUD style (the HUD tab's first row): MODERN, the page's own meters on every
// device (components/CycleHud.tsx: rubber left, the brakes, the speed; compact.ts useGameHud turns
// the game's gauges and its score, fastest, alive and ping off while a game runs), or CLASSIC, the
// game's own HUD (gHud.cpp) with the HUD tab's switches. A page choice, kept in localStorage
// (`rc.hudStyle`: "classic", or nothing for MODERN, the default). Picking CLASSIC turns the game's
// readouts on (CLASSIC_HUD_VALUES), so it shows the whole classic HUD whatever MODERN left.
//
// And the frame rate counter (`rc.showFps`, "1" when on; off by default): a saved user.cfg
// cannot be relied on to keep it, so the page sets SHOW_FPS from this on every load (applyFrameRate) and the
// GRAPHICS row's switch writes it here too (noteFrameRateSetting, from the Settings store).
import { useSyncExternalStore } from "react";
import type { Aa } from "../aa/types";

export type HudStyle = "modern" | "classic";

const KEY = "rc.hudStyle";
const FPS_KEY = "rc.showFps";

// CLASSIC's readouts, set once when it is picked; the HUD tab's switches change them from there
export const CLASSIC_HUD_VALUES: Record<string, string> = {
  SHOW_HUD: "1",
  SHOW_SPEED: "1",
  SHOW_RUBBER: "1",
  SHOW_BRAKE: "1",
  SHOW_PING: "1",
  SHOW_SCORE: "1",
  SHOW_ALIVE: "1",
  SHOW_FASTEST: "1"
};

function read(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function write(key: string, value: string | null) {
  try {
    if (value === null) localStorage.removeItem(key);
    else localStorage.setItem(key, value);
  } catch {
    // blocked storage: the choice lasts for this page load
  }
}

let style: HudStyle = read(KEY) === "classic" ? "classic" : "modern";
const listeners = new Set<() => void>();

export function hudStyle(): HudStyle {
  return style;
}

export function setHudStyle(next: HudStyle): void {
  if (next === style) return;
  style = next;
  write(KEY, next === "classic" ? "classic" : null);
  for (const fn of listeners) fn();
}

export function useHudStyle(): HudStyle {
  return useSyncExternalStore(
    (fn) => {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    hudStyle,
    hudStyle
  );
}

// ---- the frame rate counter ------------------------------------------------------------------

let fpsMemo: boolean | null = null; // when storage is blocked

export function frameRateShown(): boolean {
  return fpsMemo ?? read(FPS_KEY) === "1";
}

// the Settings store tells us about every value it writes: the GRAPHICS row's switch, a paste
export function noteFrameRateSetting(name: string, value: string): void {
  if (name.toUpperCase() !== "SHOW_FPS") return;
  fpsMemo = value !== "0" && value !== "";
  write(FPS_KEY, fpsMemo ? "1" : null);
}

// the counter as the player wants it: on load (useClient), and after a game on a touch device
// (compact.ts, where the menu cell takes its corner). The attract scene masks SHOW_FPS (API.md
// "The attract scene"): a write while it runs would be lost when it stops, so it is wrapped
export function applyFrameRate(aa: Aa, on = frameRateShown()): void {
  const want = on ? "1" : "0";
  const item = aa.getConfig("SHOW_FPS");
  if (!item || !item.writable) return;
  let scene = 0;
  try {
    const st = aa.getState();
    scene = st.state === "scene" ? st.sceneState || 1 : 0;
  } catch {
    scene = 0;
  }
  if (!scene && item.value === want) return;
  if (scene) aa.sceneStop();
  aa.setConfig("SHOW_FPS", want);
  if (scene) aa.scene(scene);
}
