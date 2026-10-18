// The embed starts silent: a game inside someone's X timeline should not make
// noise until the visitor asks for it. A new player's config already has sound off
// (web/wasm/browser_user.cfg), but a browser whose own game has it on, or one that turned it on
// before, would play it here; so every load of the embed turns it off, unless the player turned
// it on in the embed itself (Settings, SOUND), which `rc.embedSound` remembers for the embed
// alone. Not saved by the page; the full site is never touched (outside X's frame the embed
// shares its storage, so a session there may still save the silence).
import type { Aa } from "./aa/types";
import { EMBED } from "./links";

const KEY = "rc.embedSound";

export function embedSoundOff(aa: Aa): void {
  if (!EMBED) return;
  let chosen = false;
  try {
    chosen = localStorage.getItem(KEY) === "on";
  } catch {
    // blocked storage: silent every time
  }
  if (!chosen) aa.setConfig("SOUND_QUALITY", "0");
}

// the Settings store tells us whenever the sound changes in the embed
export function noteEmbedSound(name: string, value: string): void {
  if (!EMBED || name.toUpperCase() !== "SOUND_QUALITY") return;
  try {
    localStorage.setItem(KEY, value === "0" ? "off" : "on");
  } catch {
    // private mode or blocked storage
  }
}
