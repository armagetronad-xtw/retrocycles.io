// Quick Play through the platform (platform-contract/README.md, "Quick Play"): with
// `features.quickplay` the page asks POST /v1/quickplay for a room instead of taking the
// first servers.json entry. The answer holds a seat in the room for 30 s; the page then
// connects as to any server (login token, ICE servers, signalUrl) and gives the seat back
// when it cannot use it. Every room full: 503 with retryAfter, asked again for about a
// minute. Community mode (no platform, or the feature off) never gets here.
// An invite link (?join=<room>) asks for a seat in that one room: full, the page asks again
// the same way; closed (404), it gives up at once.
import type { ServerEntry } from "../aa/types";
import { PlatformError, acceptDuel, cancelDuel, createDuel, getLive, getRegions, getServers, goDuel, joinPrivateRoom, quickPlay, releaseSeat } from "./client";
import { duelName, duelRoomText, duelSub, duelsNow, msOf, nudgeDuels, onDuels, watchDuels } from "./duels";
import { platformSession, platformState, sessionUser } from "./session";
import { track } from "./analytics";
import { webRtcBlocked } from "./webrtcFallback";
import { DISCORD_INSTANCE, throughProxy } from "../discord";
import type { DuelView, PrivateRoomAnswer, QuickPlayAnswer } from "./types";
import { t } from "../i18n";
import { alertArrival } from "../alerts";

// how long the page keeps asking while every room is full, and the pause bounds
export const FIND_TIMEOUT_MS = 60000;
const MIN_RETRY_S = 1;
const MAX_RETRY_S = 15;
const ERROR_RETRY_S = 3;
// how long a Discord launch's "not in this Activity" is taken for Discord catching up, and how often asked
const DISCORD_LAG_MS = 12000;
const DISCORD_LAG_RETRY_S = 2;

// one room rather than any: an invite link's, or a friend's (the platform finds
// their room; `spectate`: the page connects with the spectator switch on)
// A duel: `duel`, a link's code (null: make a new link and wait for someone to open it);
// `duelRoom`, the room of a duel accepted in a game (its claim code) and who it is with
export type JoinTarget =
  | { room: string }
  | { friend: string; name: string; spectate: boolean }
  | { duel: string | null }
  | { duelRoom: string; opponent: string | null };

// what the finding step shows; `friend`: the name of the friend being joined
export type FindingPhase =
  | { phase: "asking"; invite?: boolean; friend?: string; reroute?: boolean; duel?: "create" | "accept" | "room" } // the request is out (for the invited room; `reroute`: the last room did not answer; a duel's step)
  | { phase: "waiting"; until: number; full: boolean; invite?: boolean; friend?: string; duel?: boolean } // every room (the invited one) full, or the platform failed: asking again at `until` (ms)
  | { phase: "found"; room: string } // the room's name; connecting next
  | { phase: "signing" } // a Discord launch: Discord is still signing the player in (before any asking)
  // a new duel link (on the clipboard when `copied`), until someone opens it or `until` (ms) passes
  | { phase: "duel"; code: string; link: string; copied: boolean; until: number }
  // my link: someone opened it (`name`), the room is being claimed (GO, from the waiting screen)
  | { phase: "arrived"; name: string }
  // a link I opened: its creator (`name`) is told and says GO, until `until` (ms) at most
  | { phase: "joining"; name: string; until: number };

export interface FoundRoom {
  server: ServerEntry; // what connect() takes: name, signalUrl as url, serverId, platformLogin
  reservationId: string;
  roomId: string;
  region: string;
  sub?: string; // the connecting screen's line under the step, when it is not the room's name (a duel's)
  opponent?: string | null; // a duel's other side by the name they play under (the duel room's VS card)
}

export class FindFailure extends Error {
  // giveup: every room stayed full (or the platform kept failing) for FIND_TIMEOUT_MS;
  // otherwise the platform refused the request; plain: a whole sentence of its own (a duel's
  // other side said or did something), shown without the "finding:" lead
  constructor(message: string, readonly giveup = false, readonly full = false, readonly plain = false) {
    super(message);
    this.name = "FindFailure";
  }
}

// is Quick Play the platform's job on this page? (after the platform answered; false in community mode)
export async function quickPlayEnabled(): Promise<boolean> {
  const state = await platformSession();
  return state.mode === "platform" && state.config.features.quickplay === true;
}

// ---- the regions: every one of GET /v1/regions, timed once per page load ----
// Every region's latency is kept, not just the fastest: each one goes to the
// analytics once (`region_latency`) and to the platform with every Quick Play request
// (`latencies`), which keeps the player's latest numbers. The fastest is still the region
// asked for; placement stays the platform's business.

const PROBES = 2;
// the first probe of a URL pays DNS, TCP and TLS before its round trip: from far away (a
// candidate region on another continent) that alone can take a second and a half on a slow
// network, and an aborted first probe throws the connection away, so the second would pay it
// again. The second, on the kept-alive connection, is one round trip.
const PROBE_TIMEOUT_MS = [5000, 2000];
// Quick Play waits at most this long for the measurements it does not have yet (they start at
// page load), then asks with what it has: the platform fills in the player's stored ones
export const QUICK_PLAY_MEASURE_WAIT_MS = 2500;

async function timeOnce(url: string, attempt: number): Promise<number> {
  const abort = new AbortController();
  const timer = window.setTimeout(() => abort.abort(), PROBE_TIMEOUT_MS[Math.min(attempt, PROBE_TIMEOUT_MS.length - 1)]);
  const start = performance.now();
  try {
    // a CORS HEAD without credentials or headers (a simple request: no preflight; the pingUrl is
    // open to any origin), so no body comes down: a candidate region's pingUrl is the hosting
    // provider's looking-glass page (about 12 KB), and the page times every region twice. Not
    // no-cors: the page is cross-origin isolated (COEP require-corp), which blocks an opaque
    // cross-origin answer without a CORP header, so every probe failed
    await fetch(url, { method: "HEAD", mode: "cors", cache: "no-store", credentials: "omit", signal: abort.signal });
    return performance.now() - start;
  } catch {
    return Infinity;
  } finally {
    window.clearTimeout(timer);
  }
}

// the round trip to a URL (a host that answers CORS HEAD requests): the best of PROBES, Infinity
// when every probe failed. The Server Browser times each server's ping host with it
export async function timeUrl(url: string): Promise<number> {
  let best = Infinity;
  for (let i = 0; i < PROBES; i++) best = Math.min(best, await timeOnce(throughProxy(url), i));
  return best;
}

export interface RegionTime {
  id: string;
  ms: number; // the best of PROBES round trips, rounded; Infinity when every probe failed
}

let measured: Promise<RegionTime[]> | null = null;
// the regions' names by id ("London"), once the list is in
export const regionNames = new Map<string, string>();
// each region's result as soon as it is known (Quick Play may not wait for the slowest)
const soFar = new Map<string, number>();

// every region's round trip, fastest first (empty in community mode, or without a list).
// The first probe of each URL pays the TLS handshake, so the best of two counts.
export function measureRegions(): Promise<RegionTime[]> {
  if (!measured)
    measured = (async () => {
      const state = await platformSession();
      if (state.mode !== "platform") return [];
      try {
        const { regions } = await getRegions(state.apiUrl);
        if (!Array.isArray(regions)) return [];
        const usable = regions.filter((r) => r && typeof r.id === "string" && /^[a-z0-9_-]{1,32}$/.test(r.id) && typeof r.pingUrl === "string");
        for (const r of usable) if (typeof r.name === "string" && r.name) regionNames.set(r.id, r.name);
        const times = await Promise.all(
          usable.map(async (r) => {
            let best = Infinity;
            for (let i = 0; i < PROBES; i++) best = Math.min(best, await timeOnce(throughProxy(r.pingUrl), i));
            const ms = Number.isFinite(best) ? Math.round(best) : Infinity;
            soFar.set(r.id, ms);
            return { id: r.id, ms };
          })
        );
        times.sort((a, b) => a.ms - b.ms);
        if (times.length) console.info("platform: regions " + times.map((t) => t.id + " " + (Number.isFinite(t.ms) ? t.ms + " ms" : "unreachable")).join(", "));
        // one event per region the page could time, once per page load (the nearest one
        // becomes the page session's region in the reports)
        for (const t of times) if (Number.isFinite(t.ms) && t.ms <= LATENCY_MAX_MS) track("region_latency", { region: t.id, ms: t.ms });
        return times;
      } catch (e) {
        console.info("platform: no region list (" + String(e instanceof Error ? e.message : e) + ")");
        return [];
      }
    })();
  return measured;
}

const LATENCY_MAX_MS = 60000; // the contract's bound for a measured round trip

// every region's time once all are measured, or after `maxWaitMs` the ones known by then,
// fastest first
async function regionTimes(maxWaitMs: number): Promise<RegionTime[]> {
  if (!Number.isFinite(maxWaitMs)) return measureRegions();
  let timer = 0;
  const all = await Promise.race([
    measureRegions(),
    new Promise<null>((resolve) => (timer = window.setTimeout(() => resolve(null), maxWaitMs)))
  ]);
  window.clearTimeout(timer);
  if (all) return all;
  return [...soFar].map(([id, ms]) => ({ id, ms })).sort((a, b) => a.ms - b.ms);
}

// the measured round trips by region id, for POST /v1/quickplay (unreachable regions left out)
export async function regionLatencies(maxWaitMs = Infinity): Promise<Record<string, number>> {
  const out: Record<string, number> = {};
  for (const t of await regionTimes(maxWaitMs)) if (Number.isFinite(t.ms) && t.ms <= LATENCY_MAX_MS) out[t.id] = t.ms;
  return out;
}

// the region to ask for: null lets the platform choose its default (one region, no
// answer, a failed list, nothing reachable)
export async function preferredRegion(maxWaitMs = Infinity): Promise<string | null> {
  const times = await regionTimes(maxWaitMs);
  if (times.length < 2 || !Number.isFinite(times[0].ms)) return null;
  return times[0].id;
}

// ---- finding a room ----

function sleep(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    const timer = window.setTimeout(done, ms);
    function done() {
      window.clearTimeout(timer);
      signal.removeEventListener("abort", done);
      resolve();
    }
    signal.addEventListener("abort", done);
  });
}

function asServer(answer: QuickPlayAnswer): ServerEntry {
  return {
    name: answer.name,
    url: answer.signalUrl,
    description: "",
    serverId: answer.serverId,
    platformLogin: answer.platformLogin === true ? true : undefined
  };
}

// asks until a room answers, every room stayed full for FIND_TIMEOUT_MS, the platform
// refused the request, or `signal` aborts (the player backed out: resolves null).
// `onPhase` drives the finding step. `avoid`: rooms this page just could not reach.
export async function findRoom(
  signal: AbortSignal,
  onPhase: (phase: FindingPhase) => void,
  target: JoinTarget | null = null,
  avoid: string[] = []
): Promise<FoundRoom | null> {
  if (target && ("duel" in target || "duelRoom" in target)) return findDuel(signal, onPhase, target);
  const invite = target !== null;
  const room = target && "room" in target ? target.room : null;
  const friend = target && "friend" in target ? target : null;
  const deadline = Date.now() + FIND_TIMEOUT_MS;
  const discordUntil = Date.now() + DISCORD_LAG_MS;
  // the seat is the user's (their login on the room releases it): a guest first, as for a login token
  await sessionUser();
  const region = invite ? null : await preferredRegion(QUICK_PLAY_MEASURE_WAIT_MS);
  const latencies = await regionLatencies(0);
  let lastWhy = "";
  while (!signal.aborted) {
    const state = platformState();
    if (state.mode !== "platform") throw new FindFailure(t("connect.fail.platformGone"));
    onPhase({ phase: "asking", invite, friend: friend?.name, reroute: avoid.length > 0 || undefined });
    let wait: number;
    let full = false;
    try {
      const webSocket = webRtcBlocked();
      const answer = await quickPlay(state.apiUrl, region, latencies, undefined, room, friend?.friend, DISCORD_INSTANCE, avoid, webSocket);
      if (signal.aborted) {
        void giveBackSeat(answer.reservationId);
        return null;
      }
      if (!answer || typeof answer.signalUrl !== "string" || typeof answer.serverId !== "string") throw new FindFailure(t("connect.fail.malformed"));
      onPhase({ phase: "found", room: answer.name });
      const server = asServer(answer);
      // a room asked for over the WebSocket takes it (the connect goes that way)
      if (webSocket) server.lobby = { webSocket: true };
      return { server, reservationId: answer.reservationId, roomId: answer.roomId, region: answer.region };
    } catch (e) {
      if (e instanceof FindFailure) throw e;
      if (!(e instanceof PlatformError)) throw new FindFailure(String(e));
      // 400/403: this page and the platform disagree; asking again will not help
      if (friend && e.status === 404) throw new FindFailure(t("connect.fail.friendClosed", { name: friend.name }));
      if (invite && e.status === 404) throw new FindFailure(t("connect.fail.inviteClosed"));
      // a Discord launch asks the moment it is signed in: Discord may not list the player in the
      // instance yet, so those two are asked again for a few seconds
      const discordLag = (e.code === "not_in_instance" || e.code === "unknown_instance") && Date.now() < discordUntil;
      if (!discordLag && (e.status === 400 || e.status === 403 || e.status === 401 || e.status === 404)) throw new FindFailure(e.message);
      full = e.status === 503 && (e.code === "no_room" || e.code === "room_full");
      lastWhy = full ? (friend ? t("connect.fail.friendStayedFull", { name: friend.name }) : invite ? t("connect.fail.inviteStayedFull") : t("connect.fail.allStayedFull")) : e.message;
      const seconds = e.retryAfter ?? (discordLag ? DISCORD_LAG_RETRY_S : ERROR_RETRY_S);
      wait = Math.min(MAX_RETRY_S, Math.max(MIN_RETRY_S, seconds)) * 1000;
      console.info("platform: no room yet (" + e.message + "), asking again in " + wait / 1000 + " s");
    }
    if (Date.now() + wait > deadline) throw new FindFailure(lastWhy || t("connect.fail.noRoom"), true, full);
    onPhase({ phase: "waiting", until: Date.now() + wait, full, invite, friend: friend?.name });
    await sleep(wait, signal);
  }
  return null;
}

// ---- private rooms ----

export function privateRoomsEnabled(): boolean {
  const state = platformState();
  return state.mode === "platform" && state.config.features.privateRooms === true;
}

// what one duel step asks the platform, asked again while every private room is taken (as a
// full Quick Play), given up at once on a no
async function untilFree<T>(signal: AbortSignal, onPhase: (phase: FindingPhase) => void, duel: "accept" | "room", ask: () => Promise<T>): Promise<T | null> {
  const deadline = Date.now() + FIND_TIMEOUT_MS;
  let lastWhy = "";
  while (!signal.aborted) {
    onPhase({ phase: "asking", duel });
    let wait: number;
    let full = false;
    try {
      const answer = await ask();
      return signal.aborted ? null : answer;
    } catch (e) {
      if (!(e instanceof PlatformError)) throw new FindFailure(String(e));
      if (e.code === "own_duel") throw new FindFailure(t("connect.fail.ownDuel"));
      if (e.code === "duel_taken") throw new FindFailure(t("connect.fail.duelTaken"), false, false, true);
      if (e.status === 404) throw new FindFailure(t("connect.fail.duelEnded"));
      if (e.status >= 400 && e.status < 500) throw new FindFailure(e.message);
      full = e.status === 503 && e.code === "private_rooms_busy";
      lastWhy = full ? t("connect.fail.privateBusy") : e.message;
      wait = Math.min(MAX_RETRY_S, Math.max(MIN_RETRY_S, e.retryAfter ?? ERROR_RETRY_S)) * 1000;
      console.info("platform: no duel room yet (" + e.message + "), asking again in " + wait / 1000 + " s");
    }
    if (Date.now() + wait > deadline) throw new FindFailure(lastWhy || t("connect.fail.privateBusy"), true, full);
    onPhase({ phase: "waiting", until: Date.now() + wait, full, duel: true });
    await sleep(wait, signal);
  }
  return null;
}

// the link on the clipboard (right after the press that made it; false where the browser says no)
export async function copyText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;
  }
}

// how long a link's creator has to say GO (the platform's ArriveSeconds)
export const DUEL_ARRIVE_MS = 60000;

// the link's arrival shows this long at least before the connecting steps take over
const ARRIVED_HOLD_MS = 1400;

// a link duel left open while its creator plays (the waiting screen's PLAY WHILE YOU WAIT): the
// find backs out without withdrawing it (App.tsx keeps it, and answers its arrival over the game)
const keptOpen = new Set<string>();
export function keepLinkOpen(code: string) {
  keptOpen.add(code);
}

// my link duel answered: someone opened it (arrived), so GO from the waiting screen at once and
// resolve with its room; a failure once it ended any other way; null when the player backed out
function linkTaken(code: string, signal: AbortSignal, onPhase: (phase: FindingPhase) => void, back: FindingPhase): Promise<DuelView | null> {
  return new Promise((resolve, reject) => {
    const stop = watchDuels();
    nudgeDuels();
    let off = () => {};
    let going = false; // a GO is out
    let told = 0; // the arrival already announced (its arrivedAt)
    let showing = false; // the screen says they arrived
    const done = () => {
      off();
      stop();
      signal.removeEventListener("abort", backOut);
    };
    function backOut() {
      done();
      resolve(null);
    }
    const go = async () => {
      going = true;
      const shownAt = Date.now();
      const p = platformState();
      try {
        if (p.mode !== "platform") throw new FindFailure(t("connect.fail.platformGone"));
        for (;;) {
          try {
            const answer = await goDuel(p.apiUrl, code);
            if (!answer.room) throw new FindFailure(t("connect.fail.duelEnded"));
            await sleep(Math.max(0, ARRIVED_HOLD_MS - (Date.now() - shownAt)), signal);
            if (signal.aborted) return;
            done();
            resolve(answer);
            return;
          } catch (e) {
            if (!(e instanceof PlatformError)) throw e;
            if (e.code === "opponent_left") {
              // they backed out before the room was claimed: wait on for the next one
              showing = false;
              onPhase(back);
              return;
            }
            if (e.status !== 503) throw new FindFailure(e.status === 404 ? t("connect.fail.duelEnded") : e.message);
            await sleep(Math.min(MAX_RETRY_S, Math.max(MIN_RETRY_S, e.retryAfter ?? ERROR_RETRY_S)) * 1000, signal);
            if (signal.aborted || duelsNow().outgoing?.state !== "arrived") return;
          }
        }
      } catch (e) {
        if (signal.aborted) return;
        done();
        reject(e instanceof FindFailure ? e : new FindFailure(String(e instanceof Error ? e.message : e)));
      } finally {
        going = false;
      }
    };
    const check = () => {
      const mine = duelsNow().outgoing;
      if (!mine || mine.code !== code) return;
      if (mine.state === "pending") {
        // an arrival that ended before the room (they backed out meanwhile): waiting again
        if (showing && !going) {
          showing = false;
          onPhase(back);
        }
        return;
      }
      if (mine.state === "arrived") {
        if (going || (mine.arrivedAt ?? 0) === told) return;
        told = mine.arrivedAt ?? 0;
        showing = true;
        onPhase({ phase: "arrived", name: mine.to?.name ?? "" });
        alertArrival();
        void go();
        return;
      }
      if (going) return;
      done();
      if (mine.state === "accepted" && mine.room) resolve(mine);
      else reject(new FindFailure(mine.state === "expired" ? t("connect.fail.duelExpired") : t("connect.fail.duelEnded")));
    };
    off = onDuels(check);
    signal.addEventListener("abort", backOut);
    check();
  });
}

// a link I opened: its creator's answer. True once they said GO (the room is claimed: accept again
// for my ticket); a failure that says how it ended otherwise; false when I backed out (my arrival
// is withdrawn so their prompt goes at once)
function creatorAnswer(apiUrl: string, code: string, signal: AbortSignal): Promise<boolean> {
  return new Promise((resolve, reject) => {
    const stop = watchDuels();
    nudgeDuels();
    let off = () => {};
    let seen = false;
    const done = () => {
      off();
      stop();
      signal.removeEventListener("abort", backOut);
    };
    function backOut() {
      done();
      void cancelDuel(apiUrl, code).catch(() => {});
      resolve(false);
    }
    const check = () => {
      const mine = duelsNow().joining;
      if (!mine || mine.code !== code) {
        // gone from the wait after it showed: withdrawn by its creator
        if (seen) {
          done();
          reject(new FindFailure(t("connect.fail.duelEnded")));
        }
        return;
      }
      seen = true;
      if (mine.state === "arrived") return;
      done();
      const name = mine.from.name;
      if (mine.state === "accepted") resolve(true);
      else if (mine.state === "pending" && mine.arrivalEnd === "not_now") reject(new FindFailure(t("connect.fail.duelNotNow", { name }), false, false, true));
      else if (mine.state === "pending" && mine.arrivalEnd === "no_answer") reject(new FindFailure(t("connect.fail.duelNoAnswer", { name }), false, false, true));
      else if (mine.state === "expired") reject(new FindFailure(t("connect.fail.duelExpiredFor", { name }), false, false, true));
      else reject(new FindFailure(t("connect.fail.duelEnded")));
    };
    off = onDuels(check);
    signal.addEventListener("abort", backOut);
    check();
  });
}

// a duel's room: a new link duel (code null: made, copied, then waited on until someone
// opens it), a link opened (its code: accepted, which claims the room), or the room of a duel
// accepted in a game (duelRoom: its claim code). There is no seat to give back: the room is the
// duel's until it empties.
async function findDuel(
  signal: AbortSignal,
  onPhase: (phase: FindingPhase) => void,
  target: { duel: string | null } | { duelRoom: string; opponent: string | null }
): Promise<FoundRoom | null> {
  await sessionUser(); // a duel is held for a session: a guest first
  const state = platformState();
  if (state.mode !== "platform") throw new FindFailure(t("connect.fail.platformGone"));
  const apiUrl = state.apiUrl;
  const latencies = await regionLatencies(QUICK_PLAY_MEASURE_WAIT_MS);
  let answer: PrivateRoomAnswer | null = null;
  let roomCode = "";
  let opponent: string | null = null;
  if ("duelRoom" in target) {
    roomCode = target.duelRoom;
    opponent = target.opponent;
  } else if (target.duel !== null) {
    const code = target.duel;
    let taken = await untilFree(signal, onPhase, "accept", () => acceptDuel(apiUrl, code, latencies, duelName()));
    if (!taken) return null;
    opponent = taken.duel.from.name;
    if (!taken.room) {
      // a link: I arrived; its creator says GO (wherever they are), then my ticket
      const arrived = msOf(taken.duel.arrivedAt ?? Math.floor(Date.now() / 1000));
      onPhase({ phase: "joining", name: opponent, until: arrived + DUEL_ARRIVE_MS });
      if (!(await creatorAnswer(apiUrl, code, signal))) return null;
      taken = await untilFree(signal, onPhase, "accept", () => acceptDuel(apiUrl, code, latencies, duelName()));
      if (!taken) return null;
      if (!taken.room) throw new FindFailure(t("connect.fail.duelEnded"));
    }
    answer = taken.room;
  } else {
    onPhase({ phase: "asking", duel: "create" });
    let duel: DuelView;
    try {
      duel = await createDuel(apiUrl, latencies, null, duelName());
    } catch (e) {
      throw new FindFailure(e instanceof PlatformError && e.code === "duels_limit" ? t("game.duel.limit") : String(e instanceof Error ? e.message : e));
    }
    if (signal.aborted) {
      void cancelDuel(apiUrl, duel.code).catch(() => {});
      return null;
    }
    const link = privateRoomLink(duel.code);
    const waiting: FindingPhase = { phase: "duel", code: duel.code, link, copied: await copyText(link), until: msOf(duel.expiresAt) };
    onPhase(waiting);
    const taken = await linkTaken(duel.code, signal, onPhase, waiting);
    if (!taken || !taken.room) {
      // backed out: withdrawn, unless it was left open to play meanwhile
      if (!keptOpen.delete(duel.code)) void cancelDuel(apiUrl, duel.code).catch(() => {});
      return null;
    }
    roomCode = taken.room.code;
    opponent = taken.to?.name ?? null;
  }
  if (!answer) {
    answer = await untilFree(signal, onPhase, "room", () => joinPrivateRoom(apiUrl, roomCode));
    if (!answer) return null;
  }
  if (typeof answer.signalUrl !== "string" || typeof answer.serverId !== "string" || typeof answer.code !== "string")
    throw new FindFailure(t("connect.fail.malformed"));
  onPhase({ phase: "found", room: answer.name });
  const server: ServerEntry = {
    name: answer.name,
    url: answer.signalUrl,
    description: "",
    serverId: answer.serverId,
    platformLogin: answer.platformLogin === true ? true : undefined,
    privateCode: answer.code,
    ticket: answer.ticket
  };
  const sub = duelSub(duelRoomText(answer.name, answer.region, regionNames), opponent);
  return { server, reservationId: "", roomId: answer.serverId, region: answer.region, sub, opponent };
}

// the ticket for one connect to a private room: the answer's own the first time, a fresh one
// after (a ticket is single use on the room); null when the room has ended
export async function privateRoomTicket(server: ServerEntry): Promise<string | null> {
  if (!server.privateCode) return null;
  if (server.ticket) {
    const ticket = server.ticket;
    server.ticket = undefined;
    return ticket;
  }
  const state = platformState();
  if (state.mode !== "platform") return null;
  const answer = await joinPrivateRoom(state.apiUrl, server.privateCode);
  return answer.ticket;
}

// a duel's link: the page with ?duel=<code>
export function privateRoomLink(code: string): string {
  const url = new URL(location.pathname, location.origin);
  url.searchParams.set("duel", code);
  url.searchParams.set("c", "invite");
  return url.toString();
}

// a duel link's code, read once and removed from the address bar like ?join=
export function readPrivateInvite(): string | null {
  const query = new URLSearchParams(location.search);
  const code = query.get("duel");
  if (code === null) return null;
  query.delete("duel");
  const rest = query.toString();
  history.replaceState(history.state, "", location.pathname + (rest ? "?" + rest : "") + location.hash);
  return /^[0-9A-Za-z]{4,16}$/.test(code) ? code.toUpperCase() : null;
}

// the page could not use the seat: free it now rather than in 30 s (a failure is a note).
// `failed`: the room could not be reached (not the player backing out)
export async function giveBackSeat(reservationId: string, failed = false): Promise<void> {
  const state = platformState();
  if (state.mode !== "platform") return;
  try {
    await releaseSeat(state.apiUrl, reservationId, failed);
  } catch (e) {
    console.info("platform: could not release the seat (" + String(e instanceof Error ? e.message : e) + ")");
  }
}

// ---- invite links ----

// the address that brings a friend into this room: the page itself with ?join=<server id>
export function inviteLink(serverId: string): string {
  const url = new URL(location.pathname, location.origin);
  url.searchParams.set("join", serverId);
  // the campaign of the page_view it brings (an invited player's source: invite)
  url.searchParams.set("c", "invite");
  return url.toString();
}

// an invite link's room, read once and removed from the address bar (a reload or a later
// Quick Play is not an invite again); null without one. A server id is at most 64
// characters (the platform's limit).
export function readInvite(): string | null {
  const query = new URLSearchParams(location.search);
  const room = query.get("join");
  if (room === null) return null;
  query.delete("join");
  const rest = query.toString();
  history.replaceState(history.state, "", location.pathname + (rest ? "?" + rest : "") + location.hash);
  return room && room.length <= 64 ? room : null;
}

// ---- how many play now ----

// GET /v1/live: the figure for the Quick Play line, or null (no platform, a failure, or
// fewer players than the platform's minShown: a small figure says "empty" louder than none)
// the platform's server list (GET /v1/servers): null in community mode, or when the platform keeps
// none or does not answer (the board then reads servers.json)
export async function platformServers(): Promise<ServerEntry[] | null> {
  const state = await platformSession();
  if (state.mode !== "platform") return null;
  try {
    const answer = await getServers(state.apiUrl);
    return Array.isArray(answer.servers) && answer.servers.length ? answer.servers : null;
  } catch {
    return null;
  }
}

export async function livePlayers(): Promise<number | null> {
  const state = await platformSession();
  if (state.mode !== "platform" || state.config.features.quickplay !== true) return null;
  try {
    const live = await getLive(state.apiUrl);
    return typeof live.players === "number" && live.players >= Math.max(1, live.minShown) ? live.players : null;
  } catch {
    return null;
  }
}
