// The Settings sheet: every setting a player cares about, in seven tabs by task
// (PLAYER, CONTROLS, CAMERA, GRAPHICS, HUD & CHAT, SOUND, ADVANCED), in the Profile sheet's
// grammar widened to 880u: the plate (SETTINGS, FIND, PASTE), the tab row (the chat panel's
// tabs), the rows of the open tab, the foot (RESET <TAB>, COPY, ESC). Out of a game it hangs at
// the right edge like the Profile; in a game it takes the Esc menu's place on the left and moves to the right edge while CAMERA is open. Every change applies at once
// through the game's config (settings/store.ts) and is saved a second later; nothing is ever
// left unsaved, so closing is always safe.
//
// Keys: the sheet is data-aa-keys="page" (the client leaves it every key, API.md "Setup") and
// handles them here: Up / Down move between rows, Left / Right change the focused value (the
// desktop menus' habit), Enter acts, Delete resets a row to stock, [ and ] switch tabs, Ctrl+F or
// / finds, Esc steps back (a capture, the find, the paste view) and then closes. Hovering a row
// gives it the focus, as in the in-game menu.
import { forwardRef, Fragment, useCallback, useEffect, useImperativeHandle, useMemo, useRef, useState, type ReactNode } from "react";
import type { AaAction, AaBinding, AudioState } from "../aa/types";
import { isTextField, moveFocus } from "../keyboard";
import { keyLabel } from "../keys";
import { applyReview, applyUndo, copyText, isStock, readPaste, stockText, type Review, type Undo } from "../settings/cfg";
import { CLASSIC_START_VALUES, CLASSIC_VALUES, lookOf, MODERN_VALUES, noteLook } from "../settings/classicLook";
import { CLASSIC_HUD_VALUES, setHudStyle, useHudStyle } from "../settings/hudStyle";
import { CAMERA_PRESETS, LAYOUT_ACTIONS, LAYOUTS, layoutOf, presetOf, type Layout } from "../settings/presets";
import { ALL_ROWS, actionWords, INSTANT_CHATS, optionText, REFERENCE_SPEED, rowOf, pitchFromTilt, TAB_ROWS, TABS, tiltFromPitch, TOUCH_ROWS, TOUCH_TABS, type Option, type Placed, type Row, type SliderRow, type TabId } from "../settings/schema";
import { en, LANGS, setLang, t, useLang, type Lang, type PlainKey } from "../i18n";
import { touchPrimary } from "../compact";
import { leaveFullscreen } from "../fullscreen";
import { setTouchLayout, touchLayout, type TouchLayout } from "../touchLayout";

const TOUCH_LAYOUTS: Option[] = [
  { value: "standard", label: "settings.touch.standard" },
  { value: "keyboard", label: "settings.touch.keyboard" },
  { value: "alternating", label: "settings.touch.alternating" }
];

// a schema's options as the controls show them, in the player's language
const shownOptions = (options: Option[]) => options.map((o) => ({ value: o.value, label: optionText(o) }));

// the stock camera preset is named; the others are players' names, as they are
const presetName = (id: string, label: string) => (id === "browser" ? t("settings.camera.browser") : label);
import { unescapeValue, type SettingsStore } from "../settings/store";
import { Choice, clampTo, css, RowShell, Rig, Segment, Slider, Swatches, Switch, TextWell, type GlanceKeys, type RigChange, type RigValues } from "./settings/controls";
import { ColorText } from "./ColorInput";
import { EverySetting } from "./settings/every";
import { KeyCells, reservedKey, useKeyCapture, useRowNotes, type Capture } from "./settings/keys";
import { PasteView } from "./settings/paste";
import { SheetClose } from "./SheetClose";
import { privateRoomsEnabled } from "../platform/quickplay";
import { setChallenges, unblockChallenges, useDuelSettings } from "../platform/duels";

export const SHARED_COMPUTER_KEY = "rc.sharedComputer";
const TAB_KEY = "rc.settingsTab";

// the rows whose focus holds a glance on the preview
const GLANCE_ROWS = new Set(["rig-glance", "g-back", "g-rise", "g-pitch", "glance-copy", "smart-glance"]);

// the part of the camera course (SCENE_PLAYER_COURSE) that shows a row at its moment: steady
// straights for where the camera sits, S-bends for following turns, reversals for the 180
// recovery, a brake and a climb back to speed for the speed rows; the mixed loop otherwise
export const COURSE_MIXED = 1;
const COURSE_OF: Record<string, number> = {
  "rig-custom": 2,
  back: 2,
  rise: 2,
  pitch: 2,
  fov: 2,
  "rig-glance": 2,
  "g-back": 2,
  "g-rise": 2,
  "g-pitch": 2,
  "glance-copy": 2,
  turn: 3,
  "incam-turn": 3,
  "turn-180": 4,
  "back-speed": 5,
  "rise-speed": 5,
  "g-back-speed": 5,
  "g-rise-speed": 5,
  zoom: 5
};

export interface SettingsPreview {
  camera: boolean; // CAMERA is open: the scene shows the player's own camera
  glance: boolean; // a glance row has the focus: the preview glances
  course: number; // the part of the camera course to drive (SCENE_PLAYER_COURSE)
}

// the Auto quality ladder's steps (gBrowserQuality.cpp): which tier each one draws
const AUTO_STEPS: PlainKey[] = ["settings.opt.low", "settings.opt.low", "settings.opt.medium", "settings.opt.medium", "settings.opt.high", "settings.opt.high", "settings.opt.ultra"];
const TIER_WORDS: Record<string, PlainKey> = { "0": "settings.opt.auto", "1": "settings.opt.low", "2": "settings.opt.medium", "3": "settings.opt.high", "4": "settings.opt.ultra" };

export interface SettingsHandle {
  // Esc from outside (App): true when the sheet stepped back inside itself
  back(): boolean;
  // a key capture is waiting for its key: a press outside the sheet only cancels it
  capturing(): boolean;
}

interface SettingsProps {
  shown: boolean; // rendered (outlives `open` by the shrink)
  open: boolean;
  inGame: boolean;
  store: SettingsStore;
  name: string;
  onName(value: string): string;
  nameRefused?: { name: string; at: number } | null; // the last typed name the platform refused
  onColour(): void; // COLOR_*_1 changed: the page's --player follows
  audio: AudioState | null;
  onAudio(): void;
  startTab?: TabId | null; // the tab to open on
  onTab?(tab: TabId): void;
  onPreview?(preview: SettingsPreview): void;
  onAccount(): void;
  onPrivacy(): void;
  onClose(): void;
}

// a touch device keeps six tabs and the rows a phone player uses (schema.ts TOUCH_TABS, TOUCH_ROWS)
const TOUCH = touchPrimary();
const SHOWN_TABS = TOUCH ? TABS.filter((t) => TOUCH_TABS.includes(t.id)) : TABS;
const shownTab = (t: TabId | null | undefined): TabId | null => (t && SHOWN_TABS.some((x) => x.id === t) ? t : null);

function readTab(): TabId | null {
  try {
    const t = window.localStorage.getItem(TAB_KEY);
    return TABS.some((x) => x.id === t) ? (t as TabId) : null;
  } catch {
    return null;
  }
}

function writeTab(tab: TabId) {
  try {
    window.localStorage.setItem(TAB_KEY, tab);
  } catch {
    // blocked storage: the sheet opens on PLAYER next time
  }
}

// FIND: every word of the query in the row's words, its group, its tab (in the player's language
// and in English, so a veteran's English words find it anywhere), its config names, its keywords
function haystack(p: Placed): string {
  const row = p.row;
  const names = "name" in row && typeof row.name === "string" ? row.name : row.kind === "keys" ? row.action : "";
  const tab = TABS.find((x) => x.id === p.tab)?.title;
  const words = [row.label, p.group.head, tab].flatMap((k) => (k ? [t(k), en(k)] : []));
  return [...words, names, ...(row.keywords ?? [])].join(" ").toLocaleLowerCase().replace(/_/g, " ");
}

function matches(p: Placed, q: string): boolean {
  const words = q.toLocaleLowerCase().replace(/_/g, " ").split(/\s+/).filter(Boolean);
  if (!words.length) return false;
  const h = haystack(p);
  return words.every((w) => h.includes(w));
}

export const Settings = forwardRef<SettingsHandle, SettingsProps>(function Settings(props, ref) {
  const { shown, open, inGame, store, name, onName, onColour, audio, onAudio, startTab, onTab, onPreview, onAccount, onPrivacy, onClose } = props;
  // 1v1 challenges: the platform's, read once the sheet is open where it has duels
  const duelsOn = privateRoomsEnabled();
  const duelSettings = useDuelSettings(open && duelsOn);
  const [duelBusy, setDuelBusy] = useState(false);
  const duelChange = (run: () => Promise<unknown>) => {
    if (duelBusy) return;
    setDuelBusy(true);
    run().catch((e: unknown) => console.info("duel settings: " + String(e instanceof Error ? e.message : e))).finally(() => setDuelBusy(false));
  };
  // re-renders the touch buttons' row after a change (a page setting the store does not track)
  const [, setLayoutSeen] = useState(0);
  const [tab, setTabState] = useState<TabId>(shownTab(startTab) ?? shownTab(readTab()) ?? "player");
  const [view, setView] = useState<"tab" | "paste">("tab");
  const [query, setQuery] = useState("");
  const [everyQuery, setEveryQuery] = useState("");
  const [pasteText, setPasteText] = useState("");
  const [groupOn, setGroupOn] = useState<Record<string, boolean>>({});
  const [undo, setUndo] = useState<Undo | null>(null);
  const [capture, setCapture] = useState<Capture | null>(null);
  const [confirm, setConfirm] = useState<"tab" | "all" | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);
  const [moreOpen, setMoreOpen] = useState(false);
  const [instantOpen, setInstantOpen] = useState(false);
  const [layoutPick, setLayoutPick] = useState<string | null>(null);
  const [tierFrom, setTierFrom] = useState<PlainKey | string | null>(null);
  const [focusRow, setFocusRow] = useState<string | null>(null);
  const [fullscreen, setFullscreen] = useState(!!document.fullscreenElement);
  const [notes, setNote] = useRowNotes();
  const sheet = useRef<HTMLElement>(null);
  const findRef = useRef<HTMLInputElement>(null);
  const fileRef = useRef<HTMLInputElement>(null);

  const v = store.version;
  // the paste review's words are made in its memo: made again in a new language
  const lang = useLang();
  const bindings = useMemo<AaBinding[]>(() => store.bindings(), [store, v]);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  const actions = useMemo<AaAction[]>(() => store.actions(), [store.actions]);
  // ADVANCED: the player's settings (what user.cfg keeps, and the rows' items), not the rooms' rules
  const everyItems = useMemo(() => {
    if (tab !== "advanced" && !query) return [];
    return store
      .list()
      .filter((i) => i.savable || rowOf(i.name))
      .sort((a, b) => a.name.localeCompare(b.name));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [store, tab, query, v]);

  const setTab = useCallback(
    (next: TabId, focus = true) => {
      setTabState(next);
      setView("tab");
      setQuery("");
      setConfirm(null);
      setLayoutPick(null);
      setNotice(null);
      // a tab opens at its top (the last one's scroll would hide its first rows)
      if (bodyRef.current) bodyRef.current.scrollTop = 0;
      writeTab(next);
      if (focus) window.requestAnimationFrame(() => sheet.current?.querySelector<HTMLElement>(".st-tab.is-on")?.focus({ preventScroll: true }));
    },
    []
  );

  useEffect(() => onTab?.(tab), [tab, onTab]);

  // the live preview: CAMERA open puts the player's own camera on the scene, a
  // focused glance row holds a glance; App turns these into the client's settings
  const previewCamera = shown && open && view === "tab" && !query && tab === "camera";
  const previewGlance = previewCamera && !!focusRow && GLANCE_ROWS.has(focusRow);
  const previewCourse = previewCamera ? (focusRow ? COURSE_OF[focusRow] ?? COURSE_MIXED : COURSE_MIXED) : 0;
  useEffect(() => onPreview?.({ camera: previewCamera, glance: previewGlance, course: previewCourse }), [previewCamera, previewGlance, previewCourse, onPreview]);

  // the asked-for tab, whenever the sheet opens with one
  useEffect(() => {
    if (!shown) return;
    if (shownTab(startTab)) setTabState(startTab as TabId);
  }, [shown, startTab]);

  // opening: the focus on the open tab (the paste view's text when it opens on one)
  useEffect(() => {
    if (!shown) {
      setCapture(null);
      setConfirm(null);
      setUndo(null);
      setNotice(null);
      return;
    }
    const id = window.requestAnimationFrame(() => {
      const first = sheet.current?.querySelector<HTMLElement>(view === "paste" ? "#settings-paste-text" : ".st-tab.is-on");
      first?.focus({ preventScroll: true });
    });
    return () => window.cancelAnimationFrame(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown]);

  useEffect(() => {
    const on = () => setFullscreen(!!document.fullscreenElement);
    document.addEventListener("fullscreenchange", on);
    return () => document.removeEventListener("fullscreenchange", on);
  }, []);

  // a body taller than the sheet scrolls; while more is below the foot draws its edge
  const bodyRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const body = bodyRef.current;
    if (!body || !shown) return;
    const mark = () => body.toggleAttribute("data-more", body.scrollTop + body.clientHeight < body.scrollHeight - 2);
    mark();
    const watch = new ResizeObserver(mark);
    watch.observe(body);
    for (const child of Array.from(body.children)) watch.observe(child);
    body.addEventListener("scroll", mark, { passive: true });
    return () => {
      watch.disconnect();
      body.removeEventListener("scroll", mark);
    };
  }, [shown, tab, view, query, moreOpen, instantOpen]);

  // ---- values -------------------------------------------------------------------------------

  const tabTitle = (id: TabId) => {
    const k = TABS.find((x) => x.id === id)?.title;
    return k ? t(k) : id;
  };

  const value = (n: string) => store.get(n).value;
  const hudStyle = useHudStyle();

  const ensureCustom = () => {
    const q = value("BROWSER_QUALITY");
    if (q === "-1" || q === "") return;
    const step = q === "0" ? AUTO_STEPS[Number(value("BROWSER_QUALITY_LEVEL"))] ?? "settings.opt.auto" : TIER_WORDS[q];
    setTierFrom(step ?? q);
    store.set("BROWSER_QUALITY", "-1");
  };

  const setRow = (row: Row, text: string) => {
    if (!("name" in row) || typeof row.name !== "string") return;
    if (row.tier) ensureCustom();
    if (row.name === "BROWSER_QUALITY") setTierFrom(null);
    store.set(row.name, text);
  };

  const resetRow = (row: Row) => {
    const stock = stockText(row);
    if (stock !== null) setRow(row, stock);
  };

  // a row the quality tier writes is the tier's, not the player's, until the quality is CUSTOM
  const tierOwns = (row: Row) => !!row.tier && value("BROWSER_QUALITY") !== "-1";
  const changedRow = (row: Row, val: string) => !tierOwns(row) && !isStock(row, val);

  const greyed = (row: Row): string | null => {
    if ("name" in row && typeof row.name === "string") {
      const item = store.get(row.name);
      if (!item.known) return t("settings.note.notInBuild");
      if (!item.writable) return t("settings.note.serverSets");
    }
    if (row.needs && value(row.needs.name) !== row.needs.value) return t(row.needs.note);
    return null;
  };

  // ---- keys ---------------------------------------------------------------------------------

  const boundCodes = (action: string) => bindings.filter((b) => b.action === action && b.player === 1).map((b) => b.code ?? String(b.keysym));

  const bindKey = (c: Capture, e: KeyboardEvent) => {
    const rowId = KEY_ROW_ID(c.action);
    const reserved = reservedKey(e);
    if (reserved) return setNote(rowId, { text: reserved });
    const keysym = store.keysymOf(e.code);
    if (!keysym || keysym < 0) return setNote(rowId, { text: t("settings.keys.noKey") });
    const was = bindings.find((b) => b.keysym === keysym) ?? null;
    if (was && was.action === c.action && was.player === c.player) return;
    if (c.replace !== null) store.unbind(c.replace);
    if (!store.bind(c.action, c.player, keysym)) return setNote(rowId, { text: t("settings.keys.cantBind") });
    if (was) {
      const replaced = c.replace;
      setNote(rowId, {
        text: t("settings.keys.was", { action: actionWords(was.action) }),
        undo: () => {
          store.bind(was.action, was.player, keysym);
          if (replaced !== null) store.bind(c.action, c.player, replaced);
          setNote(rowId, null);
        }
      });
    }
  };

  useKeyCapture(capture, (e) => {
    const c = capture;
    setCapture(null);
    if (c && e) bindKey(c, e);
    // the focus back on the row's last cell (+ ADD KEY, else the newest chip), unless a press
    // elsewhere (which cancelled it) put the focus there
    window.requestAnimationFrame(() => {
      if (!c) return;
      const row = sheet.current?.querySelector<HTMLElement>(`#st-${KEY_ROW_ID(c.action)}`);
      const here = document.activeElement;
      if (!row || (here && here !== document.body && !row.contains(here))) return;
      const cells = row.querySelectorAll<HTMLElement>(".st-keycell");
      if (cells.length) cells[cells.length - 1].focus({ preventScroll: true });
    });
  });

  const applyLayout = (l: Layout) => {
    for (const a of LAYOUT_ACTIONS) for (const b of bindings) if (b.action === a && b.player === 1) store.unbind(b.keysym);
    for (const a of LAYOUT_ACTIONS) for (const code of l.keys[a] ?? []) store.bind(a, 1, code);
    setLayoutPick(null);
    setNotice(t("settings.notice.keys", { layout: t(l.label), say: t(l.say) }));
  };

  // ---- the camera -------------------------------------------------------------------------

  const rigValues = (prefix: "CAMERA_CUSTOM" | "CAMERA_GLANCE"): RigValues => ({
    back: Number(value(prefix + "_BACK")) || 0,
    rise: Number(value(prefix + "_RISE")) || 0,
    tilt: tiltFromPitch(value(prefix + "_PITCH"))
  });

  const setRig = (prefix: "CAMERA_CUSTOM" | "CAMERA_GLANCE", next: RigChange) => {
    const out: Array<[string, string]> = [];
    if (next.backSpeed !== undefined) out.push([prefix + "_BACK_FROMSPEED", String(next.backSpeed)]);
    if (next.riseSpeed !== undefined) out.push([prefix + "_RISE_FROMSPEED", String(next.riseSpeed)]);
    if (next.back !== undefined) out.push([prefix + "_BACK", String(next.back)]);
    if (next.rise !== undefined) out.push([prefix + "_RISE", String(next.rise)]);
    if (next.tilt !== undefined) out.push([prefix + "_PITCH", pitchFromTilt(next.tilt)]);
    store.setMany(out);
  };

  // where the camera is at speed: the _FROMSPEED rows times the stock cycle speed added on
  const rigAtSpeed = (prefix: "CAMERA_CUSTOM" | "CAMERA_GLANCE"): RigValues => {
    const v = rigValues(prefix);
    return {
      back: v.back + (Number(value(prefix + "_BACK_FROMSPEED")) || 0) * REFERENCE_SPEED,
      rise: v.rise + (Number(value(prefix + "_RISE_FROMSPEED")) || 0) * REFERENCE_SPEED,
      tilt: v.tilt
    };
  };

  const rigFocus = (glance: boolean): "back" | "rise" | "tilt" | "speed" | null => {
    const map: Record<string, "back" | "rise" | "tilt" | "speed"> = glance
      ? { "g-back": "back", "g-rise": "rise", "g-pitch": "tilt", "g-back-speed": "speed", "g-rise-speed": "speed" }
      : { back: "back", rise: "rise", pitch: "tilt", "back-speed": "speed", "rise-speed": "speed" };
    return focusRow ? map[focusRow] ?? null : null;
  };

  // the glance keys, named in the glance rig's plan
  const glanceKeys = useMemo<GlanceKeys>(() => {
    const name = (action: string) => {
      const b = bindings.find((x) => x.action === action && x.player === 1);
      return b ? keyLabel(b) : null;
    };
    return { left: name("GLANCE_LEFT"), right: name("GLANCE_RIGHT"), back: name("GLANCE_BACK") };
  }, [bindings]);

  // ---- copy, paste, reset --------------------------------------------------------------------

  const keyName = (keysym: number) => keyLabel({ keysym, code: store.codeOf(keysym), keyName: String(keysym), action: "", player: 1 });

  const review = useMemo<Review | null>(() => {
    if (view !== "paste" || !pasteText.trim()) return null;
    const r = readPaste(pasteText, { get: (n) => store.get(n), bindings, actions, keyName });
    for (const g of r.groups) if (g.id in groupOn) g.on = groupOn[g.id];
    return r;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [view, pasteText, groupOn, v, actions, lang]);

  const openPaste = useCallback((text: string) => {
    setPasteText(text);
    setGroupOn({});
    setView("paste");
    setQuery("");
    setNotice(null);
    window.requestAnimationFrame(() => sheet.current?.querySelector<HTMLElement>("#settings-paste-text")?.focus({ preventScroll: true }));
  }, []);

  const apply = () => {
    if (!review) return;
    const count = review.groups.reduce((n, g) => n + (g.on ? g.changes.length : 0), 0);
    const u = applyReview(review, store);
    if (review.groups.some((g) => g.on && g.id === "identity")) {
      onColour();
      const pastedName = review.groups.find((g) => g.id === "identity")?.changes.find((c) => c.name === "PLAYER_1");
      if (pastedName) onName(unescapeValue(pastedName.value));
    }
    setUndo(u);
    setNotice(t("settings.notice.applied", { n: count }));
    setView("tab");
  };

  // Ctrl+V anywhere in the sheet outside a text field, and a .cfg dropped on it
  useEffect(() => {
    if (!shown) return;
    const paste = (e: ClipboardEvent) => {
      if (isTextField(e.target) || !sheet.current?.contains(document.activeElement)) return;
      const text = e.clipboardData?.getData("text/plain") ?? "";
      if (!text.trim()) return;
      e.preventDefault();
      openPaste(text);
    };
    window.addEventListener("paste", paste);
    return () => window.removeEventListener("paste", paste);
  }, [shown, openPaste]);

  const readFile = (file: File | null | undefined) => {
    if (!file) return;
    if (file.size > 64 * 1024) return setNotice(t("settings.notice.tooBig"));
    void file.text().then(openPaste);
  };

  const copy = async () => {
    const text = view === "paste" ? pasteText : copyText(tab, store);
    try {
      await navigator.clipboard.writeText(text);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2000);
    } catch {
      setNotice(t("settings.notice.clipboard"));
    }
  };

  const exportFile = () => {
    const blob = new Blob([store.exportText()], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "user.cfg";
    a.click();
    window.setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };

  const resetTab = (which: TabId) => {
    const values: Array<[string, string]> = [];
    for (const p of ALL_ROWS) {
      if (p.tab !== which || p.row.kind === "name" || p.row.kind === "colour") continue;
      const stock = stockText(p.row);
      if (stock !== null && "name" in p.row && typeof p.row.name === "string") values.push([p.row.name, stock]);
    }
    if (which === "graphics") {
      // the tier owns its rows again
      setTierFrom(null);
      const others = values.filter(([n]) => !ALL_ROWS.some((p) => p.row.tier && "name" in p.row && p.row.name === n));
      store.setMany(others);
    } else if (values.length) store.setMany(values);
    if (which === "controls") applyLayout(LAYOUTS[0]);
    if (which === "camera") store.setMany(Object.entries(CAMERA_PRESETS[0].values));
  };

  const resetNow = () => {
    if (confirm === "all") for (const t of SHOWN_TABS) resetTab(t.id);
    else resetTab(tab);
    setConfirm(null);
    setNotice(confirm === "all" ? t("settings.notice.allStock") : t("settings.notice.backToStock", { tab: tabTitle(tab) }));
  };

  // ---- Esc and the keys ------------------------------------------------------------------------

  const back = useCallback((): boolean => {
    if (capture) {
      setCapture(null);
      return true;
    }
    if (confirm) {
      setConfirm(null);
      return true;
    }
    if (query) {
      setQuery("");
      findRef.current?.focus({ preventScroll: true });
      return true;
    }
    if (view === "paste") {
      setView("tab");
      window.requestAnimationFrame(() => sheet.current?.querySelector<HTMLElement>(".st-tab.is-on")?.focus({ preventScroll: true }));
      return true;
    }
    return false;
  }, [capture, confirm, query, view]);

  useImperativeHandle(ref, () => ({ back, capturing: () => capture !== null }), [back, capture]);

  const nav = (step: number) => {
    const root = sheet.current;
    if (!root) return;
    const all = Array.from(root.querySelectorAll<HTMLElement>("[data-nav]")).filter((el) => !(el as HTMLButtonElement).disabled && el.getAttribute("aria-disabled") !== "true" && el.offsetParent !== null);
    if (!all.length) return;
    const active = document.activeElement as HTMLElement | null;
    let at = active ? all.indexOf(active) : -1;
    if (at < 0 && active) {
      const row = active.closest("[data-row]");
      at = row ? all.findIndex((el) => el.closest("[data-row]") === row) : -1;
    }
    const next = all[at < 0 ? (step > 0 ? 0 : all.length - 1) : Math.max(0, Math.min(all.length - 1, at + step))];
    next.focus({ preventScroll: true });
    (next.closest<HTMLElement>("[data-row], .st-paste-group") ?? next).scrollIntoView?.({ block: "nearest" });
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLElement>) => {
    if (e.defaultPrevented || capture) return;
    const t = e.target as HTMLElement;
    const text = isTextField(t);
    const tabButton = t.classList.contains("st-tab");
    if (e.key === "Escape") {
      // the sheet's own: it must not reach the page's handler on the window as well (the sheet
      // is closed by then, and Esc there means something else)
      e.preventDefault();
      e.stopPropagation();
      if (!back()) onClose();
      return;
    }
    if (e.key === "Tab") {
      e.preventDefault();
      moveFocus(e.shiftKey, sheet.current);
      return;
    }
    if ((e.key === "f" && (e.ctrlKey || e.metaKey)) || (e.key === "/" && !text)) {
      e.preventDefault();
      findRef.current?.focus({ preventScroll: true });
      findRef.current?.select();
      return;
    }
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      if (t.tagName === "TEXTAREA") return;
      e.preventDefault();
      nav(e.key === "ArrowDown" ? 1 : -1);
      return;
    }
    if ((tabButton && (e.key === "ArrowLeft" || e.key === "ArrowRight")) || (!text && (e.key === "[" || e.key === "]" || (e.ctrlKey && (e.key === "PageUp" || e.key === "PageDown"))))) {
      e.preventDefault();
      const at = SHOWN_TABS.findIndex((x) => x.id === tab);
      const step = e.key === "ArrowRight" || e.key === "]" || e.key === "PageDown" ? 1 : -1;
      setTab(SHOWN_TABS[(at + step + SHOWN_TABS.length) % SHOWN_TABS.length].id, tabButton);
      return;
    }
    if (!text && (e.key === "Delete" || e.key === "Backspace")) {
      const rowId = t.closest<HTMLElement>("[data-row]")?.dataset.row;
      const placed = ALL_ROWS.find((p) => p.row.id === rowId);
      if (placed) {
        e.preventDefault();
        resetRow(placed.row);
      }
      return;
    }
    if (!text && (e.key === "Enter" || e.key === " ") && t.tagName === "BUTTON") {
      e.preventDefault();
      if (!(t as HTMLButtonElement).disabled) t.click();
    }
  };

  // hovering a row gives it the focus (unless a field is being typed in, or a drag is on)
  const hover = (e: React.MouseEvent<HTMLElement>) => {
    if (e.buttons) return;
    const el = (e.target as HTMLElement).closest<HTMLElement>("[data-row]")?.querySelector<HTMLElement>("[data-nav]") ?? (e.target as HTMLElement).closest<HTMLElement>(".st-tab, .st-foot button");
    if (!el || document.activeElement === el || el.contains(document.activeElement) || (el as HTMLButtonElement).disabled) return;
    if (isTextField(document.activeElement) || isTextField(el)) return;
    if (document.activeElement?.closest("[data-row]") === el.closest("[data-row]") && el.closest("[data-row]")) return;
    el.focus({ preventScroll: true });
  };

  // ---- rows ---------------------------------------------------------------------------------

  const renderRow = (p: Placed, where: string | null): ReactNode => {
    const row = p.row;
    if (row.showWhen && value(row.showWhen.name) !== row.showWhen.value) return null;
    // the game's own readouts: only under CLASSIC; MODERN draws its own
    if (row.classicHud && hudStyle !== "classic") return null;
    const off = greyed(row);
    const common = { id: row.id, label: t(row.label), hint: row.hint && t(row.hint), tab: where };
    switch (row.kind) {
      case "toggle": {
        const val = value(row.name);
        return (
          <RowShell {...common} key={row.id} disabled={!!off} note={off} changed={changedRow(row, val)} onReset={() => resetRow(row)}>
            <Switch id={row.id} on={val !== "0" && val !== ""} disabled={!!off} onChange={(on) => setRow(row, on ? "1" : "0")} />
          </RowShell>
        );
      }
      case "segment": {
        const val = value(row.name);
        let note = off;
        let options = row.options;
        if (row.name === "BROWSER_QUALITY") {
          options = row.options.filter((o) => o.value !== "-1" || val === "-1");
          if (!note && val === "0") {
            const step = AUTO_STEPS[Number(value("BROWSER_QUALITY_LEVEL"))];
            note = step ? t("settings.quality.now", { tier: t(step) }) : t("settings.quality.measuring");
          }
          if (!note && val === "-1" && tierFrom) note = t("settings.quality.from", { tier: tierFrom.startsWith("settings.") ? t(tierFrom as PlainKey) : tierFrom });
        }
        return (
          <RowShell {...common} key={row.id} disabled={!!off} note={note} changed={!note && changedRow(row, val)} onReset={() => resetRow(row)}>
            <Segment id={row.id} options={shownOptions(options)} value={val} disabled={!!off} onChange={(x) => setRow(row, x)} />
          </RowShell>
        );
      }
      case "choice": {
        const val = value(row.name);
        const known = row.options.some((o) => Number(o.value) === Number(val));
        const words = known ? undefined : row.name === "MAX_FPS" ? val + " fps" : undefined;
        return (
          <RowShell {...common} key={row.id} disabled={!!off} note={off} changed={!isStock(row, val)} onReset={() => resetRow(row)}>
            <Choice id={row.id} options={shownOptions(row.options)} value={val} words={words} disabled={!!off} onChange={(x) => setRow(row, x)} />
          </RowShell>
        );
      }
      case "slider":
        return sliderRow(row, off, where);
      case "text": {
        const val = unescapeValue(value(row.name));
        return (
          <RowShell {...common} key={row.id} disabled={!!off} note={off}>
            <TextWell id={row.id} value={val} max={row.max} disabled={!!off} onCommit={(x) => setRow(row, x)} />
          </RowShell>
        );
      }
      case "keys": {
        const n = notes[row.id];
        return (
          <RowShell
            {...common}
            key={row.id}
            note={n ? null : off}
            hint={
              n ? (
                <span className="st-keynote">
                  {n.text}
                  {n.undo && (
                    <button className="pf-row-act" type="button" onClick={n.undo}>
                      {t("settings.keys.undo")}
                    </button>
                  )}
                </span>
              ) : undefined
            }
          >
            <KeyCells id={row.id} action={row.action} player={row.player} bindings={bindings} capture={capture} onCapture={setCapture} onUnbind={(k) => store.unbind(k)} />
          </RowShell>
        );
      }
      default:
        return <Fragment key={row.id}>{special(p, where)}</Fragment>;
    }
  };

  const sliderRow = (row: SliderRow, off: string | null, where: string | null) => {
    const text = value(row.name);
    const n = row.fromGame ? row.fromGame(text) : Number(text);
    return (
      <RowShell id={row.id} key={row.id} label={t(row.label)} hint={row.hint && t(row.hint)} tab={where} disabled={!!off} note={off} changed={changedRow(row, text)} onReset={() => resetRow(row)}>
        <Slider id={row.id} row={row} value={n} disabled={!!off} onChange={(x) => setRow(row, row.toGame ? row.toGame(x) : String(x))} />
      </RowShell>
    );
  };

  const special = (p: Placed, where: string | null): ReactNode => {
    const row = p.row;
    const common = { id: row.id, label: t(row.label), hint: row.hint && t(row.hint), tab: where };
    switch (row.kind) {
      case "fps-now":
        return (
          <RowShell {...common} hint={inGame ? undefined : t("settings.row.fps-now.hint")}>
            <FpsNow store={store} />
          </RowShell>
        );
      case "name":
        return (
          <RowShell {...common} hint={props.nameRefused ? t("settings.row.name.refused", { name: props.nameRefused.name }) : common.hint}>
            <TextWell id={row.id} value={name} max={16} colors onCommit={(x) => onName(x)} />
          </RowShell>
        );
      case "language":
        return (
          <RowShell {...common}>
            <LanguageChoice id={row.id} />
          </RowShell>
        );
      case "colour": {
        const rgb: [number, number, number] = [Number(value("COLOR_R_1")) || 0, Number(value("COLOR_G_1")) || 0, Number(value("COLOR_B_1")) || 0];
        const set = (next: [number, number, number]) => {
          store.setMany([
            ["COLOR_R_1", String(next[0])],
            ["COLOR_G_1", String(next[1])],
            ["COLOR_B_1", String(next[2])]
          ]);
          onColour();
        };
        const channel = (i: 0 | 1 | 2, label: "red" | "green" | "blue") => (
          <RowShell id={"colour-" + label} key={label} label={t(`settings.colour.${label}`)}>
            <Slider
              id={"colour-" + label}
              row={{ min: 0, max: 15, step: 1, def: rgb[i], words: (x) => String(x) }}
              value={rgb[i]}
              onChange={(x) => {
                const next: [number, number, number] = [...rgb];
                next[i] = x;
                set(next);
              }}
            />
          </RowShell>
        );
        return (
          <div className="st-colour" key={row.id}>
            <RowShell {...common} wide>
              <Swatches id={row.id} rgb={rgb} onPick={set} />
            </RowShell>
            {channel(0, "red")}
            {channel(1, "green")}
            {channel(2, "blue")}
            <i className="st-colour-now" aria-hidden="true" style={{ background: css(rgb) }} />
          </div>
        );
      }
      case "touch-layout": {
        // a touch device's buttons (touchLayout.ts): a page setting, shown there alone
        if (!TOUCH) return null;
        const layout = touchLayout();
        return (
          <RowShell {...common} changed={layout !== "standard"} onReset={() => { setTouchLayout("standard"); setLayoutSeen((n) => n + 1); }}
            hint={layout === "keyboard" ? t("settings.touch.keyboard.hint") : layout === "alternating" ? t("settings.touch.alternating.hint") : t("settings.touch.standard.hint")}>
            <Segment id={row.id} options={shownOptions(TOUCH_LAYOUTS)} value={layout} onChange={(x) => { setTouchLayout(x as TouchLayout); setLayoutSeen((n) => n + 1); }} />
          </RowShell>
        );
      }
      case "shared": {
        const on = store.page(SHARED_COMPUTER_KEY) === "1";
        return (
          <RowShell {...common} changed={on} onReset={() => store.setPage(SHARED_COMPUTER_KEY, null)}>
            <Switch id={row.id} on={on} onChange={(x) => store.setPage(SHARED_COMPUTER_KEY, x ? "1" : null)} />
          </RowShell>
        );
      }
      case "duels": {
        // off: nobody can challenge me (I still can); CHANGED resets it to on
        if (!duelsOn) return null;
        const on = duelSettings?.acceptChallenges ?? true;
        return (
          <RowShell {...common} changed={!on} onReset={() => duelChange(() => setChallenges(true))}>
            <Switch id={row.id} on={on} disabled={!duelSettings || duelBusy} onChange={(x) => duelChange(() => setChallenges(x))} />
          </RowShell>
        );
      }
      case "duel-blocks": {
        // one row per player blocked from challenging me, newest first, UNBLOCK at its right; none: nothing
        if (!duelsOn || !duelSettings || duelSettings.blocked.length === 0) return null;
        return (
          <Fragment key={row.id}>
            {duelSettings.blocked.map((b, i) => (
              <RowShell
                key={b.userId}
                id={"duel-block-" + i}
                label={
                  <>
                    <span className="st-key-quiet">{t("settings.row.duelBlocks")}</span> <span className="st-key-name">{b.name}</span>
                  </>
                }
                tab={where}
              >
                <button className="st-act" id={"st-duel-block-" + i + "-ctl"} type="button" data-nav="" disabled={duelBusy} onClick={() => duelChange(() => unblockChallenges(b.userId))}>
                  {t("settings.act.unblock")}
                </button>
              </RowShell>
            ))}
          </Fragment>
        );
      }
      case "link-account":
      case "link-privacy":
        return (
          <RowShell {...common} hint={row.kind === "link-account" ? t("settings.row.account.hint") : t("settings.row.privacy.hint")}>
            <button className="st-act" id={"st-" + row.id + "-ctl"} type="button" data-nav="" onClick={row.kind === "link-account" ? onAccount : onPrivacy}>
              {row.kind === "link-account" ? t("settings.act.profile") : t("settings.act.friends")}
              <ArrowMark />
            </button>
          </RowShell>
        );
      case "layout": {
        const now = layoutOf(boundCodes);
        const pick = layoutPick ? LAYOUTS.find((l) => l.id === layoutPick) ?? null : null;
        const options = LAYOUTS.map((l) => ({ value: l.id, label: `${t(l.label)}  ${l.turns}` }));
        const shownId = pick?.id ?? now?.id ?? "";
        return (
          <RowShell
            {...common}
            hint={pick && pick.id !== now?.id ? t("settings.layout.pick.hint") : now ? undefined : t("settings.layout.own.hint")}
          >
            <div className="st-layout">
              <Choice
                id={row.id}
                options={options}
                value={shownId}
                words={shownId ? undefined : t("settings.opt.custom")}
                onChange={(x) => {
                  if (x === now?.id) return setLayoutPick(null);
                  setLayoutPick(x);
                }}
                onEnter={() => {
                  if (!pick || pick.id === now?.id) return false;
                  applyLayout(pick);
                  return true;
                }}
              />
              {pick && pick.id !== now?.id && (
                <button
                  className="st-act st-act-ink"
                  type="button"
                  onClick={() => applyLayout(pick)}
                  onKeyDown={(e) => e.key === "Enter" && (e.preventDefault(), applyLayout(pick))}
                >
                  {t("settings.layout.use")}
                </button>
              )}
            </div>
          </RowShell>
        );
      }
      case "camera-preset": {
        const presets = CAMERA_PRESETS;
        const now = presetOf<(typeof presets)[number]>(presets, value);
        return (
          <RowShell {...common}>
            <Choice
              id={row.id}
              options={presets.map((x) => ({ value: x.id, label: presetName(x.id, x.label) }))}
              value={now?.id ?? ""}
              words={now ? undefined : t("settings.opt.custom")}
              onChange={(x) => {
                const preset = presets.find((y) => y.id === x);
                if (preset) store.setMany(Object.entries(preset.values));
              }}
            />
          </RowShell>
        );
      }
      // the HUD style (settings/hudStyle.ts): a page choice; CLASSIC turns the game's readouts on
      case "hud-style":
        return (
          <RowShell {...common}>
            <Segment
              id={row.id}
              options={[
                { value: "modern", label: t("settings.opt.modern") },
                { value: "classic", label: t("settings.opt.classic") }
              ]}
              value={hudStyle}
              onChange={(x) => {
                const next = x === "classic" ? "classic" : "modern";
                if (next === hudStyle) return;
                if (next === "classic") store.setMany(Object.entries(CLASSIC_HUD_VALUES));
                setHudStyle(next);
              }}
            />
          </RowShell>
        );
      case "graphics-preset": {
        const now = lookOf(value);
        return (
          <RowShell {...common} note={now ? null : t("settings.opt.custom")}>
            <Segment
              id={row.id}
              options={[
                { value: "modern", label: t("settings.opt.modern") },
                { value: "classic", label: t("settings.opt.classic") }
              ]}
              value={now ?? ""}
              onChange={(x) => {
                const look = x === "classic" ? "classic" : "modern";
                store.setMany(Object.entries(look === "classic" ? { ...CLASSIC_VALUES, ...CLASSIC_START_VALUES } : MODERN_VALUES));
                noteLook(look);
              }}
            />
          </RowShell>
        );
      }
      case "rig-custom":
      case "rig-glance": {
        const glance = row.kind === "rig-glance";
        const prefix = glance ? "CAMERA_GLANCE" : "CAMERA_CUSTOM";
        const off = !store.get(prefix + "_BACK").writable;
        return (
          <RowShell
            {...common}
            wide
            note={off ? t("settings.note.serverSets") : null}
            hint={glance ? t("settings.row.rig-glance.hint") : undefined}
          >
            <Rig
              id={row.id}
              values={rigValues(prefix)}
              speed={rigAtSpeed(prefix)}
              stock={{ back: 6, rise: 4, tilt: 30 }}
              focus={rigFocus(glance)}
              fov={Number(value("START_FOV_1")) || 90}
              glance={glance}
              keys={glanceKeys}
              disabled={off}
              onChange={(x) => setRig(prefix, x)}
            />
          </RowShell>
        );
      }
      case "glance-copy":
        return (
          <RowShell {...common}>
            <button
              className="st-act"
              id={"st-" + row.id + "-ctl"}
              type="button"
              data-nav=""
              onClick={() => {
                const c = rigValues("CAMERA_CUSTOM");
                setRig("CAMERA_GLANCE", c);
                setNotice(t("settings.notice.glanceCopied"));
              }}
            >
              {t("settings.glance.copy")}
            </button>
          </RowShell>
        );
      case "fullscreen":
        return (
          <RowShell {...common}>
            <Switch
              id={row.id}
              on={fullscreen}
              onChange={(on) => {
                if (on) void document.documentElement.requestFullscreen?.().catch(() => setNotice(t("settings.notice.fullscreen")));
                else leaveFullscreen();
              }}
            />
          </RowShell>
        );
      case "audio":
        // sound off closes the device, which also reads "none"
        if ((audio !== "suspended" && audio !== "none") || value("SOUND_QUALITY") === "0") return null;
        return (
          <RowShell {...common} label={t("settings.audio.waiting")} hint={audio === "none" ? t("settings.audio.none.hint") : t("settings.audio.suspended.hint")} note={audio === "none" ? t("settings.audio.noDevice") : null}>
            {audio === "suspended" && (
              <button className="st-act st-act-ink" id={"st-" + row.id + "-ctl"} type="button" data-nav="" onClick={onAudio}>
                {t("settings.audio.on")}
              </button>
            )}
          </RowShell>
        );
      case "instant-chats": {
        if (p.tab === "controls") {
          return (
            <div className="st-instant" key={row.id}>
              <RowShell {...common}>
                <button className="st-act" id={"st-" + row.id + "-ctl"} type="button" data-nav="" aria-expanded={instantOpen} onClick={() => setInstantOpen((o) => !o)}>
                  {instantOpen ? t("settings.instant.hide") : t("settings.instant.show", { n: INSTANT_CHATS })}
                </button>
              </RowShell>
              {instantOpen && instantRows(false)}
            </div>
          );
        }
        return (
          <div className="st-instant" key={row.id}>
            <p className="st-group-say">{t("settings.instant.say")}</p>
            {instantRows(true)}
          </div>
        );
      }
      case "every-setting":
        return (
          <EverySetting
            key={row.id}
            items={everyItems}
            query={everyQuery}
            onQuery={setEveryQuery}
            onSet={(n, x) => {
              store.set(n, x);
              if (/^COLOR_[RGB]_1$/.test(n)) onColour();
            }}
          />
        );
      case "config-file":
        return (
          <div className="st-file" key={row.id}>
            <p className="st-group-say">{t("settings.file.say")}</p>
            <div className="st-file-acts">
              <button className="st-act" type="button" data-nav="" onClick={exportFile}>
                {t("settings.file.export")}
              </button>
              <button className="st-act" type="button" data-nav="" onClick={() => fileRef.current?.click()}>
                {t("settings.file.import")}
              </button>
              <button className="st-act" type="button" data-nav="" onClick={() => setConfirm("all")}>
                {confirm === "all" ? t("settings.file.resetAllSure") : t("settings.file.resetAll")}
              </button>
            </div>
            <input ref={fileRef} type="file" accept=".cfg,.txt,text/plain" hidden onChange={(e) => readFile(e.target.files?.[0])} />
          </div>
        );
      default:
        return null;
    }
  };

  const instantRows = (texts: boolean) =>
    Array.from({ length: INSTANT_CHATS }, (_, i) => {
      const n = i + 1;
      const stringName = "INSTANT_CHAT_STRING_1_" + n;
      const said = unescapeValue(value(stringName));
      const id = "instant-" + n;
      return (
        <RowShell
          key={id}
          id={id}
          label={
            texts ? (
              String(n)
            ) : (
              <>
                {n} · {said ? <ColorText text={said} /> : t("settings.instant.empty")}
              </>
            )
          }
        >
          <div className="st-instant-row">
            {texts && <TextWell id={id + "-text"} value={said} placeholder={t("settings.instant.empty")} colors onCommit={(x) => store.set(stringName, x)} />}
            <KeyCells id={id} action={"INSTANT_CHAT_" + n} player={1} bindings={bindings} capture={capture} onCapture={setCapture} onUnbind={(k) => store.unbind(k)} />
          </div>
        </RowShell>
      );
    });

  // ---- the views ----------------------------------------------------------------------------

  let body: ReactNode;
  if (view === "paste") {
    body = (
      <PasteView
        review={review}
        text={pasteText}
        onText={(t) => {
          setPasteText(t);
          setGroupOn({});
        }}
        onToggle={(g, on) => setGroupOn((o) => ({ ...o, [g]: on }))}
        onApply={apply}
      />
    );
  } else if (query.trim()) {
    const hits = ALL_ROWS.filter((p) => p.row.kind !== "every-setting" && p.row.kind !== "config-file" && p.row.kind !== "touch-layout" && matches(p, query));
    const curated = new Set(hits.flatMap((p) => ("name" in p.row && typeof p.row.name === "string" ? [p.row.name] : [])));
    const q = query.trim().toLowerCase().replace(/\s+/g, "_");
    const raw = q.length >= 3 ? everyItems.filter((i) => i.name.toLowerCase().includes(q) && !curated.has(i.name)).slice(0, 12) : [];
    body = (
      <div className="st-tabview st-found" id="settings-found">
        {hits.length === 0 && raw.length === 0 && <p className="pf-say st-none">{t("settings.find.none")}</p>}
        {hits.map((p) => renderRow(p, `${tabTitle(p.tab)}${p.group.head ? " · " + t(p.group.head) : ""}`))}
        {raw.length > 0 && (
          <section className="st-group">
            <h3 className="st-group-head">{t("settings.find.every")}</h3>
            <EverySetting items={raw} query="" onQuery={() => {}} onSet={(n, x) => store.set(n, x)} />
          </section>
        )}
      </div>
    );
  } else {
    body = (
      <div className="st-tabview" id={"settings-" + tab} role="tabpanel" aria-labelledby={"settings-tab-" + tab} key={tab}>
        {TAB_ROWS[tab].groups.map((g) => {
          const rows = g.rows.filter((r) => (TOUCH ? TOUCH_ROWS.has(r.id) : r.kind !== "touch-layout")).map((r) => renderRow({ tab, group: g, row: r }, null)).filter(Boolean);
          if (!rows.length) return null;
          if (g.more) {
            return (
              <section className="st-group" key={g.id}>
                <button className="st-more" type="button" data-nav="" aria-expanded={moreOpen} onClick={() => setMoreOpen((o) => !o)}>
                  <span>{g.head && t(g.head)}</span>
                  <span className="st-more-say">{moreOpen ? t("settings.more.less") : t("settings.more.count", { n: g.rows.length })}</span>
                </button>
                {moreOpen && rows}
              </section>
            );
          }
          return (
            <section className="st-group" key={g.id} aria-label={g.head && t(g.head)}>
              {g.head && <h3 className="st-group-head">{t(g.head)}</h3>}
              {rows}
            </section>
          );
        })}
      </div>
    );
  }

  const changedHere = view === "tab" && !query && ALL_ROWS.some((p) => p.tab === tab && "name" in p.row && typeof p.row.name === "string" && !isStock(p.row, value(p.row.name)));

  return (
    <section
      ref={sheet}
      className={"pf st" + (TOUCH ? " is-touch" : "") + (open ? " is-open" : "") + (inGame ? " is-game" : "") + (inGame ? " is-right" : "")}
      id="settings"
      role="dialog"
      aria-modal="true"
      aria-labelledby="settings-title"
      data-tab={tab}
      data-view={query ? "find" : view}
      data-aa-keys="page"
      hidden={!shown}
      onKeyDown={onKeyDown}
      onMouseOver={hover}
      onFocus={(e) => setFocusRow((e.target as HTMLElement).closest<HTMLElement>("[data-row]")?.dataset.row ?? null)}
      onDragOver={(e) => e.preventDefault()}
      onDrop={(e) => {
        e.preventDefault();
        readFile(e.dataTransfer.files?.[0]);
      }}
    >
      <header className="pf-plate st-plate">
        <h2 className="fr-title st-title" id="settings-title">
          {t("settings.title")}
        </h2>
        {!TOUCH && (
        <div className="pf-well st-find">
          <label className="pf-key" htmlFor="settings-find">
            {t("settings.find")}
          </label>
          <input
            ref={findRef}
            className="pf-input"
            id="settings-find"
            value={query}
            spellCheck={false}
            autoComplete="off"
            placeholder={t("settings.find.placeholder")}
            onChange={(e) => {
              setQuery(e.target.value);
              setView("tab");
            }}
            onKeyDown={(e) => {
              if (e.key === "Enter" || e.key === "ArrowDown") {
                e.preventDefault();
                e.stopPropagation();
                nav(1);
              }
            }}
          />
        </div>
        )}
        {!TOUCH && (
        <button className="st-act st-paste-act" type="button" id="settings-paste-open" aria-pressed={view === "paste"} onClick={() => (view === "paste" ? setView("tab") : openPaste(""))}>
          {t("settings.import")}
        </button>
        )}
        <SheetClose label={t("settings.title")} onClose={onClose} />
      </header>

      <div className="st-tabs" role="tablist" aria-label={t("settings.title")}>
        {SHOWN_TABS.map((x) => {
          const on = x.id === tab && view === "tab" && !query;
          return (
            <button
              key={x.id}
              className={"st-tab" + (x.id === tab ? " is-on" : "") + (on ? "" : " is-away")}
              id={"settings-tab-" + x.id}
              type="button"
              role="tab"
              aria-selected={x.id === tab}
              aria-controls={"settings-" + x.id}
              tabIndex={x.id === tab ? 0 : -1}
              data-nav={x.id === tab ? "" : undefined}
              onClick={() => setTab(x.id)}
            >
              {t(x.title)}
            </button>
          );
        })}
      </div>

      <div className="pf-body st-body" ref={bodyRef}>
        <p className="pf-notice" id="settings-notice" role="status" hidden={!notice}>
          {notice || ""}
        </p>
        <div className="pf-swap" key={view + ":" + (query ? "find" : tab)}>
          {body}
        </div>
      </div>

      <footer className="st-foot">
        <div className="st-foot-acts">
          {view === "tab" && !query && tab !== "advanced" && !(TOUCH && tab === "controls") && (
            <button className="st-foot-act" type="button" disabled={!changedHere && confirm !== "tab" && tab !== "controls"} onClick={() => (confirm === "tab" ? resetNow() : setConfirm("tab"))}>
              {confirm === "tab" ? t("settings.foot.resetSure", { tab: tabTitle(tab) }) : t("settings.foot.reset", { tab: tabTitle(tab) })}
            </button>
          )}
          {confirm === "all" && (
            <button className="st-foot-act is-ink" type="button" onClick={resetNow}>
              {t("settings.foot.resetAll")}
            </button>
          )}
          {view === "tab" && !query && tab !== "advanced" && !TOUCH && (
            <button className="st-foot-act" type="button" onClick={() => void copy()}>
              {copied ? t("settings.foot.copied") : t("settings.foot.copy")}
            </button>
          )}
          {undo && (
            <button
              className="st-foot-act is-ink"
              type="button"
              onClick={() => {
                applyUndo(undo, store);
                onColour();
                setUndo(null);
                setNotice(t("settings.notice.undone"));
              }}
            >
              {t("settings.foot.undoImport")}
            </button>
          )}
        </div>
        {(capture || confirm || query || view === "paste") && (
          <button className="pf-foot st-esc" type="button" tabIndex={-1} onClick={() => back()}>
            <span className="pf-foot-key">{t("common.esc")}</span> {t("common.back")}
          </button>
        )}
      </footer>
    </section>
  );
});

// a key row's element id (the capture puts the focus back on it)
function KEY_ROW_ID(action: string): string {
  const m = /^INSTANT_CHAT_(\d+)$/.exec(action);
  if (m) return "instant-" + m[1];
  return ALL_ROWS.find((p) => p.row.kind === "keys" && p.row.action === action)?.row.id ?? action.toLowerCase();
}

// the client's frame rate, read twice a second while the row is up (the game's own counter is
// part of the HUD, which the attract scene hides); the Quality row under it says the tier
function FpsNow({ store }: { store: SettingsStore }) {
  const [fps, setFps] = useState<number | null>(() => store.fps());
  useEffect(() => {
    const t = window.setInterval(() => setFps(store.fps()), 500);
    return () => window.clearInterval(t);
  }, [store]);
  return (
    <span className="st-fps" id="st-fps-now-ctl" role="status" aria-live="off">
      <span className="st-fps-n">{fps === null ? "–" : Math.round(fps)}</span>
      <span className="st-fps-unit">fps</span>
    </span>
  );
}

// the page's language (i18n/index.ts): each named in itself; applies at once, kept as rc.lang
function LanguageChoice({ id }: { id: string }) {
  const lang = useLang();
  return <Choice id={id} options={LANGS.map((l) => ({ value: l.id, label: l.name }))} value={lang} onChange={(x) => void setLang(x as Lang)} />;
}

function ArrowMark() {
  return (
    <svg className="st-act-mark" viewBox="0 0 24 24" aria-hidden="true">
      <path d="M9 5 L16 12 L9 19" />
    </svg>
  );
}

// the slider's limits for a typed value
export const clampSetting = clampTo;
