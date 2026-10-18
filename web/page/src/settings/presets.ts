// The presets: one press for a sane key layout or camera (the HUD has its style instead,
// settings/hudStyle.ts).
// The key layouts are the game's own templates (config/keys_*.cfg, web/wasm/browser_keys.cfg),
// player 1's keys only: the files also bind player 2 (split screen, not in the browser yet) and
// only ever add bindings, so the page applies them itself: it unbinds player 1's keys for the
// actions a layout covers, then binds the layout's. Keys are DOM KeyboardEvent.code values
// (the game binds physical keys, API.md setBinding).
import type { PlainKey } from "../i18n";

export interface Layout {
  id: string;
  label: PlainKey;
  // what the choice row says beside the name: the turn pattern over D F J K (no language changes it)
  turns: string;
  // the sentence once it is applied
  say: PlainKey;
  keys: Record<string, string[]>;
}

// the actions a layout sets; any other binding is left alone
export const LAYOUT_ACTIONS = ["CYCLE_TURN_LEFT", "CYCLE_TURN_RIGHT", "CYCLE_BRAKE", "GLANCE_LEFT", "GLANCE_RIGHT", "GLANCE_BACK", "SWITCH_VIEW"];

/* Two layouts for now, both on the home row's D F J K (the browser's template,
   web/wasm/browser_keys.cfg) and the arrows: STANDARD turns left with the left hand's two keys
   and right with the right hand's (LL RR), ALTERNATING turns left and right with each hand
   (LR LR), the way many desktop players bind two turn keys per hand. Brake, glances and view as
   the template has them. (The desktop game's templates, keys_*.cfg, bind player 2 as well and only
   add bindings, so they are not used as they are.) */
const HOME = {
  CYCLE_BRAKE: ["Space", "ArrowDown"],
  GLANCE_LEFT: ["KeyA", "Numpad0"],
  GLANCE_RIGHT: ["KeyS", "NumpadDecimal"],
  GLANCE_BACK: ["Semicolon"],
  SWITCH_VIEW: ["KeyV"]
};

export const LAYOUTS: Layout[] = [
  {
    id: "standard",
    label: "settings.layout.standard",
    turns: "LL RR",
    say: "settings.layout.standard.say",
    keys: { CYCLE_TURN_LEFT: ["KeyD", "KeyF", "ArrowLeft"], CYCLE_TURN_RIGHT: ["KeyJ", "KeyK", "ArrowRight"], ...HOME }
  },
  {
    id: "alternating",
    label: "settings.layout.alternating",
    turns: "LR LR",
    say: "settings.layout.alternating.say",
    keys: { CYCLE_TURN_LEFT: ["KeyD", "KeyJ", "ArrowLeft"], CYCLE_TURN_RIGHT: ["KeyF", "KeyK", "ArrowRight"], ...HOME }
  }
];

// the desktop templates, kept for reference (not offered): wasd, zqsd, arrows, traditional
export const OLD_LAYOUTS: Array<Omit<Layout, "label" | "say"> & { label: string; say: string }> = [
  {
    id: "browser",
    label: "browser",
    turns: "D F / J K",
    say: "",
    keys: {
      CYCLE_TURN_LEFT: ["KeyD", "KeyF", "ArrowLeft"],
      CYCLE_TURN_RIGHT: ["KeyJ", "KeyK", "ArrowRight"],
      CYCLE_BRAKE: ["Space", "ArrowDown"],
      GLANCE_LEFT: ["KeyA", "Numpad0"],
      GLANCE_RIGHT: ["KeyS", "NumpadDecimal"],
      GLANCE_BACK: ["Semicolon"],
      SWITCH_VIEW: ["KeyV"]
    }
  },
  {
    id: "wasd",
    label: "wasd",
    turns: "A / D",
    say: "",
    keys: {
      CYCLE_TURN_LEFT: ["KeyA", "KeyZ", "KeyY"],
      CYCLE_TURN_RIGHT: ["KeyD", "KeyX"],
      CYCLE_BRAKE: ["KeyS", "Space"],
      GLANCE_LEFT: ["ArrowLeft", "KeyQ"],
      GLANCE_RIGHT: ["ArrowRight", "KeyE"],
      GLANCE_BACK: ["ArrowDown", "KeyW"],
      SWITCH_VIEW: ["KeyV", "KeyC"]
    }
  },
  {
    id: "zqsd",
    label: "zqsd",
    turns: "Q / D",
    say: "",
    keys: {
      CYCLE_TURN_LEFT: ["KeyQ", "KeyW"],
      CYCLE_TURN_RIGHT: ["KeyD", "KeyX"],
      CYCLE_BRAKE: ["KeyS", "Space"],
      GLANCE_LEFT: ["ArrowLeft", "KeyA"],
      GLANCE_RIGHT: ["ArrowRight", "KeyE"],
      GLANCE_BACK: ["ArrowDown", "KeyZ"],
      SWITCH_VIEW: ["KeyV", "KeyC"]
    }
  },
  {
    id: "arrows",
    label: "arrows",
    turns: "LEFT / RIGHT",
    say: "",
    keys: {
      CYCLE_TURN_LEFT: ["ArrowLeft", "KeyZ", "KeyX", "KeyW"],
      CYCLE_TURN_RIGHT: ["ArrowRight", "KeyR", "KeyC"],
      CYCLE_BRAKE: ["ArrowDown", "KeyE", "Space"],
      GLANCE_LEFT: ["Numpad0", "KeyS"],
      GLANCE_RIGHT: ["NumpadDecimal", "KeyF"],
      GLANCE_BACK: ["KeyD"],
      SWITCH_VIEW: ["KeyV"]
    }
  },
  {
    id: "traditional",
    label: "traditional",
    turns: "LEFT U / RIGHT O",
    say: "",
    keys: {
      CYCLE_TURN_LEFT: ["ArrowLeft", "KeyU"],
      CYCLE_TURN_RIGHT: ["ArrowRight", "KeyO"],
      CYCLE_BRAKE: ["ArrowDown", "KeyI"],
      GLANCE_LEFT: ["KeyJ"],
      GLANCE_RIGHT: ["KeyL"],
      GLANCE_BACK: ["KeyK"],
      SWITCH_VIEW: ["KeyV", "KeyC"]
    }
  }
];

// which layout a key map is (its player-1 keys for the layout's actions, exactly), else null
export function layoutOf(bound: (action: string) => string[]): Layout | null {
  const same = (a: string[], b: string[]) => a.length === b.length && [...a].sort().join(" ") === [...b].sort().join(" ");
  return LAYOUTS.find((l) => LAYOUT_ACTIONS.every((a) => same(bound(a), l.keys[a] ?? []))) ?? null;
}

// ---- the camera ----------------------------------------------------------------------------

export interface CameraPreset {
  id: string;
  label: string;
  // the camera's settings as the game takes them (the pitch a slope); every preset sets the same
  // ones, so one never leaves another's behind
  values: Record<string, string>;
}

// the stock camera: what a fresh browser has (config/settings_visual.cfg) and what a preset
// leaves as it is unless it says otherwise
const STOCK_CAMERA: Record<string, string> = {
  CAMERA_CUSTOM_BACK: "6",
  CAMERA_CUSTOM_RISE: "4",
  CAMERA_CUSTOM_PITCH: "-0.58",
  CAMERA_CUSTOM_BACK_FROMSPEED: "0.5",
  CAMERA_CUSTOM_RISE_FROMSPEED: "0.4",
  CAMERA_CUSTOM_TURN_SPEED: "4",
  CAMERA_CUSTOM_TURN_SPEED_180: "4",
  CAMERA_GLANCE_BACK: "6",
  CAMERA_GLANCE_RISE: "4",
  CAMERA_GLANCE_PITCH: "-0.58",
  CAMERA_GLANCE_BACK_FROMSPEED: "0.5",
  CAMERA_GLANCE_RISE_FROMSPEED: "0.4",
  START_FOV_1: "90"
};

const camera = (id: string, values: Record<string, string> = {}): CameraPreset => ({ id, label: id, values: { ...STOCK_CAMERA, ...values } });

// BROWSER is the stock camera (RESET CAMERA's too); the others are players' own cameras, high and
// far back, as they gave them
export const CAMERA_PRESETS: CameraPreset[] = [
  camera("browser"),
  camera("koala", {
    CAMERA_CUSTOM_BACK: "20",
    CAMERA_CUSTOM_RISE: "19.5",
    CAMERA_CUSTOM_PITCH: "-0.75",
    CAMERA_GLANCE_BACK: "20",
    CAMERA_GLANCE_RISE: "19.5",
    CAMERA_GLANCE_PITCH: "-0.75",
    START_FOV_1: "85"
  }),
  camera("olive", {
    CAMERA_CUSTOM_BACK: "20",
    CAMERA_CUSTOM_RISE: "22",
    CAMERA_CUSTOM_PITCH: "-0.8",
    CAMERA_CUSTOM_BACK_FROMSPEED: "0",
    CAMERA_CUSTOM_RISE_FROMSPEED: "0",
    CAMERA_CUSTOM_TURN_SPEED: "5",
    CAMERA_GLANCE_BACK: "20",
    CAMERA_GLANCE_RISE: "22",
    CAMERA_GLANCE_PITCH: "-0.8",
    CAMERA_GLANCE_BACK_FROMSPEED: "0",
    CAMERA_GLANCE_RISE_FROMSPEED: "0"
  }),
  camera("kronkleberry", {
    CAMERA_CUSTOM_BACK: "18",
    CAMERA_CUSTOM_RISE: "18",
    CAMERA_CUSTOM_PITCH: "-0.7",
    CAMERA_CUSTOM_TURN_SPEED: "4.5",
    CAMERA_GLANCE_BACK: "18",
    CAMERA_GLANCE_RISE: "18",
    CAMERA_GLANCE_PITCH: "-0.7"
  })
];

// which preset a set of values is, else null (CUSTOM); numbers compared as numbers
export function presetOf<T extends { values: Record<string, string> }>(presets: T[], get: (name: string) => string): T | null {
  const same = (a: string, b: string) => (Number.isFinite(Number(a)) && Number.isFinite(Number(b)) ? Math.abs(Number(a) - Number(b)) < 1e-3 : a === b);
  return presets.find((p) => Object.entries(p.values).every(([n, v]) => same(get(n), v))) ?? null;
}
