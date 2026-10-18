// Settings as text: COPY gives a tab's changed settings as
// user.cfg lines, the format players already trade on the forums and in the desktop game; a
// paste is read here into a review (what would change, by tab, in the rows' own words, and what
// is refused and why) before anything applies. The page passes on only what is a client
// setting (an item getConfig knows, savable, not the room's) or a KEYBOARD line for a known
// action of player 1 or a global one; never a command (INCLUDE, SAY, CONNECT: anything getConfig
// does not know), a login (USER_*, PASSWORD*, ADMIN_*) or a server setting.
import type { AaAction, AaBinding } from "../aa/types";
import { ALL_ROWS, actionWords, optionText, rowNames, rowOf, tabOf, TABS, type Row, type TabId } from "./schema";
import type { SettingsStore } from "./store";
import { unescapeValue } from "./store";
import { en, num, t } from "../i18n";

export const PASTE_MAX = 64 * 1024;

// names a paste may never set, whatever getConfig says
const NEVER = /^(PASSWORD|USER_|ADMIN|LOCAL_USER|GLOBAL_ID|AUTO_LOGIN|PLAYER_[2-4]$|COLOR_[RGB]_[2-4]$|SERVER_|BROWSER_CONSOLE_LOG|TEXT_OUT_)/;

// ---- copy ------------------------------------------------------------------------------------

function sameValue(a: string, b: string): boolean {
  const x = Number(a);
  const y = Number(b);
  if (a !== "" && b !== "" && Number.isFinite(x) && Number.isFinite(y)) return Math.abs(x - y) < 1e-6;
  return unescapeValue(a) === unescapeValue(b);
}

// a row's stock value as the game writes it (the tilt and the percentages are stored otherwise)
export function stockText(row: Row): string | null {
  if (row.kind === "toggle" || row.kind === "segment" || row.kind === "choice" || row.kind === "text") return row.def;
  if (row.kind === "slider") return row.toGame ? row.toGame(row.def) : String(row.def);
  return null;
}

export function isStock(row: Row, value: string): boolean {
  if (row.kind === "slider") {
    const n = row.fromGame ? row.fromGame(value) : Number(value);
    return !Number.isFinite(n) || Math.abs(n - row.def) < Math.max(1e-6, row.step / 2);
  }
  const stock = stockText(row);
  return stock === null || sameValue(stock, value);
}

// the tab's changed settings as user.cfg lines; CONTROLS gives its KEYBOARD lines
export function copyText(tab: TabId, store: SettingsStore, when = new Date()): string {
  // a user.cfg comment: English, whatever the page's language (the file is the desktop game's)
  const titleKey = TABS.find((x) => x.id === tab)?.title;
  const title = titleKey ? en(titleKey) : tab;
  const lines: string[] = [`# Retrocycles ${title} settings, ${when.toISOString().slice(0, 10)}`];
  if (tab === "controls") {
    for (const b of store.bindings()) if (b.player <= 1) lines.push(`KEYBOARD\t${b.keysym}\tPLAYER_BIND\t${b.action}\t${b.player}`);
  } else {
    for (const p of ALL_ROWS) {
      if (p.tab !== tab) continue;
      for (const name of rowNames(p.row)) {
        const v = store.get(name);
        if (!v.known) continue;
        if (rowNames(p.row).length === 1 && stockText(p.row) !== null && isStock(p.row, v.value)) continue;
        lines.push(`${name} ${v.value}`);
      }
    }
  }
  return lines.join("\n") + "\n";
}

// ---- paste -----------------------------------------------------------------------------------

export interface Change {
  name: string; // the config name, or KEYBOARD <keysym>
  line: string; // what runs: "NAME value" through setConfig, or the KEYBOARD line through console
  label: string; // the row's words ("distance"), or the raw name
  from: string; // the value now, in words
  to: string; // the value pasted, in words
  value: string; // the raw value pasted
  keysym?: number;
}

export interface Group {
  id: string; // a tab id, or "identity" / "keys"
  title: string;
  on: boolean; // name, colour and keys start off
  changes: Change[];
}

export interface Refused {
  line: string;
  why: string;
}

export interface Review {
  groups: Group[];
  same: number; // lines that would change nothing
  refused: Refused[];
}

interface Lookup {
  get(name: string): { value: string; writable: boolean; known: boolean; savable: boolean };
  bindings: AaBinding[];
  actions: AaAction[];
  keyName(keysym: number): string;
}

// a value as its row says it ("6 m", "30° down", "on"), else the raw text
export function valueWords(name: string, value: string): string {
  const placed = rowOf(name);
  const raw = unescapeValue(value);
  if (!placed) return raw;
  const row = placed.row;
  if (row.kind === "toggle") return raw === "0" ? t("common.off") : t("common.on");
  if (row.kind === "segment" || row.kind === "choice") {
    const o = row.options.find((x) => sameValue(x.value, raw));
    return o ? optionText(o) : raw;
  }
  if (row.kind === "slider") {
    const n = row.fromGame ? row.fromGame(raw) : Number(raw);
    if (!Number.isFinite(n)) return raw;
    return row.words ? row.words(n) : num(n) + (row.unit ? (row.unit === "%" || row.unit === "°" ? row.unit : " " + row.unit) : "");
  }
  return raw;
}

function labelOf(name: string): string {
  const placed = rowOf(name);
  if (!placed) return name;
  const row = placed.row;
  if (row.kind === "colour") return t("settings.paste.colourChannel", { c: name.slice(6, 7).toLowerCase() });
  if (row.kind === "instant-chats") return t("settings.action.instant", { n: name.split("_").pop() ?? "" });
  return t(row.label);
}

export function readPaste(text: string, look: Lookup): Review {
  const refused: Refused[] = [];
  const byGroup = new Map<string, Group>();
  const seen = new Set<string>();
  let same = 0;

  const group = (id: string): Group => {
    let g = byGroup.get(id);
    if (!g) {
      const tab = TABS.find((x) => x.id === id)?.title;
      const title = id === "identity" ? t("settings.paste.identity") : id === "keys" ? t("settings.paste.keys") : tab ? t(tab) : id;
      g = { id, title, on: id !== "identity" && id !== "keys", changes: [] };
      byGroup.set(id, g);
    }
    return g;
  };

  if (text.length > PASTE_MAX) refused.push({ line: t("settings.paste.why.kb", { n: Math.round(text.length / 1024) }), why: t("settings.paste.why.tooLong") });

  for (const rawLine of text.slice(0, PASTE_MAX).split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#") || line.startsWith("//")) continue;
    const m = /^(\S+)(?:\s+(.*))?$/.exec(line);
    if (!m) continue;
    const name = m[1].toUpperCase();
    const rest = (m[2] ?? "").trim();

    if (name === "KEYBOARD") {
      const k = /^(-?\d+)\s+PLAYER_BIND\s+(\S+)\s+(\d+)$/i.exec(rest);
      if (!k) {
        if (/^-1\b/.test(rest)) continue; // the game's "no keys" line
        refused.push({ line, why: t("settings.paste.why.notKeyLine") });
        continue;
      }
      const keysym = Number(k[1]);
      const action = k[2].toUpperCase();
      const player = Number(k[3]);
      const known = look.actions.find((a) => a.name === action);
      if (!known) {
        refused.push({ line, why: t("settings.paste.why.unknownAction") });
        continue;
      }
      if (player > 1) {
        refused.push({ line, why: t("settings.paste.why.player", { n: player }) });
        continue;
      }
      const key = "KEYBOARD " + keysym;
      if (seen.has(key)) continue;
      seen.add(key);
      const now = look.bindings.find((b) => b.keysym === keysym);
      if (now && now.action === action && now.player === player) {
        same++;
        continue;
      }
      group("keys").changes.push({
        name: key,
        line: `KEYBOARD ${keysym} PLAYER_BIND ${action} ${player}`,
        label: look.keyName(keysym),
        from: now ? actionWords(now.action) : t("settings.paste.nothingBound"),
        to: actionWords(action),
        value: action,
        keysym
      });
      continue;
    }

    if (NEVER.test(name)) {
      refused.push({ line: name, why: /PASSWORD/.test(name) ? t("settings.paste.why.password") : /^(USER_|GLOBAL_ID|AUTO_LOGIN|LOCAL_USER|ADMIN)/.test(name) ? t("settings.paste.why.login") : /_[2-4]$/.test(name) ? t("settings.paste.why.splitScreen") : t("settings.paste.why.notForPaste") });
      continue;
    }
    const item = look.get(name);
    if (!item.known) {
      refused.push({ line: name, why: t("settings.paste.why.unknown") });
      continue;
    }
    if (!item.writable) {
      refused.push({ line: name, why: t("settings.paste.why.room") });
      continue;
    }
    // not in user.cfg and not one of the rows: a server or session setting (CYCLE_SPEED), or a
    // command (INCLUDE, SAY, KICK: getConfig and listConfig know them too, with no value)
    if (!item.savable && !rowOf(name)) {
      refused.push({ line: name, why: item.value === "" ? t("settings.paste.why.command") : t("settings.paste.why.server") });
      continue;
    }
    if (seen.has(name)) continue;
    seen.add(name);
    if (sameValue(item.value, rest)) {
      same++;
      continue;
    }
    const id = name === "PLAYER_1" || /^COLOR_[RGB]_1$/.test(name) ? "identity" : tabOf(name);
    group(id).changes.push({ name, line: `${name} ${rest}`, label: labelOf(name), from: valueWords(name, item.value), to: valueWords(name, rest), value: rest });
  }

  const order = ["identity", ...TABS.map((t) => t.id), "keys"];
  const groups = [...byGroup.values()].sort((a, b) => order.indexOf(a.id) - order.indexOf(b.id));
  return { groups, same, refused };
}

// what UNDO PASTE puts back: the values and key bindings the paste replaced
export interface Undo {
  values: Array<[string, string]>;
  keys: Array<{ keysym: number; was: AaBinding | null }>;
}

export function applyReview(review: Review, store: SettingsStore): Undo {
  const undo: Undo = { values: [], keys: [] };
  const bindings = store.bindings();
  const values: Array<[string, string]> = [];
  for (const g of review.groups) {
    if (!g.on) continue;
    for (const c of g.changes) {
      if (c.keysym !== undefined) {
        undo.keys.push({ keysym: c.keysym, was: bindings.find((b) => b.keysym === c.keysym) ?? null });
        store.console(c.line);
      } else {
        undo.values.push([c.name, store.get(c.name).value]);
        values.push([c.name, c.value]);
      }
    }
  }
  if (values.length) store.setMany(values);
  return undo;
}

export function applyUndo(undo: Undo, store: SettingsStore) {
  if (undo.values.length) store.setMany(undo.values);
  for (const k of undo.keys) {
    if (k.was) store.bind(k.was.action, k.was.player, k.keysym);
    else store.unbind(k.keysym);
  }
}
