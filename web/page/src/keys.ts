// The keys, page side: the move from the old default key map to the browser's template
// (web/wasm/browser_keys.cfg), and the words for the keys line (components/KeysLine.tsx).
import type { Aa, AaBinding } from "./aa/types";
import { EMBED } from "./links";
import { t, type PlainKey } from "./i18n";

// keys_cursor.cfg, the template browsers were seeded with before browser_keys.cfg: its
// keys other than the arrows and v (which the new template keeps). Player 1: u o turn,
// j k l glance, i brake, c view; player 2 (nobody plays it in the browser): z x w s r q f
// e d Space.
const OLD_TEMPLATE_KEYS = [117, 111, 108, 107, 106, 105, 99, 122, 120, 119, 115, 114, 113, 102, 101, 100, 32];

function codesOf(bindings: AaBinding[], action: string): string {
  return bindings
    .filter((b) => b.player === 1 && b.action === action)
    .map((b) => b.code ?? String(b.keysym))
    .sort()
    .join(" ");
}

// A browser still on the old defaults (both turns exactly as keys_cursor.cfg set them:
// a player who changed either keeps their map) is moved to browser_keys.cfg once:
// the old template's keys are unbound, the new template is included, and the map is
// saved. Afterwards the turns differ from the old ones, so this never runs again.
export function moveOffOldDefaults(aa: Aa): boolean {
  let bindings: AaBinding[];
  try {
    bindings = aa.getBindings();
  } catch {
    return false;
  }
  if (codesOf(bindings, "CYCLE_TURN_LEFT") !== "ArrowLeft KeyU" || codesOf(bindings, "CYCLE_TURN_RIGHT") !== "ArrowRight KeyO") return false;
  for (const key of OLD_TEMPLATE_KEYS) aa.unbind(key);
  aa.console("INCLUDE browser_keys.cfg");
  aa.saveConfig();
  console.info("keys: moved from the old defaults to browser_keys.cfg");
  return true;
}

// default.cfg's "camera on numpad and mouse": mouse motion (uInput.h SDLK_MOUSE_X_PLUS =
// SDLK_LAST 323 + 1: 324 right, 325 left, 326 up, 327 down) turns and banks the camera, so
// a mouse moved over the game swung the view. Mouse button 3
// (332, the right button: ZOOM_IN while held) stays; the wheel (buttons 4/5, 333/334 from the
// shim) is bound to nothing. browser_user.cfg unbinds them on a first run.
const MOUSE_MOTION = [324, 325, 326, 327];
const MOUSE_CAMERA_DONE = "rc.mouseCameraOff";

// A browser whose user.cfg still has default.cfg's mouse-motion camera binds loses them, once
// (the localStorage flag: a mouse bound again later, by hand in the console, stays). Saved.
export function dropMouseCamera(aa: Aa): boolean {
  try {
    if (window.localStorage.getItem(MOUSE_CAMERA_DONE)) return false;
  } catch {
    // no storage: the check below runs on every load, harmless
  }
  let bindings: AaBinding[];
  try {
    bindings = aa.getBindings();
  } catch {
    return false;
  }
  const bound = bindings.filter((b) => MOUSE_MOTION.includes(b.keysym));
  for (const b of bound) aa.unbind(b.keysym);
  if (bound.length) aa.saveConfig();
  try {
    window.localStorage.setItem(MOUSE_CAMERA_DONE, "1");
  } catch {
    // as above
  }
  if (bound.length) console.info("keys: mouse motion no longer moves the camera");
  return bound.length > 0;
}

// an action and its words: `say` the key, `words` the text in the page's language at the moment
// it is read (a getter, so a list made once follows a change of language)
export interface ActionWords {
  action: string;
  say: PlainKey;
  readonly words: string;
}

export function actionSay(action: string, say: PlainKey): ActionWords {
  return {
    action,
    say,
    get words() {
      return t(say);
    }
  };
}

// what the keys line names, in the game's words (language/english_base.txt
// input_*_text), in this order
export const HINT_ACTIONS: ActionWords[] = [
  actionSay("CYCLE_TURN_LEFT", "game.keys.turnLeft"),
  actionSay("CYCLE_TURN_RIGHT", "game.keys.turnRight"),
  actionSay("GLANCE_LEFT", "game.keys.glanceLeft"),
  actionSay("GLANCE_RIGHT", "game.keys.glanceRight"),
  actionSay("GLANCE_BACK", "game.keys.glanceBack"),
  actionSay("CYCLE_BRAKE", "game.keys.brake"),
  actionSay("INGAME_MENU", "game.keys.menu")
];

// a key as the keys line shows it: the game's face is ASCII only, so the arrows are words. The
// names stay English (a keyboard's own labels; KeyCaps draws the arrows by these names)
export function keyLabel(b: AaBinding): string {
  const code = b.code ?? "";
  if (/^Key[A-Z]$/.test(code)) return code.slice(3);
  if (/^Digit\d$/.test(code)) return code.slice(5);
  const named: Record<string, string> = {
    Semicolon: ";",
    Quote: "'",
    Comma: ",",
    Period: ".",
    Slash: "/",
    Space: "space",
    Escape: "esc",
    Enter: "enter",
    Tab: "tab",
    ArrowLeft: "left",
    ArrowRight: "right",
    ArrowUp: "up",
    ArrowDown: "down",
    ShiftLeft: "shift",
    ShiftRight: "shift",
    ControlLeft: "ctrl",
    ControlRight: "right ctrl",
    AltLeft: "alt",
    AltRight: "alt gr",
    Backquote: "`",
    Minus: "-",
    Equal: "=",
    BracketLeft: "[",
    BracketRight: "]",
    Backslash: "\\",
    Backspace: "backspace",
    PageUp: "pg up",
    PageDown: "pg dn",
    Home: "home",
    End: "end",
    Insert: "ins",
    Delete: "del",
    NumpadDecimal: "num .",
    NumpadEnter: "num enter",
    NumpadAdd: "num +",
    NumpadSubtract: "num -",
    NumpadMultiply: "num *",
    NumpadDivide: "num /"
  };
  if (/^Numpad\d$/.test(code)) return "num " + code.slice(6);
  return named[code] ?? (b.keyName || code).toLowerCase();
}

// The embed (a visitor in an X post, most of them new to the game) names only the arrows:
// left and right turn, down brakes. The template binds them so already
// (browser_keys.cfg); embedKeys binds any of them a player changed back, so what the hints
// say is true.
const EMBED_KEYS: Array<[code: string, action: string]> = [
  ["ArrowLeft", "CYCLE_TURN_LEFT"],
  ["ArrowRight", "CYCLE_TURN_RIGHT"],
  ["ArrowDown", "CYCLE_BRAKE"]
];

// what the embed's hints say: the two turn arrows as one group
function embedHints(): HintItem[] {
  return [
    { keys: ["left", "right"], words: t("game.keys.turn") },
    { keys: ["down"], words: t("game.keys.brake"), action: "CYCLE_BRAKE" }
  ];
}

// the connecting screen's two groups (the keys a newcomer needs first): the first key of each
// turn, and the brake
export function firstKeys(items: HintItem[]): HintItem[] {
  if (EMBED) return embedHints();
  const left = items.find((k) => k.action === "CYCLE_TURN_LEFT");
  const right = items.find((k) => k.action === "CYCLE_TURN_RIGHT");
  const brake = items.find((k) => k.action === "CYCLE_BRAKE");
  const out: HintItem[] = [];
  if (left && right) out.push({ keys: [left.keys[0], right.keys[0]], words: t("game.keys.turn") });
  if (brake) out.push({ keys: [brake.keys[0]], words: t("game.keys.brake"), action: "CYCLE_BRAKE" });
  return out;
}

export function embedKeys(aa: Aa): void {
  if (!EMBED) return;
  let bindings: AaBinding[];
  try {
    bindings = aa.getBindings();
  } catch {
    return;
  }
  let changed = false;
  for (const [code, action] of EMBED_KEYS) {
    if (bindings.some((b) => b.code === code && b.action === action && (b.player === 1 || b.player === 0))) continue;
    changed = aa.setBinding(action, 1, code) || changed;
  }
  if (changed) aa.saveConfig();
}

export interface HintItem {
  keys: string[];
  words: string; // in the page's language
  action?: string; // the game's action, when the item is one action's keys
}

// the arrows and the numpad are second ways (the template keeps the arrows, default.cfg
// puts glances and the camera on the numpad): named only when an action has nothing else
function secondWay(code: string): boolean {
  return code.startsWith("Arrow") || code.startsWith("Numpad");
}

// the keys line's items: each action with its keys (at most three), actions without a key
// left out
export function hintItems(aa: Aa): HintItem[] {
  if (EMBED) return embedHints();
  return keyItems(aa, HINT_ACTIONS, 3);
}

// any actions' keys the same way (the Training screen's groups, components/Training.tsx), at
// most `most` keys each
export function keyItems(aa: Aa, actions: Array<{ action: string; words: string }>, most: number): HintItem[] {
  let bindings: AaBinding[];
  try {
    bindings = aa.getBindings();
  } catch {
    return [];
  }
  return actions.map(({ action, words }) => {
    const all = bindings.filter((b) => b.action === action && (b.player === 1 || b.player === 0) && b.code).sort((a, b) => a.keysym - b.keysym);
    const first = all.filter((b) => !secondWay(b.code!));
    const keys = (first.length ? first : all).map(keyLabel).slice(0, most);
    return { keys, words, action };
  }).filter((item) => item.keys.length > 0);
}
