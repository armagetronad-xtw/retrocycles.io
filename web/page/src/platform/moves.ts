// Following a move (platform-contract/README.md section 3, "Moving players"; web/wasm/API.md
// "Moves"): the room the player is in sends them to another room (rooms merged, a server
// emptied, a closer server). The page has left the old room when it gets here; it asks the
// platform for the seat held in the target (POST /v1/quickplay with the moveId) and the
// usual connect follows (login token, ICE servers). Without a platform the move's own
// signalling URL is used, without a login.
import type { AaMoveEvent, ServerEntry } from "../aa/types";
import { PlatformError, quickPlayMove } from "./client";
import { platformSession, sessionUser } from "./session";
import { QUICK_PLAY_MEASURE_WAIT_MS, preferredRegion, regionLatencies } from "./quickplay";
import type { FoundRoom } from "./quickplay";
import type { QuickPlayAnswer } from "./types";
import { t } from "../i18n";
import { sameHost } from "../directAddress";

export type MoveReason = "merge" | "drain" | "closer" | "join" | "admin";
export type MoveResult = "arrived" | "failed" | "cancelled";

const REASONS: MoveReason[] = ["merge", "drain", "closer", "join", "admin"];
// a full target answers 503 (the platform gave the seat to nobody else, so this is rare): a few more tries
const TRIES = 3;

export function moveReason(reason: string): MoveReason {
  return (REASONS as string[]).includes(reason) ? (reason as MoveReason) : "admin";
}

// the one line the player sees: a closer server and a friend's room (/join) are their own move, the rest go together
export function moveLine(move: AaMoveEvent, stripColors: (s: string) => string): string {
  const name = stripColors(move.name || move.serverId);
  const own = moveReason(move.reason) === "closer" || moveReason(move.reason) === "join";
  return own ? t("connect.move.own", { name }) : t("connect.move.together", { name });
}

export class MoveFailure extends Error {
  constructor(message: string) {
    super(message);
    this.name = "MoveFailure";
  }
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

//: a server that is not one of our rooms (a community server, a Direct Connect)
// moves a player only to another port of its own host; anywhere else the page asks first and
// never follows on its own. How a move from such a server is taken:
//   follow  the same host: on its way at once
//   ask     another host: the player is asked (App's move question), and the game goes on meanwhile
//   refuse  no signalling URL at all
export type ForeignMove = "follow" | "ask" | "refuse";

export function foreignMove(move: AaMoveEvent, fromUrl: string): ForeignMove {
  if (!move.signalUrl) return "refuse";
  return sameHost(fromUrl, move.signalUrl, location.href) ? "follow" : "ask";
}

// where to connect for this move: the platform's seat in the target, or (community mode, or a
// server that is not ours: `foreign`, the URL to use, through the gateway where the old one was)
// the move's own signalling URL. null when `signal` aborted (the player backed out).
export async function findMoveSeat(move: AaMoveEvent, signal: AbortSignal, foreign: ServerEntry | null = null): Promise<FoundRoom | null> {
  if (foreign) return { server: foreign, reservationId: "", roomId: move.serverId, region: "" };
  const state = await platformSession();
  if (state.mode !== "platform" || state.config.features.quickplay !== true) {
    if (!move.signalUrl) throw new MoveFailure(t("connect.move.noWay"));
    return { server: { name: move.name, url: move.signalUrl, description: "" }, reservationId: "", roomId: move.serverId, region: "" };
  }
  // the seat is the user's: the same session that played in the old room (a guest is made only
  // if there is none any more)
  await sessionUser();
  // measured once per page load; they make the fallback (a finished move) an ordinary Quick Play
  const region = await preferredRegion(QUICK_PLAY_MEASURE_WAIT_MS);
  const latencies = await regionLatencies(0);
  for (let tries = 1; ; tries++) {
    if (signal.aborted) return null;
    try {
      const answer = await quickPlayMove(state.apiUrl, move.moveId, region, latencies);
      if (!answer || typeof answer.signalUrl !== "string" || typeof answer.serverId !== "string") throw new MoveFailure(t("connect.fail.malformed"));
      return { server: asServer(answer), reservationId: answer.reservationId, roomId: answer.roomId, region: answer.region };
    } catch (e) {
      if (e instanceof MoveFailure) throw e;
      if (!(e instanceof PlatformError) || e.status !== 503 || tries >= TRIES) {
        throw new MoveFailure(e instanceof PlatformError && e.status === 503 ? t("connect.finding.allFull") : String(e instanceof Error ? e.message : e));
      }
      await sleep(Math.min(5, Math.max(1, e.retryAfter ?? 2)) * 1000, signal);
    }
  }
}
