// The compact view: the page in a small frame (the embed, links.ts) or on a small screen (a
// phone either way up, a small window). One definition for the CSS (body.is-compact) and the
// page (useCompact), so the chat and the game's HUD change together.
//
// The game's HUD while a game runs (useGameHud), by the HUD style (settings/hudStyle.ts). MODERN, the default on every device: a client with aa.getCycle() gets the page's
// own meters (components/CycleHud.tsx: the rail and the danger edge) and the game's three gauges
// (speed, rubber, brakes) go, with the score, the fastest, the alive count and the ping. CLASSIC
// (or a client without getCycle): the game's own HUD (src/tron/gHud.cpp) as the player set it;
// in the compact view, where its readouts, drawn in screen fractions, pile into each other along
// the foot, the page leaves the three gauges, larger and spread out, and turns the other four
// off: a client with HUD_GAUGE_KEEP_ASPECT (gHud.cpp) draws the gauges in their own proportions
// on any screen shape and the page lays them out in a row in pixels; an older one gets them
// stretched. On a touch device the frame rate goes (the menu cell takes its corner). The
// player's own values come back when the game ends, the view grows or the style changes; they
// are kept in localStorage meanwhile, so a tab closed mid-game puts them back on the next load
// (the client saves its settings at the end of every session, which would otherwise keep the
// page's ones).
import { useEffect, useState } from "react";
import type { Aa } from "./aa/types";
import { DISCORD_PLATFORM } from "./discord";
import { DISCORD, EMBED } from "./links";
import { applyFrameRate, type HudStyle } from "./settings/hudStyle";

const QUERY = "(max-width: 760px), (max-height: 540px)";
const STORE = "rc.compactHud";

const HIDDEN = ["SHOW_SCORE", "SHOW_FASTEST", "SHOW_ALIVE", "SHOW_PING"];
// the game's own three, when the page draws the meters (CycleHud)
const METERS = ["SHOW_SPEED", "SHOW_RUBBER", "SHOW_BRAKE"];
// the rows MODERN takes over (the HUD tab greys them then: schema.ts `classicHud`)
export const MODERN_OVERRIDES = [...METERS, ...HIDDEN];
const GAUGES = [
  ["RUBBER", -1],
  ["SPEED", 0],
  ["BRAKE", 1]
] as const;
const KEEP_ASPECT = "HUD_GAUGE_KEEP_ASPECT";
// SHOW_FPS is not among them: the page owns it (hudStyle.ts applyFrameRate); an rc.compactHud
// left by an older page with SHOW_FPS in it is read for these names alone
const NAMES = [...HIDDEN, ...METERS, KEEP_ASPECT, ...GAUGES.flatMap(([g]) => [g + "_GAUGE_SIZE", g + "_GAUGE_LOCX", g + "_GAUGE_LOCY"])];

// a touch device: the primary pointer is a finger (a phone or tablet, the embed inside X's app
// too; a laptop with a touch screen keeps its fine pointer). The usage events' device class
// (platform/analytics.ts) tells the same apart for the reports
export function touchPrimary(): boolean {
  return window.matchMedia?.("(pointer: coarse)").matches ?? false;
}

// The embed (X's frame) is always small. A Discord Activity is compact on a phone (Discord's
// own platform=mobile) or when its window is small (picture in picture, the call grid, a
// narrow pane); on desktop it usually fills Discord's stage and gets the full view
function compactNow(): boolean {
  const small = window.matchMedia?.(QUERY).matches ?? false;
  if (DISCORD) return DISCORD_PLATFORM !== "desktop" || small;
  return EMBED || small;
}

// the compact view now, following resizes and turns; body.is-compact with it
export function useCompact(): boolean {
  const [compact, setCompact] = useState(compactNow);
  useEffect(() => {
    const media = window.matchMedia?.(QUERY);
    const update = () => setCompact(compactNow());
    media?.addEventListener("change", update);
    return () => media?.removeEventListener("change", update);
  }, []);
  useEffect(() => {
    document.body.classList.toggle("is-compact", compact);
  }, [compact]);
  return compact;
}

// the HUD's values for a view of this many pixels. Without HUD_GAUGE_KEEP_ASPECT (an older
// client) the gauges stretch with the screen: the meters' words about 14 px tall (the game sizes
// them at .12 of the gauge's size in screen halves), never smaller than the game's own .175;
// each gauge reaches 1.1 sizes either side for its 0 and its maximum, which run .12 sizes a
// character, so the side gauges stand 2.44 sizes and a margin from the centre (the game's own
// .48 at .175), and .25 is the largest size whose brake maximum stays on screen
function stretchedValues(height: number): Record<string, string> {
  const size = Math.min(0.25, Math.max(0.175, 117 / Math.max(1, height)));
  const offset = Math.min(0.98 - 1.22 * size, 2.44 * size + 0.05);
  const values: Record<string, string> = {};
  for (const [gauge, side] of GAUGES) {
    values[gauge + "_GAUGE_SIZE"] = size.toFixed(3);
    values[gauge + "_GAUGE_LOCX"] = (side * offset).toFixed(3);
    values[gauge + "_GAUGE_LOCY"] = "-0.9";
  }
  return values;
}

// With HUD_GAUGE_KEEP_ASPECT every horizontal measure follows the height, so the layout is in
// pixels of u, a gauge's height (its size times half the screen's height): its words are .24 u
// tall, its 0 starts 1.47 u left of its centre and its maximum (three characters at most, .16 u
// each) ends 1.95 u right. Always one row at the foot, the words
// up to 12 px, smaller where the width asks
const TEXT_PX = 12;
const LEFT = 1.47;
const RIGHT = 1.95;
const PITCH = RIGHT + 0.3 + LEFT; // centre to centre, a 0.3 u gap between neighbours
function keptValues(width: number, height: number): Record<string, string> {
  const span = 0.96 * width; // the gauges keep 2 % of the width clear at either side
  const u = Math.min(TEXT_PX / 0.24, span / (2 * PITCH + LEFT + RIGHT));
  const values: Record<string, string> = { [KEEP_ASPECT]: "1" };
  for (const [gauge, side] of GAUGES) {
    values[gauge + "_GAUGE_SIZE"] = ((2 * u) / height).toFixed(3);
    values[gauge + "_GAUGE_LOCX"] = ((side * PITCH * u) / (width / 2)).toFixed(3);
    values[gauge + "_GAUGE_LOCY"] = "-0.9";
  }
  return values;
}

// the page's values for the game's HUD: MODERN (the page's meters) on any view; else the
// compact view's gauge layout; else none (the classic HUD as the player set it)
function hudValues(width: number, height: number, keepAspect: boolean, pageMeters: boolean, compact: boolean): Record<string, string> {
  const values: Record<string, string> = {};
  if (pageMeters) for (const name of METERS) values[name] = "0";
  else if (compact) Object.assign(values, keepAspect ? keptValues(Math.max(1, width), Math.max(1, height)) : stretchedValues(height));
  else return values;
  for (const name of HIDDEN) values[name] = "0";
  return values;
}

// the client can tell the page its cycle's meters (aa.getCycle, a newer bundle)
export function pageMetersAvailable(aa: Aa | null): boolean {
  return !!aa && typeof aa.getCycle === "function";
}

// a config value as the game writes it back ("0.175000" for "0.175")
function same(value: string | undefined, set: string | undefined): boolean {
  if (value === undefined || set === undefined) return false;
  return value === set || (value.trim() !== "" && Number(value) === Number(set));
}

function readStore(): Record<string, string> | null {
  try {
    const raw = localStorage.getItem(STORE);
    const parsed: unknown = raw ? JSON.parse(raw) : null;
    return parsed && typeof parsed === "object" ? (parsed as Record<string, string>) : null;
  } catch {
    return null;
  }
}

function writeStore(values: Record<string, string> | null) {
  try {
    if (values) localStorage.setItem(STORE, JSON.stringify(values));
    else localStorage.removeItem(STORE);
  } catch {
    // blocked storage: the values still come back at the game's end
  }
}

// the game's HUD while `playing` (a network or local game): `style` the player's choice
// (hudStyle.ts), `pageMeters` the client can tell the page its cycle's meters (CycleHud draws
// them then, with MODERN); `touch` a touch device (its menu cell takes the frame rate's corner)
export function useGameHud(aa: Aa | null, playing: boolean, compact: boolean, style: HudStyle, pageMeters: boolean, touch = false) {
  // a tab closed mid-game left the page's values saved: the player's own come back
  useEffect(() => {
    if (!aa) return;
    const left = readStore();
    if (!left) return;
    for (const name of NAMES) if (typeof left[name] === "string") aa.setConfig(name, left[name]);
    aa.saveConfig();
    writeStore(null);
  }, [aa]);

  const modern = style === "modern" && pageMeters;
  useEffect(() => {
    if (!aa || !playing || (!modern && !compact)) return;
    const before: Record<string, string> = {};
    for (const name of NAMES) {
      const item = aa.getConfig(name);
      if (item) before[name] = item.value;
    }
    writeStore(before);
    let applied: Record<string, string> = {};
    const keepAspect = KEEP_ASPECT in before;
    const apply = () => {
      applied = hudValues(window.innerWidth, window.innerHeight, keepAspect, modern, compact);
      for (const [name, value] of Object.entries(applied)) if (name in before) aa.setConfig(name, value);
    };
    apply();
    window.addEventListener("resize", apply);
    return () => {
      window.removeEventListener("resize", apply);
      // what the player changed meanwhile (the Settings sheet in the game, CLASSIC picked) stays theirs
      for (const name of Object.keys(before)) if (same(aa.getConfig(name)?.value, applied[name])) aa.setConfig(name, before[name]);
      aa.saveConfig();
      writeStore(null);
    };
  }, [aa, playing, compact, modern]);

  // the frame rate on a touch device: off while a game runs, the player's choice after it
  useEffect(() => {
    if (!aa || !playing || !touch) return;
    applyFrameRate(aa, false);
    return () => applyFrameRate(aa);
  }, [aa, playing, touch]);
}
