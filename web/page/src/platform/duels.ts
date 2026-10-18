// Duels: a 1v1 sumo in a private room, started by a link (the home menu's 1v1 line) or
// by a challenge to a player in the same room (the in-game menu's player card; browser players
// only), or to a friend whose page hears it (the Friends panel's card; no room shared). Nobody
// is put in a room until the other side accepts: the platform claims a pool room at that moment
// and both pages go there.
// Each page learns about its duels through one long poll, GET /v1/duels/wait, kept running while
// something watches (a network game, the home screen for a signed-in player, the link's waiting
// screen): the challenges to this player and their own newest duel. After an own act (a
// challenge, a cancel) the poll is asked again at once, since the platform only wakes the other
// side.
import { useEffect, useSyncExternalStore } from "react";
import * as api from "./client";
import { PlatformError } from "./client";
import { platformState } from "./session";
import type { DuelSettings, DuelView, DuelWait } from "./types";
import { t } from "../i18n";

const EMPTY: DuelWait = { cursor: "", incoming: [], outgoing: null };
// a failed poll is asked again after this long (the platform restarting, the network gone)
const RETRY_MS = 5000;

let snapshot: DuelWait = EMPTY;
// the name this player plays under (a guest's platform name is not it), read when a duel is
// made or accepted, so the other side sees it
let gameName: () => string = () => "";
let watchers = 0;
let running = false;
let current: AbortController | null = null;
const listeners = new Set<() => void>();

function set(next: DuelWait) {
  snapshot = next;
  for (const l of listeners) l();
}

export function setDuelName(read: () => string) {
  gameName = read;
}

export function duelName(): string | null {
  return gameName().trim() || null;
}

export function duelsNow(): DuelWait {
  return snapshot;
}

export function onDuels(fn: () => void): () => void {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

function enabled(): boolean {
  const p = platformState();
  return p.mode === "platform" && p.config.features.privateRooms === true;
}

async function loop() {
  if (running) return;
  running = true;
  try {
    while (watchers > 0 && enabled()) {
      const p = platformState();
      if (p.mode !== "platform") break;
      const abort = new AbortController();
      current = abort;
      try {
        const answer = await api.waitDuels(p.apiUrl, snapshot.cursor, abort.signal);
        if (answer && Array.isArray(answer.incoming))
          set({ cursor: String(answer.cursor ?? ""), incoming: answer.incoming, outgoing: answer.outgoing ?? null, joining: answer.joining ?? null });
      } catch (e) {
        if (abort.signal.aborted) {
          if (watchers > 0) continue; // asked again at once (nudge)
          break; // nobody watches any more
        }
        if (e instanceof PlatformError && e.status === 404) break; // the feature is off
        if (e instanceof PlatformError && e.status === 401) break; // no session: the next watch asks again
        console.info("duels: " + String(e instanceof Error ? e.message : e));
        await new Promise((r) => window.setTimeout(r, RETRY_MS));
      } finally {
        if (current === abort) current = null;
      }
    }
  } finally {
    running = false;
    if (watchers === 0) set(EMPTY);
  }
}

// keep the poll running while the returned function is not called
export function watchDuels(): () => void {
  watchers++;
  void loop();
  let done = false;
  return () => {
    if (done) return;
    done = true;
    watchers--;
    if (watchers === 0) current?.abort();
  };
}

// ask again now, not with the old cursor (after an own act)
export function nudgeDuels() {
  snapshot = { ...snapshot, cursor: "" };
  current?.abort();
}

// the duels, while `on`
export function useDuels(on: boolean): DuelWait {
  useEffect(() => {
    if (!on) return;
    return watchDuels();
  }, [on]);
  return useSyncExternalStore(onDuels, duelsNow, duelsNow);
}

// a time the platform sent: Unix seconds or ms
export function msOf(time: number): number {
  return time < 1e12 ? time * 1000 : time;
}

// "Private Sumo 2, London": the room and its region by name (the regions list, else the id)
export function duelRoomText(name: string, region: string, regionNames: ReadonlyMap<string, string>): string {
  return t("connect.duel.room", { room: name, region: regionNames.get(region) ?? region });
}

// the connecting screen's line under the step: who with, and where
export function duelSub(room: string, opponent: string | null): string {
  return opponent ? t("connect.duel.subWith", { name: opponent, room }) : t("connect.duel.sub", { room });
}

// the other side of a duel, from this player's side
export function opponentOf(duel: DuelView, myId: string | null): string | null {
  if (duel.from.userId === myId) return duel.to?.name ?? null;
  return duel.from.name;
}

// the platform's no to a challenge or an accept, in the page's words
export function duelFailText(e: unknown): string {
  if (e instanceof PlatformError) {
    if (e.code === "player_unavailable") return t("game.duel.unavailable");
    if (e.code === "duels_limit") return t("game.duel.limit");
    // turned down lately: a few minutes after one, the rest of the day after three
    if (e.code === "duel_cooldown") {
      const minutes = Math.max(1, Math.ceil((e.retryAfter ?? 600) / 60));
      return minutes > 60 ? t("game.duel.cooldownDay") : t("game.duel.cooldown", { minutes });
    }
    if (e.code === "private_rooms_busy") return t("game.duel.busy");
    if (e.code === "duel_ended" || e.status === 404) return t("game.duel.ended");
  }
  return t("game.duel.failed");
}

// ---- my say over challenges: the switch and the players blocked from challenging me ----------
// Read from the platform once something shows them (the Esc menu's card, Settings), then kept
// here; every change answers the whole settings, which replace these.

let settings: DuelSettings | null = null;
let settingsAsked = false;
const settingsListeners = new Set<() => void>();

function setSettings(next: DuelSettings | null) {
  settings = next;
  for (const l of settingsListeners) l();
}

function onSettings(fn: () => void): () => void {
  settingsListeners.add(fn);
  return () => {
    settingsListeners.delete(fn);
  };
}

function settingsNow(): DuelSettings | null {
  return settings;
}

function apiUrl(): string | null {
  const p = platformState();
  return p.mode === "platform" && p.config.features.privateRooms === true ? p.apiUrl : null;
}

// the settings while `on` (asked once; null until they came, or without duels)
export function useDuelSettings(on: boolean): DuelSettings | null {
  useEffect(() => {
    if (!on || settingsAsked) return;
    const url = apiUrl();
    if (!url) return;
    settingsAsked = true;
    api.getDuelSettings(url).then(setSettings, (e: unknown) => {
      settingsAsked = false; // asked again next time
      console.info("duel settings: " + String(e instanceof Error ? e.message : e));
    });
  }, [on]);
  return useSyncExternalStore(onSettings, settingsNow, settingsNow);
}

async function change(run: (url: string) => Promise<DuelSettings>): Promise<DuelSettings> {
  const url = apiUrl();
  if (!url) throw new PlatformError("duels are off", 404, "not_found");
  const next = await run(url);
  setSettings(next);
  settingsAsked = true;
  nudgeDuels(); // a challenge waiting from them is declined now
  return next;
}

export function setChallenges(on: boolean): Promise<DuelSettings> {
  return change((url) => api.setDuelSettings(url, on));
}

export function blockChallenges(userId: string, name: string | null): Promise<DuelSettings> {
  return change((url) => api.blockDuels(url, userId, name));
}

export function unblockChallenges(userId: string): Promise<DuelSettings> {
  return change((url) => api.unblockDuels(url, userId));
}

export function challengesBlocked(s: DuelSettings | null, userId: string): boolean {
  return !!s && s.blocked.some((b) => b.userId === userId);
}

