// The page's words in the player's language. No library: a typed t(key, vars) over
// flat catalogues, plurals by Intl.PluralRules, numbers and "3 minutes ago" by Intl. English
// (./en) is the source and ships in the main bundle; every other language is its own chunk
// (./locales/<lang>), fetched only when chosen, so an English player downloads nothing more.
//
// - A key is a readable dotted name ("settings.row.fov"); t() takes only keys of the English
//   catalogue and the vars its text names ({name}), so a missing key or var fails tsc.
// - A text is a string, or a plural: {one, other} (and zero/two/few/many where the language has
//   them; Polish has few and many) chosen by the var n.
// - The language: rc.lang in localStorage when the player chose one (Settings, PLAYER), else
//   the first of navigator.languages we have, else English. Changing it re-renders the page
//   (App subscribes with useLang()) without a reload; <html lang> follows.
// - The game client stays English: the page reads its console lines (chat/sort.ts and others)
//   by their English templates. Never set the game's LANGUAGE.
import { createElement, Fragment, useSyncExternalStore, type ReactNode } from "react";
import { EN } from "./en";

export type Lang = "en" | "de" | "fr" | "es" | "pt-BR" | "pl";

// each named in itself, for the Settings row (never translated)
export const LANGS: ReadonlyArray<{ id: Lang; name: string }> = [
  { id: "en", name: "English" },
  { id: "de", name: "Deutsch" },
  { id: "fr", name: "Français" },
  { id: "es", name: "Español" },
  { id: "pt-BR", name: "Português (Brasil)" },
  { id: "pl", name: "Polski" }
];

export interface Plural {
  zero?: string;
  one?: string;
  two?: string;
  few?: string;
  many?: string;
  other: string;
}
export type Msg = string | Plural;

type En = typeof EN;
export type MsgKey = keyof En;

// a translation of a set of English texts: every key, nothing else (the vars are checked at
// load in development, checkCatalogue)
export type Translation<T> = { readonly [K in keyof T]: Msg };
export type Catalogue = Translation<En>;

// the {vars} a text names
type ParamsOf<S> = S extends `${string}{${infer P}}${infer R}` ? P | ParamsOf<R> : never;
type MsgParams<V> = V extends string ? ParamsOf<V> : V extends Plural ? ParamsOf<V["other"]> | ParamsOf<NonNullable<V["one"]>> | "n" : never;
type Params<K extends MsgKey> = MsgParams<En[K]>;
// a key whose text names no vars: what data may hold and render later with t(key) (the
// Settings schema's labels, a mode's title)
export type PlainKey = { [K in MsgKey]: [Params<K>] extends [never] ? K : never }[MsgKey];
type VarsArg<K extends MsgKey, V> = [Params<K>] extends [never] ? [] : [vars: Record<Params<K>, V> & (En[K] extends Plural ? { n: number } : unknown)];

// ---- the store -----------------------------------------------------------------------------

const STORE_KEY = "rc.lang";
let lang: Lang = "en";
let cat: Catalogue = EN;
const listeners = new Set<() => void>();

const loaders: Record<Exclude<Lang, "en">, () => Promise<{ default: Catalogue }>> = {
  de: () => import("./locales/de/index"),
  fr: () => import("./locales/fr/index"),
  es: () => import("./locales/es/index"),
  "pt-BR": () => import("./locales/pt-BR/index"),
  pl: () => import("./locales/pl/index")
};

function isLang(v: unknown): v is Lang {
  return LANGS.some((l) => l.id === v);
}

// the language a browser asks for: the first of navigator.languages we have (pt-PT and pt
// get Brazilian Portuguese, the one Portuguese we have)
export function browserLang(list: readonly string[] = navigator.languages?.length ? navigator.languages : [navigator.language]): Lang {
  for (const tag of list) {
    const t = (tag || "").toLowerCase();
    const base = t.split("-")[0];
    if (base === "en") return "en";
    if (base === "pt") return "pt-BR";
    if (isLang(base)) return base;
  }
  return "en";
}

function stored(): Lang | null {
  try {
    const v = localStorage.getItem(STORE_KEY);
    return isLang(v) ? v : null;
  } catch {
    return null;
  }
}

// the player chose a language in Settings (kept), or not (the browser's decides)
export function chosenLang(): Lang | null {
  return stored();
}

async function apply(next: Lang): Promise<void> {
  let c: Catalogue = EN;
  if (next !== "en") {
    try {
      c = (await loaders[next]()).default;
    } catch (e) {
      console.info("i18n: " + next + " did not load (" + String(e instanceof Error ? e.message : e) + "); English");
      next = "en";
    }
  }
  if (import.meta.env.DEV && next !== "en") for (const p of checkCatalogue(c)) console.warn("i18n " + next + ": " + p);
  lang = next;
  cat = c;
  plurals = null;
  numbers = null;
  document.documentElement.lang = next;
  for (const l of listeners) l();
}

// at startup, before the first render (main.tsx): the chosen or the browser's language; English
// at once, and at worst after 2 s (a chunk that does not come)
export function initLang(): Promise<void> {
  const want = stored() ?? browserLang();
  if (want === "en") {
    document.documentElement.lang = "en";
    return Promise.resolve();
  }
  return Promise.race([apply(want), new Promise<void>((r) => setTimeout(r, 2000))]);
}

// Settings: the player's choice, kept and applied now
export function setLang(next: Lang): Promise<void> {
  try {
    localStorage.setItem(STORE_KEY, next);
  } catch {
    /* blocked storage: this load only */
  }
  return apply(next);
}

// a hint from elsewhere (Discord's locale): used only when the player has not chosen one
export function hintLang(tag: string): void {
  if (stored()) return;
  const want = browserLang([tag]);
  if (want !== lang && want !== "en") void apply(want);
}

export function currentLang(): Lang {
  return lang;
}

function subscribe(fn: () => void) {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

// re-render on a change of language (App calls it; the tree under it re-renders with App)
export function useLang(): Lang {
  return useSyncExternalStore(subscribe, currentLang);
}

// ---- formatting ----------------------------------------------------------------------------

let plurals: Intl.PluralRules | null = null;
let numbers: Intl.NumberFormat | null = null;

// a number as the language writes it (3,5 in German); no grouping, so years and ports stay whole
export function num(n: number, opts?: Intl.NumberFormatOptions): string {
  if (Object.is(n, -0)) n = 0; // String(-0) is "0"; Intl would say "-0"
  if (opts) return new Intl.NumberFormat(lang, { useGrouping: false, ...opts }).format(n);
  numbers ??= new Intl.NumberFormat(lang, { useGrouping: false, maximumFractionDigits: 20 });
  return numbers.format(n);
}

function pick(m: Msg, n: unknown): string {
  if (typeof m === "string") return m;
  if (typeof n !== "number") return m.other;
  if (n === 0 && m.zero !== undefined) return m.zero;
  plurals ??= new Intl.PluralRules(lang);
  return m[plurals.select(n)] ?? m.other;
}

function fill(text: string, vars: Record<string, unknown> | undefined): string {
  if (!vars) return text;
  return text.replace(/\{(\w+)\}/g, (all, k: string) => {
    const v = vars[k];
    if (v === undefined) return all;
    return typeof v === "number" ? num(v) : String(v);
  });
}

// the text of a key in the current language
export function t<K extends MsgKey>(key: K, ...args: VarsArg<K, string | number>): string {
  const vars = args[0] as Record<string, unknown> | undefined;
  return fill(pick(cat[key] ?? EN[key], vars?.n), vars);
}

// the same with React nodes for vars: a link or a key cell inside a sentence
export function tx<K extends MsgKey>(key: K, ...args: VarsArg<K, ReactNode>): ReactNode {
  const vars = args[0] as Record<string, ReactNode> | undefined;
  const text = pick(cat[key] ?? EN[key], vars?.n);
  if (!vars) return text;
  const out: ReactNode[] = [];
  let last = 0;
  for (const m of text.matchAll(/\{(\w+)\}/g)) {
    out.push(text.slice(last, m.index));
    const v = vars[m[1]];
    out.push(v === undefined ? m[0] : typeof v === "number" ? num(v) : v);
    last = m.index! + m[0].length;
  }
  out.push(text.slice(last));
  return createElement(Fragment, null, ...out);
}

// English, whatever the language (FIND matches the English words too)
export function en<K extends MsgKey>(key: K, ...args: VarsArg<K, string | number>): string {
  const vars = args[0] as Record<string, unknown> | undefined;
  const text = pick(EN[key], vars?.n);
  if (!vars) return text;
  return text.replace(/\{(\w+)\}/g, (all, k: string) => (vars[k] === undefined ? all : String(vars[k])));
}

// "3 minutes ago" (the sessions, a friend last seen): "just now" under 90 s
export function ago(unixSeconds: number): string {
  const s = Math.max(0, Math.round(Date.now() / 1000 - unixSeconds));
  if (s < 90) return t("common.justNow");
  const rtf = new Intl.RelativeTimeFormat(lang, { numeric: "always" });
  const m = Math.round(s / 60);
  if (m < 90) return rtf.format(-m, "minute");
  const h = Math.round(m / 60);
  if (h < 36) return rtf.format(-h, "hour");
  return rtf.format(-Math.round(h / 24), "day");
}

// ---- checks (development) ------------------------------------------------------------------

function textsOf(m: Msg): string[] {
  return typeof m === "string" ? [m] : (Object.values(m).filter((v) => typeof v === "string") as string[]);
}

function varsOf(m: Msg): string {
  const s = new Set<string>();
  for (const text of textsOf(m)) for (const x of text.matchAll(/\{(\w+)\}/g)) s.add(x[1]);
  if (typeof m !== "string") s.add("n");
  return [...s].sort().join(",");
}

// what a catalogue gets wrong against English: missing or extra keys, vars that differ
export function checkCatalogue(c: Catalogue): string[] {
  const out: string[] = [];
  for (const k of Object.keys(EN) as MsgKey[]) {
    if (!(k in c)) out.push("missing " + k);
    else if (varsOf(c[k]).replace(/(^|,)n(,|$)/, "$1$2") !== varsOf(EN[k]).replace(/(^|,)n(,|$)/, "$1$2")) out.push("vars differ in " + k + ": " + varsOf(c[k]) + " vs " + varsOf(EN[k]));
  }
  for (const k of Object.keys(c)) if (!(k in EN)) out.push("extra " + k);
  return out;
}
