// The settings the Settings sheet edits, read and written through the game's own
// config API (web/wasm/API.md "Functions"): the game is the store, the page keeps no copy that
// could disagree with it. A change applies at once (setConfig) and is saved a second after the
// last one (saveConfig writes user.cfg and the browser's autoexec list, then IndexedDB), and on
// close. Page-only settings (the master volume, the shared-computer choice) live in
// localStorage, read and written here too. `version` grows on every write so the sheet renders
// the new values.
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { Aa, AaAction, AaBinding, AaConfigItem } from "../aa/types";
import { noteEmbedSound } from "../embedSound";
import { noteArena } from "../arenaTest";
import { noteLookSetting } from "./classicLook";
import { noteFrameRateSetting } from "./hudStyle";

const SAVE_AFTER_MS = 1000;

// the items the attract scene forces off while it runs (API.md "The attract scene"): getConfig
// reads the scene's 0 then, the player's own value is in exportConfig(); and a setConfig during
// the scene is lost when it stops, so the scene is stopped for the write and started again
const SCENE_MASKED = new Set(["TEXT_OUT", "SHOW_HUD", "SHOW_FPS", "SHOW_TIME", "UPPER_SKY", "LOWER_SKY", "FLOOR_DETAIL"]);

function sceneState(aa: Aa): number {
  try {
    const st = aa.getState();
    return st.state === "scene" ? st.sceneState || 1 : 0;
  } catch {
    return 0;
  }
}

// the player's own value of a scene-masked item, from the export (which saves first: it is read
// once per version of the store, not on every render)
function ownValue(text: string, name: string): string | null {
  // user.cfg right-aligns the names: "            SHOW_HUD 1"
  const m = new RegExp("^[ \\t]*" + name + "[ \\t]+(.*)$", "m").exec(text);
  return m ? m[1].trim() : null;
}

export interface ConfigValue {
  value: string;
  writable: boolean; // false: the room sets it (a server-controlled item while connected)
  known: boolean; // the client has the item at all (an older bundle may not)
  savable: boolean; // written to user.cfg (the rest: the browser autoexec list, or lost on reload)
}

export interface SettingsStore {
  version: number;
  get(name: string): ConfigValue;
  set(name: string, value: string): boolean;
  // several at once (a preset, a paste), one save
  setMany(values: Array<[string, string]>): void;
  bindings(): AaBinding[];
  bind(action: string, player: number, code: string | number): boolean;
  unbind(key: string | number): boolean;
  console(line: string): void;
  page(key: string): string | null;
  setPage(key: string, value: string | null): void;
  touch(): void; // something outside changed the game's values (a key binding by INCLUDE)
  saveNow(): void;
  // the rest of the API the sheet reads (only App.tsx and the hooks talk to aa)
  actions(): AaAction[];
  list(): AaConfigItem[];
  exportText(): string;
  keysymOf(code: string): number;
  fps(): number | null; // the client's frame rate now (getState().fps)
  codeOf(keysym: number): string | null;
}

// getConfig's value is the item's WriteVal text: blanks and backslashes escaped ("Next\ Kid")
export function unescapeValue(value: string): string {
  return value.replace(/\\(.)/g, "$1");
}

function readPage(key: string): string | null {
  try {
    return window.localStorage.getItem(key);
  } catch {
    return null;
  }
}

function writePage(key: string, value: string | null) {
  try {
    if (value === null) window.localStorage.removeItem(key);
    else window.localStorage.setItem(key, value);
  } catch {
    // blocked storage: the setting lasts for this page load
  }
}

export function useSettingsStore(aa: Aa | null, onSaved?: () => void): SettingsStore {
  const [version, setVersion] = useState(0);
  const timer = useRef(0);
  const memo = useRef(new Map<string, string | null>()); // page settings when storage is blocked
  const exported = useRef<{ version: number; text: string } | null>(null);

  const saveNow = useCallback(() => {
    window.clearTimeout(timer.current);
    timer.current = 0;
    if (!aa) return;
    try {
      aa.saveConfig();
      onSaved?.();
    } catch (e) {
      console.info("settings: not saved: " + String(e));
    }
  }, [aa, onSaved]);

  const later = useCallback(() => {
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(saveNow, SAVE_AFTER_MS);
  }, [saveNow]);

  // a pending save goes out when the page goes away
  useEffect(() => {
    const flush = () => {
      if (timer.current) saveNow();
    };
    window.addEventListener("pagehide", flush);
    return () => {
      window.removeEventListener("pagehide", flush);
      flush();
    };
  }, [saveNow]);

  return useMemo<SettingsStore>(
    () => ({
      version,
      get(name) {
        const item = aa?.getConfig(name) ?? null;
        if (!item) return { value: "", writable: false, known: false, savable: false };
        let own: string | null = null;
        if (aa && SCENE_MASKED.has(name.toUpperCase()) && sceneState(aa)) {
          if (!exported.current || exported.current.version !== version) {
            let text = "";
            try {
              text = aa.exportConfig();
            } catch {
              text = "";
            }
            exported.current = { version, text };
          }
          own = ownValue(exported.current.text, name.toUpperCase());
        }
        return { value: own ?? item.value, writable: item.writable, known: true, savable: item.savable };
      },
      set(name, value) {
        if (!aa) return false;
        const item = aa.getConfig(name);
        if (!item || !item.writable) return false;
        const k = SCENE_MASKED.has(name.toUpperCase()) ? sceneState(aa) : 0;
        if (!k && item.value === value) return true;
        if (k) aa.sceneStop();
        const ok = aa.setConfig(name, value);
        if (ok) {
          noteEmbedSound(name, value);
          noteArena(name, value);
          noteLookSetting(name, value);
          noteFrameRateSetting(name, value);
        }
        if (k) aa.scene(k);
        setVersion((v) => v + 1);
        later();
        return ok;
      },
      setMany(values) {
        if (!aa) return;
        const k = values.some(([n]) => SCENE_MASKED.has(n.toUpperCase())) ? sceneState(aa) : 0;
        if (k) aa.sceneStop();
        for (const [name, value] of values) {
          const item = aa.getConfig(name);
          if (item && item.writable && item.value !== value && aa.setConfig(name, value)) {
            noteEmbedSound(name, value);
            noteArena(name, value);
          noteLookSetting(name, value);
            noteFrameRateSetting(name, value);
          }
        }
        if (k) aa.scene(k);
        setVersion((v) => v + 1);
        later();
      },
      bindings() {
        try {
          return aa ? aa.getBindings() : [];
        } catch {
          return [];
        }
      },
      bind(action, player, code) {
        if (!aa) return false;
        const ok = aa.setBinding(action, player, code);
        setVersion((v) => v + 1);
        later();
        return ok;
      },
      unbind(key) {
        if (!aa) return false;
        const ok = aa.unbind(key);
        setVersion((v) => v + 1);
        later();
        return ok;
      },
      console(line) {
        if (!aa) return;
        aa.console(line);
        setVersion((v) => v + 1);
        later();
      },
      page(key) {
        if (memo.current.has(key)) return memo.current.get(key) ?? null;
        return readPage(key);
      },
      setPage(key, value) {
        memo.current.set(key, value);
        writePage(key, value);
        setVersion((v) => v + 1);
      },
      touch() {
        setVersion((v) => v + 1);
        later();
      },
      saveNow,
      actions() {
        try {
          return aa ? aa.getActions() : [];
        } catch {
          return [];
        }
      },
      list() {
        try {
          return aa ? aa.listConfig("") : [];
        } catch {
          return [];
        }
      },
      exportText() {
        return aa ? aa.exportConfig() : "";
      },
      fps() {
        try {
          const f = aa ? aa.getState().fps : null;
          return typeof f === "number" && Number.isFinite(f) ? f : null;
        } catch {
          return null;
        }
      },
      keysymOf(code) {
        try {
          return aa ? aa.keysymFromCode(code) : 0;
        } catch {
          return 0;
        }
      },
      codeOf(keysym) {
        try {
          return aa ? aa.codeFromKeysym(keysym) : null;
        } catch {
          return null;
        }
      }
    }),
    [aa, version, later, saveNow]
  );
}
