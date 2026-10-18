// The keys line: at the start of the player's first games in this browser, one line low
// at the left over the game names the keys as the key map has them (keys.ts hintItems):
// each key in ivory, the game's word for it in grey. Nothing acts here (no caret, no
// focus); it fades in, and the page takes it away after KEYS_LINE_MS. The game's own
// tooltips ("Press d to turn left.") are off (web/wasm/autoexec.cfg). Not on a touch device
// (compact.ts touchPrimary: a phone or tablet, the embed in X's app too): it names keys a
// finger does not have, and on-screen controls are to come there.
import type { HintItem } from "../keys";
import { touchPrimary } from "../compact";
import { KeyGroups } from "./KeyCaps";

export const KEYS_LINE_MS = 12000;
// how many games show it (a browser that has seen it this often knows the keys)
const KEYS_LINE_GAMES = 3;

export function KeysLine({ items }: { items: HintItem[] }) {
  return <KeyGroups className="keys-line" id="keys-line" items={items} live />;
}

// a count, kept in localStorage like the conduct line's flag (no id, no time); blocked
// storage shows it every game, which is harmless
const COUNT_KEY = "rc.keysLineGames";

export function keysLineWanted(): boolean {
  if (touchPrimary()) return false;
  try {
    return Number(localStorage.getItem(COUNT_KEY) || "0") < KEYS_LINE_GAMES;
  } catch {
    return true;
  }
}

export function countKeysLine() {
  try {
    localStorage.setItem(COUNT_KEY, String(Number(localStorage.getItem(COUNT_KEY) || "0") + 1));
  } catch {
    // private mode or blocked storage
  }
}
