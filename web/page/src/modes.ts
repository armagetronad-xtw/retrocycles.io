// The play modes and what the page knows about each:
// its title and the attract-scene state it indexes (API.md "The attract scene":
// mode k plays the scene to state k).
import { t, type PlainKey } from "./i18n";

export type Mode = "quick" | "ranked" | "browser" | "boards" | "local" | "custom";

export interface ModeInfo {
  mode: Mode;
  titleKey: PlainKey;
  readonly title: string; // in the current language (read at render)
  scene: 1 | 2 | 3 | 4;
  live: boolean; // can be played today
}

function info(mode: Mode, titleKey: PlainKey, scene: ModeInfo["scene"], live: boolean): ModeInfo {
  return {
    mode,
    titleKey,
    get title() {
      return t(titleKey);
    },
    scene,
    live
  };
}

// the order: Quick Play, Challenge a Friend, Leaderboards, Server Browser, Training
const ALL: ModeInfo[] = [
  info("quick", "home.mode.quick", 1, true),
  // Challenge a Friend: a private 1v1 sumo room, where the platform offers them
  // (features.privateRooms; setPrivateRooms below)
  info("custom", "home.mode.private", 4, true),
  info("ranked", "home.mode.ranked", 2, false),
  // the Leaderboards page: about the people playing
  info("boards", "home.mode.leaderboards", 3, true),
  info("browser", "home.mode.browser", 3, true),
  info("local", "home.mode.local", 4, true)
];

// the platform's last word on private rooms, kept by the browser: the line is there from the first
// paint (players found it arriving after the others awkward) unless the platform said no
// last time; its answer then adds or removes it
const PRIVATE_KEY = "rc.privateRooms";

function privateRoomsRemembered(): boolean {
  try {
    return localStorage.getItem(PRIVATE_KEY) !== "0";
  } catch {
    return true;
  }
}

export const MODES: ModeInfo[] = ALL.filter((m) => m.live && (m.mode !== "custom" || privateRoomsRemembered()));

// the Challenge a Friend line follows the platform's answer (features.privateRooms), second in the
// list. The index it was added at or removed from (the lines after it move by one), or -1 when
// nothing changed.
export function setPrivateRooms(on: boolean): number {
  try {
    localStorage.setItem(PRIVATE_KEY, on ? "1" : "0");
  } catch {
    /* the line is just not remembered */
  }
  const i = MODES.findIndex((m) => m.mode === "custom");
  const at = Math.min(1, MODES.length);
  if (on && i < 0) {
    MODES.splice(at, 0, ALL.find((m) => m.mode === "custom")!);
    return at;
  }
  if (!on && i >= 0) {
    MODES.splice(i, 1);
    return i;
  }
  return -1;
}

export function modeIndex(mode: Mode): number {
  return MODES.findIndex((m) => m.mode === mode);
}

export function modeInfo(mode: Mode): ModeInfo {
  return MODES[modeIndex(mode)];
}
