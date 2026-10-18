// The Console view's log: every line the game printed, as it printed it. The
// chat overlay sorts the same `console` events into what a player reads while playing
// (sort.ts); this keeps them raw: colour codes and all, nothing sorted, renamed or dropped.
// It listens from the moment the client is prepared (useClient), so the lines the client
// printed while it loaded are here before any game. Memory only, never stored.
//
// It holds this session and the one before it: beginSession() (a connect, a local game)
// draws a quiet divider naming the room and forgets everything before the previous
// divider. 2,000 lines at most, the oldest dropping off.
import { useSyncExternalStore } from "react";
import type { Aa, AaConsoleEvent } from "../aa/types";
import { decodeGameText } from "./text";
import { stripCodes } from "./sort";
import { currentLang } from "../i18n";

export const LOG_KEEP = 2000;

// line: the game's own text; divider: a session began (the room's name); input: what the
// player ran from the Console line; note: the page's own answer when it could not run it
export type LogKind = "line" | "divider" | "input" | "note";

export interface LogLine {
  id: number;
  kind: LogKind;
  raw: string; // with the game's colour codes (a line), else the same as text
  text: string; // plain
  at: number; // Date.now()
}

let lines: LogLine[] = [];
let nextId = 1;
const listeners = new Set<() => void>();
let attached: Aa | null = null;

function emit() {
  for (const fn of listeners) fn();
}

function push(add: LogLine[]) {
  if (!add.length) return;
  const all = lines.concat(add);
  lines = all.length > LOG_KEEP ? all.slice(all.length - LOG_KEEP) : all;
  emit();
}

function line(kind: LogKind, raw: string, text = raw): LogLine {
  return { id: nextId++, kind, raw, text, at: Date.now() };
}

// from AA.prepare on, every `console` event (several lines in one event are split)
export function attachLog(aa: Aa) {
  if (attached === aa) return;
  attached = aa;
  aa.on("console", (e: AaConsoleEvent) => {
    if (!e || typeof e.text !== "string") return;
    const raw = decodeGameText(typeof e.line === "string" ? e.line : e.text);
    const add: LogLine[] = [];
    for (const l of raw.split(/\r?\n/)) {
      const r = l.replace(/\s+$/, "");
      if (!stripCodes(r).trim()) continue;
      add.push(line("line", r, stripCodes(r)));
    }
    push(add);
  });
}

// a game begins (a connect, a local game): the divider, and only the previous session kept
export function beginSession(room: string) {
  const last = lines.map((l) => l.kind).lastIndexOf("divider");
  if (last > 0) lines = lines.slice(last);
  const time = new Date().toLocaleTimeString(currentLang(), { hour: "2-digit", minute: "2-digit" });
  push([line("divider", `${room} · ${time}`)]);
}

export function logInput(text: string) {
  push([line("input", text)]);
}

export function logNote(text: string) {
  push([line("note", text)]);
}

// the log as plain text (Copy log): the lines given, one per line
export function logText(list: LogLine[]): string {
  return list
    .map((l) => (l.kind === "divider" ? `--- ${l.text} ---` : l.kind === "input" ? "> " + l.text : l.text))
    .join("\n");
}

function subscribe(fn: () => void) {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

export function useLog(): LogLine[] {
  return useSyncExternalStore(subscribe, () => lines);
}
