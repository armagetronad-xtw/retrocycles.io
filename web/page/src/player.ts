// The player as the game knows them: PLAYER_1 (the name) and COLOR_R/G/B_1 (the cycle
// colour), read and written through the config API once the client is ready. There is
// no page-side copy that outlives the game's own config; the chip shows it and the
// Profile stage edits it.
import { useCallback, useEffect, useRef, useState } from "react";
import type { Aa } from "./aa/types";
import { checkName } from "./platform/client";

// the client's own default (web/wasm/browser_user.cfg): a browser still carrying it gets a
// name of its own on load (usePlayer), so a room is not full of "Browser"s
export const DEFAULT_NAME = "Browser";
export const NAME_MAX = 16;

// a new player's name: two words of the arena, at most 12 characters, so the game's 16 leave
// room for the 2..9 a room adds to tell two of the same name apart
const NAME_FIRST = [
  "Neon", "Retro", "Turbo", "Pixel", "Laser", "Hyper", "Cyber", "Quick", "Swift", "Static",
  "Vector", "Chrome", "Atomic", "Cosmic", "Sonic", "Polar", "Lunar", "Solar", "Rapid", "Silent",
  "Glitch", "Binary", "Arcade", "Electric", "Mega", "Nova", "Volt", "Ultra", "Zero", "Crimson",
  "Amber", "Cobalt", "Violet", "Jade", "Silver", "Midnight", "Phantom", "Iron", "Ghost", "Wild"
];
const NAME_SECOND = [
  "Rider", "Cycle", "Racer", "Comet", "Falcon", "Fox", "Lynx", "Wolf", "Hawk", "Viper",
  "Spark", "Pulse", "Byte", "Pilot", "Drift", "Blaze", "Orbit", "Raven", "Tiger", "Otter",
  "Panda", "Kite", "Arrow", "Bolt", "Flux", "Echo", "Rocket", "Dash", "Glider", "Jet",
  "Wheel", "Trail", "Grid", "Runner", "Nomad", "Ranger", "Mantis", "Moth", "Shark", "Cobra"
];

export function randomName(): string {
  const pick = (list: string[]) => list[Math.floor(Math.random() * list.length)];
  for (;;) {
    const name = pick(NAME_FIRST) + pick(NAME_SECOND);
    if (name.length <= 12) return name;
  }
}

// COLOR_*_1 are 0..15 (API.md: the cycle colour, r/15 etc.)
export function playerColor(aa: Aa): string {
  const part = (name: string) => {
    const item = aa.getConfig(name);
    const v = item ? Math.max(0, Math.min(15, Number(item.value) || 0)) : 15;
    return Math.round((v / 15) * 255)
      .toString(16)
      .padStart(2, "0");
  };
  return "#" + part("COLOR_R_1") + part("COLOR_G_1") + part("COLOR_B_1");
}

// getConfig's `value` is the item's WriteVal text, which escapes blanks and
// backslashes ("Next\ Kid"); the name as the player typed it
export function configText(value: string): string {
  return value.replace(/\\(.)/g, "$1");
}

export function cleanName(value: string): string {
  return value.trim().slice(0, NAME_MAX) || randomName();
}

export interface Player {
  name: string; // as the page shows it in plain text: the colour codes left out
  raw: string; // as the game has it, colour codes and all ("0xabab12Holing!!")
  // write PLAYER_1 (trimmed, capped, the default when empty) and save the config
  setName(value: string): string;
  // a name the player typed: shown at once, written once the platform allows it (without a
  // platform, or without its answer, at once: rooms check every name anyway)
  askName(value: string): string;
  // the last typed name the platform refused (the name is back to what it was)
  refused: { name: string; at: number } | null;
}

export function usePlayer(aa: Aa | null, apiUrl: string | null = null): Player {
  const [raw, setRaw] = useState(DEFAULT_NAME);
  const [refused, setRefused] = useState<{ name: string; at: number } | null>(null);
  const asking = useRef(0);

  useEffect(() => {
    if (!aa) return;
    const item = aa.getConfig("PLAYER_1");
    const value = item ? configText(item.value).trim() : "";
    if (item && (value === "" || value === DEFAULT_NAME)) {
      // a first visit (or a browser that never chose a name): a name of its own, kept
      const next = randomName();
      aa.setConfig("PLAYER_1", next);
      aa.saveConfig();
      setRaw(next);
    } else if (item && item.value) setRaw(configText(item.value).slice(0, NAME_MAX));
    document.documentElement.style.setProperty("--player", playerColor(aa));
  }, [aa]);

  const setName = useCallback(
    (value: string) => {
      const next = cleanName(value);
      setRaw(next);
      const item = aa ? aa.getConfig("PLAYER_1") : null;
      if (aa && (!item || configText(item.value) !== next)) {
        aa.setConfig("PLAYER_1", next);
        aa.saveConfig();
      }
      return next;
    },
    [aa]
  );

  const askName = useCallback(
    (value: string) => {
      const next = cleanName(value);
      const item = aa ? aa.getConfig("PLAYER_1") : null;
      const before = item ? configText(item.value) : raw;
      if (!aa || !apiUrl || before === next) return setName(value);
      setRaw(next);
      const ticket = ++asking.current;
      const write = () => {
        aa.setConfig("PLAYER_1", next);
        aa.saveConfig();
      };
      checkName(apiUrl, next).then(
        (answer) => {
          if (ticket !== asking.current) return; // another name was typed since
          if (answer.allowed) write();
          else {
            setRaw(before);
            setRefused({ name: aa.stripColors(next), at: Date.now() });
          }
        },
        () => {
          if (ticket === asking.current) write();
        }
      );
      return next;
    },
    [aa, apiUrl, raw, setName]
  );

  // the codes count toward the game's 16 characters, as in the desktop game
  const name = (aa ? aa.stripColors(raw) : raw.replace(/0x(?:[0-9a-fA-F]{6}|RESETT)/g, "")) || DEFAULT_NAME;
  return { name, raw, setName, askName, refused };
}
