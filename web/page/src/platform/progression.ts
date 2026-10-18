// Progression (platform-contract/README.md section 1, "Progression"):
// what the platform credited the player for, from the game servers' events, never from the
// page. The page reads it from GET /v1/me (`progression`) and keeps the latest answer in a
// small store for the Profile stage; XpTracker turns successive answers into "what is new
// since the last look": the XP entries whose ids are above the highest seen (a round's gain)
// and a `lastMatch` whose id changed (a match ended). Nothing here without a platform whose
// features.progression is on, or without a session.
import { useSyncExternalStore } from "react";
import { getMe } from "./client";
import { platformState, setPlatformUser } from "./session";
import { offersOn, wellFormedOffer } from "./offers";
import { currentLang, num, t } from "../i18n";
import type { LastMatch, MoveOffer, PlatformState, PlatformUser, Progression, XpEntry } from "./types";

export function progressionOn(platform: PlatformState): boolean {
  return platform.mode === "platform" && platform.config.features.progression === true && !!platform.user;
}

// ---- the store --------------------------------------------------------------------------

let latest: Progression | null = null;
let owner: string | null = null; // the user id the latest progression is of
const listeners = new Set<() => void>();

function publish(next: Progression | null, userId: string | null) {
  latest = next;
  owner = next ? userId : null;
  for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function progressionSnapshot(): Progression | null {
  return latest;
}

// whose the latest progression is (after Sign out or New player it is someone else's until
// the next read)
export function progressionOwner(): string | null {
  return owner;
}

// the latest progression the page saw (the Profile renders it)
export function useProgression(): Progression | null {
  return useSyncExternalStore(subscribe, progressionSnapshot, progressionSnapshot);
}

function wellFormed(p: unknown): p is Progression {
  if (!p || typeof p !== "object") return false;
  const q = p as Progression;
  return typeof q.level === "number" && typeof q.xp === "number" && Array.isArray(q.recent);
}

// GET /v1/me: the user (the store of session.ts follows it) and its progression; null when
// the feature is off, there is no session, or the answer carries none. A failed request
// keeps what the page had (a console note), so a slow platform never costs the player more
// than a missing line.
export async function refreshProgression(): Promise<Progression | null> {
  const platform = platformState();
  if (!progressionOn(platform)) return null;
  return (await refreshMe()).progression;
}

export interface MeRead {
  progression: Progression | null; // null without features.progression (or none in the answer)
  offer: MoveOffer | null; // an offer of a closer server (features.moveOffers), or null
  failed: boolean; // the request failed: progression is what the page had, no offer
}

// one GET /v1/me for the round-end look (useRewards.ts): the progression as above and, with
// features.moveOffers, the offer of a closer server it may carry (platform/offers.ts)
export async function refreshMe(): Promise<MeRead> {
  const platform = platformState();
  const none: MeRead = { progression: null, offer: null, failed: false };
  if (platform.mode !== "platform" || !platform.user) return none;
  const wantProgression = progressionOn(platform);
  if (!wantProgression && !offersOn(platform)) return none;
  let me: PlatformUser | null;
  try {
    me = await getMe(platform.apiUrl);
  } catch (e) {
    console.info("platform: no /v1/me (" + String(e instanceof Error ? e.message : e) + ")");
    return { progression: wantProgression ? latest : null, offer: null, failed: true };
  }
  if (!me) {
    setPlatformUser(null);
    publish(null, null);
    return none;
  }
  const { progression, moveOffer, ...user } = me;
  setPlatformUser(user);
  let next: Progression | null = null;
  if (wantProgression) {
    next = wellFormed(progression) ? { ...progression, recent: progression.recent ?? [] } : null;
    publish(next, user.id);
  }
  return { progression: next, offer: offersOn(platform) ? wellFormedOffer(moveOffer) : null, failed: false };
}

// ---- what is new since the last look -------------------------------------------------

export interface Gain {
  amount: number; // the sum of the new entries
  entries: XpEntry[];
  level: number; // the level now
  levelUp: boolean;
}

export interface Fresh {
  gain: Gain | null;
  match: LastMatch | null; // a match that ended since the last look
  progression: Progression;
}

export class XpTracker {
  private based = false;
  private seenId = -Infinity;
  private matchId: string | null = null;
  private level = 0;

  reset() {
    this.based = false;
    this.seenId = -Infinity;
    this.matchId = null;
    this.level = 0;
  }

  get hasBaseline(): boolean {
    return this.based;
  }

  // the first answer only records where the player stands (what was earned before this
  // look is not news); every later one reports what is new
  take(p: Progression): Fresh {
    const ids = p.recent.map((e) => e.id);
    const maxId = ids.length ? Math.max(...ids) : -Infinity;
    const matchId = p.lastMatch ? p.lastMatch.id : null;
    if (!this.based) {
      this.based = true;
      this.seenId = maxId;
      this.matchId = matchId;
      this.level = p.level;
      return { gain: null, match: null, progression: p };
    }
    const entries = p.recent.filter((e) => e.id > this.seenId);
    const amount = entries.reduce((sum, e) => sum + e.amount, 0);
    const levelUp = p.level > this.level;
    const gain = entries.length || levelUp ? { amount, entries, level: p.level, levelUp } : null;
    const match = matchId && matchId !== this.matchId ? p.lastMatch : null;
    this.seenId = Math.max(this.seenId, maxId);
    this.matchId = matchId;
    this.level = p.level;
    return { gain, match, progression: p };
  }
}

// ---- words ------------------------------------------------------------------------------

// a place as the language writes it ("2nd", "2.", "2e"): the ordinal plural rules pick the form
export function ordinal(n: number): string {
  const form = new Intl.PluralRules(currentLang(), { type: "ordinal" }).select(n);
  if (form === "one") return t("game.place.one", { n });
  if (form === "two") return t("game.place.two", { n });
  if (form === "few") return t("game.place.few", { n });
  return t("game.place.other", { n });
}

// a gain with its sign: "+25", "-5"
export function xpNumber(amount: number): string {
  return (amount >= 0 ? "+" : "") + num(amount);
}

export function xpText(amount: number): string {
  return t("game.xp.amount", { amount: xpNumber(amount) });
}

// "12 min", "1 h 05 min", "40 s"
export function playedText(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  if (s < 60) return t("game.played.seconds", { s });
  const minutes = Math.floor(s / 60);
  if (minutes < 60) return t("game.played.minutes", { m: minutes });
  return t("game.played.hours", { h: Math.floor(minutes / 60), mm: String(minutes % 60).padStart(2, "0") });
}

// the same, short, for a cell that holds one number: "45s", "12m", "1h 20m"
export function playedShort(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  if (s < 60) return t("game.played.secondsShort", { s });
  const minutes = Math.floor(s / 60);
  if (minutes < 60) return t("game.played.minutesShort", { m: minutes });
  const h = Math.floor(minutes / 60);
  return minutes % 60 ? t("game.played.hoursMinutesShort", { h, m: minutes % 60 }) : t("game.played.hoursShort", { h });
}

// how far into the level: 0..1
export function levelFraction(p: Pick<Progression, "xp" | "levelXp" | "nextLevelXp">): number {
  const span = p.nextLevelXp - p.levelXp;
  if (!(span > 0)) return 1;
  return Math.max(0, Math.min(1, (p.xp - p.levelXp) / span));
}
