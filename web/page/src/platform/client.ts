// Typed calls to the platform (platform-contract/README.md section 1). The session is
// the platform's httpOnly cookie, sent with `credentials: "include"`; the page never
// sees or stores it, and nothing login related goes into IndexedDB or localStorage.
// State-changing calls that use the session carry `X-RC-CSRF: 1` (the contract's CSRF
// rule); errors are the platform's problem+json, kept as `code` and field `errors`.
import type {
  AccountInfo,
  BoardPage,
  BoardSort,
  BoardsInfo,
  BoardWindow,
  MatchTally,
  MyPlaces,
  AskAnswer,
  FriendInfo,
  FriendsList,
  ConversationsList,
  ListedPerson,
  MessageInfo,
  PageState,
  PrivacyInfo,
  RoomPlayerInfo,
  GamePasswordInfo,
  GameToken,
  IceServersResponse,
  LinkCode,
  LinkedIdInfo,
  LiveAnswer,
  MoveOffer,
  NewGamePassword,
  PendingLink,
  PlatformConfig,
  PlatformUser,
  PrivateRoomAnswer,
  DuelAcceptAnswer,
  DuelView,
  DuelSettings,
  DuelWait,
  QuickPlayAnswer,
  RegionsResponse,
  SessionInfo
} from "./types";
import type { PasswordCheck } from "./linking";
import type { ServerEntry } from "../aa/types";

export class PlatformError extends Error {
  constructor(
    message: string,
    readonly status: number, // HTTP status, 0 for network errors and timeouts
    readonly code: string | null = null, // the problem's machine-readable `code`
    readonly errors: Record<string, string[]> = {}, // validation: field -> codes
    readonly retryAfter: number | null = null, // seconds: the problem's `retryAfter`, else the Retry-After header
    readonly problem: Record<string, unknown> = {} // the whole problem object (e.g. a ban's `until` and `reason`)
  ) {
    super(message);
    this.name = "PlatformError";
  }
}

const DEFAULT_TIMEOUT_MS = 5000;

type Method = "GET" | "POST" | "PUT" | "DELETE";

interface CallOptions {
  body?: unknown;
  csrf?: boolean;
  timeoutMs?: number;
  signal?: AbortSignal; // the caller's own way to end it (as a timeout)
}

async function problemOf(response: Response): Promise<{ code: string | null; errors: Record<string, string[]>; title: string; retryAfter: number | null; raw: Record<string, unknown> }> {
  const header = Number(response.headers.get("Retry-After"));
  const headerRetry = Number.isFinite(header) && header > 0 ? header : null;
  try {
    const data = (await response.json()) as { code?: unknown; errors?: unknown; title?: unknown; retryAfter?: unknown };
    const errors: Record<string, string[]> = {};
    if (data.errors && typeof data.errors === "object") {
      for (const [field, codes] of Object.entries(data.errors as Record<string, unknown>)) {
        if (Array.isArray(codes)) errors[field.charAt(0).toLowerCase() + field.slice(1)] = codes.filter((c): c is string => typeof c === "string");
      }
    }
    const retryAfter = typeof data.retryAfter === "number" && data.retryAfter > 0 ? data.retryAfter : headerRetry;
    return {
      code: typeof data.code === "string" ? data.code : null,
      errors,
      title: typeof data.title === "string" ? data.title : "",
      retryAfter,
      raw: data && typeof data === "object" ? (data as Record<string, unknown>) : {}
    };
  } catch {
    return { code: null, errors: {}, title: "", retryAfter: headerRetry, raw: {} };
  }
}

// Inside a Discord Activity there is no cookie of ours: the session is a bearer token the
// platform handed out for Discord's sign-in (discordActivitySignIn), kept in memory only,
// for this page load (src/discord.ts)
let bearer: string | null = null;

export function setBearer(token: string | null) {
  bearer = token;
}

export function hasBearer(): boolean {
  return bearer !== null;
}

// one request; `null` for 202/204 (no body)
async function call<T>(apiUrl: string, method: Method, path: string, options: CallOptions = {}): Promise<T> {
  const abort = new AbortController();
  const timer = window.setTimeout(() => abort.abort(), options.timeoutMs ?? DEFAULT_TIMEOUT_MS);
  const stop = () => abort.abort();
  options.signal?.addEventListener("abort", stop);
  const headers: Record<string, string> = {};
  if (options.body !== undefined) headers["Content-Type"] = "application/json";
  if (options.csrf) headers["X-RC-CSRF"] = "1";
  if (bearer) headers["Authorization"] = "Bearer " + bearer;
  let response: Response;
  try {
    response = await fetch(apiUrl + path, {
      method,
      credentials: "include",
      cache: "no-store",
      headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      signal: abort.signal
    });
  } catch (e) {
    throw new PlatformError(method + " " + path + ": " + (abort.signal.aborted ? "timeout" : String(e)), 0);
  } finally {
    window.clearTimeout(timer);
    options.signal?.removeEventListener("abort", stop);
  }
  if (!response.ok) {
    const problem = await problemOf(response);
    throw new PlatformError(
      method + " " + path + ": HTTP " + response.status + (problem.code ? " " + problem.code : ""),
      response.status,
      problem.code,
      problem.errors,
      problem.retryAfter,
      problem.raw
    );
  }
  if (response.status === 202 || response.status === 204) return null as T;
  return (await response.json()) as T;
}

// `?p=`: the load a sign-in call is for, which then gets the user
const pageQuery = (page?: string) => (page ? "?p=" + encodeURIComponent(page) : "");

// with this load's page session and page_view: the platform keeps its copy of the load
export function getPlatformConfig(apiUrl: string, load?: { page: string; view: string; gpc: boolean } | null): Promise<PlatformConfig> {
  const query = load ? "?" + new URLSearchParams({ p: load.page, v: load.view, ...(load.gpc ? { gpc: "1" } : {}) }).toString() : "";
  return call<PlatformConfig>(apiUrl, "GET", "/v1/platform/config" + query);
}

// creates a guest and signs it in; with a valid session it returns the current user
// (page: the load it is for, which then gets the user)
export function ensureGuest(apiUrl: string, page?: string): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/guest" + pageQuery(page));
}

// "New player": ends the current session (guest or account) and creates a new guest
export function newPlayer(apiUrl: string, name: string): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/guest?fresh=true", { body: { name }, csrf: true });
}

// the signed-in user, or null without a session (401)
export async function getMe(apiUrl: string, page?: string): Promise<PlatformUser | null> {
  try {
    return await call<PlatformUser>(apiUrl, "GET", "/v1/me" + pageQuery(page));
  } catch (e) {
    if (e instanceof PlatformError && e.status === 401) return null;
    throw e;
  }
}

// the page's answer to an offer of a closer server (contract "Offers"): shown at a round end,
// accept (may complete the group: the move follows as a `move` event), dismiss; the offer back
export function answerOffer(apiUrl: string, offerId: string, action: "shown" | "accept" | "dismiss"): Promise<MoveOffer> {
  return call<MoveOffer>(apiUrl, "POST", "/v1/me/offers/" + encodeURIComponent(offerId), { body: { action }, csrf: true, timeoutMs: 6000 });
}

// a fresh login token for one server (60 s, single use); fetched right before connecting.
// 403 `banned` (PlatformError.problem: `until` Unix seconds or null, `reason`) when the user
// has a ban in force for that server: the page does not connect
export function getGameToken(apiUrl: string, serverId: string, timeoutMs = 4000): Promise<GameToken> {
  return call<GameToken>(apiUrl, "POST", "/v1/auth/game-token", { body: { serverId }, timeoutMs });
}

// ICE servers (a TURN relay with fresh credentials) for the next connect; session optional.
// serverId: the room about to be joined, so the platform hands out the relay of its region
export function getIceServers(apiUrl: string, serverId?: string, timeoutMs = 3000): Promise<IceServersResponse> {
  const query = serverId ? "?serverId=" + encodeURIComponent(serverId) : "";
  return call<IceServersResponse>(apiUrl, "GET", "/v1/net/ice-servers" + query, { timeoutMs });
}

// whether a player name is allowed in games: asked before a typed name is saved, so the player
// hears it at once instead of being renamed in a room (which checks every name itself)
export function checkName(apiUrl: string, name: string, timeoutMs = 3000): Promise<{ allowed: boolean; code?: string }> {
  return call<{ allowed: boolean; code?: string }>(apiUrl, "POST", "/v1/names/check", { body: { name }, timeoutMs });
}

// ---- reports (contract section 5, "Reports"; features.moderation) ----

export interface ReportBody {
  target: string; // a gid (also a former one) or a user id
  reason: string; // abusive_chat, hate, offensive_name, cheating, griefing, impersonation, other
  note?: string; // at most 500 characters
  serverId?: string; // the room it happened in: the platform attaches that room's recent chat
}

// 201 a new report, 200 the same reporter's open report on that target (10 minutes);
// 404 target_not_found, 409 cannot_report_self, 429 rate limited
export function createReport(apiUrl: string, body: ReportBody): Promise<{ id: string; status: string }> {
  return call<{ id: string; status: string }>(apiUrl, "POST", "/v1/reports", { body, csrf: true });
}

// ---- Quick Play (contract section 1, "Quick Play") ----

export function getRegions(apiUrl: string): Promise<RegionsResponse> {
  return call<RegionsResponse>(apiUrl, "GET", "/v1/regions", { timeoutMs: 3000 });
}

// a room and a seat in it; 503 `no_room` (PlatformError.retryAfter) when every room is full.
// `latencies`: the page's measured round trip per region id (ms), which the platform keeps
// for the player; left out when nothing was measured. `moveId`: see quickPlayMove.
// `room`: an invite link's room: a seat in exactly that one (503 `room_full`, 404 `room_closed`)
// `friend`: a friend's user id: a seat in the room they are in (404 `friend_not_joinable`)
// `avoid`: server ids this page just failed to reach; any other room (not for an invite or friend)
// `webSocket`: WebRTC does not get through from this page (webrtcFallback.ts): only rooms that
// take the game over their signalling WebSocket, an invite's or friend's too
export function quickPlay(
  apiUrl: string,
  region: string | null,
  latencies?: Record<string, number> | null,
  moveId?: string,
  room?: string | null,
  friend?: string | null,
  discordInstance?: string | null,
  avoid?: string[],
  webSocket?: boolean
): Promise<QuickPlayAnswer> {
  const body: { region?: string; latencies?: Record<string, number>; moveId?: string; room?: string; friend?: string; discordInstance?: string; avoid?: string[]; webSocket?: boolean } = {};
  if (friend) body.friend = friend;
  else if (room) body.room = room;
  else if (region) body.region = region;
  if (latencies && Object.keys(latencies).length) body.latencies = latencies;
  if (moveId) body.moveId = moveId;
  // a Discord Activity's launch: the platform keeps everyone in it in one room
  if (discordInstance && !moveId && !friend && !room) body.discordInstance = discordInstance;
  if (avoid && avoid.length && !friend && !room) body.avoid = avoid.slice(-8);
  if (webSocket && !body.discordInstance) body.webSocket = true;
  return call<QuickPlayAnswer>(apiUrl, "POST", "/v1/quickplay", { body, csrf: true });
}

// following a move (contract "Moving players"): the seat the platform holds in the move's
// target; an unknown or finished move is an ordinary Quick Play answer (so the region and
// latencies go along, as with every Quick Play request)
export function quickPlayMove(apiUrl: string, moveId: string, region: string | null, latencies?: Record<string, number> | null): Promise<QuickPlayAnswer> {
  return quickPlay(apiUrl, region, latencies, moveId);
}

// ---- private rooms ----

// a private room's way in for one connect (a fresh single-use ticket); 404 room_ended
export function joinPrivateRoom(apiUrl: string, code: string): Promise<PrivateRoomAnswer> {
  return call<PrivateRoomAnswer>(apiUrl, "POST", "/v1/private-rooms/" + encodeURIComponent(code) + "/join", { body: {}, csrf: true, timeoutMs: 8000 });
}

// ---- duels: nobody moves until the other side accepts ----

// a duel: a link (no target), a challenge to the player with that network id in that room (both
// must be in it; only browser players), or to a friend whose presence is challengeable (no room).
// The caller's earlier pending duel ends.
// 404 player_unavailable, 429 duels_limit
// `name`: the name the player plays under, shown to the other side
export function createDuel(apiUrl: string, latencies: Record<string, number> | null, target?: { serverId: string; netId: number } | { friendId: string } | null, name?: string | null): Promise<DuelView> {
  return call<DuelView>(apiUrl, "POST", "/v1/duels", { body: { ...(latencies ? { latencies } : {}), ...(target ?? {}), ...(name ? { name } : {}) }, csrf: true, timeoutMs: 8000 });
}

// the challenges to me and my newest duel; held up to ~25 s while nothing differs from `after`
export function waitDuels(apiUrl: string, after: string, signal: AbortSignal): Promise<DuelWait> {
  return call<DuelWait>(apiUrl, "GET", "/v1/duels/wait" + (after ? "?after=" + encodeURIComponent(after) : ""), { timeoutMs: 40000, signal });
}

// takes a duel. A challenge to me: a room is claimed now (503 private_rooms_busy: still pending).
// A link: I arrive (state "arrived", room null) and its creator says GO; once accepted, the same
// call answers the room with a ticket. 404 duel_ended, 409 own_duel, 409 duel_taken
export function acceptDuel(apiUrl: string, code: string, latencies: Record<string, number> | null, name?: string | null): Promise<DuelAcceptAnswer> {
  return call<DuelAcceptAnswer>(apiUrl, "POST", "/v1/duels/" + encodeURIComponent(code) + "/accept", { body: { ...(latencies ? { latencies } : {}), ...(name ? { name } : {}) }, csrf: true, timeoutMs: 8000 });
}

// my link someone opened: GO claims the room near both (join it with its room's code) - 409
// opponent_left (pending again), 503 private_rooms_busy (still arrived), 404 duel_ended; NOT NOW
// leaves the link open
export function goDuel(apiUrl: string, code: string): Promise<DuelView> {
  return call<DuelView>(apiUrl, "POST", "/v1/duels/" + encodeURIComponent(code) + "/go", { body: {}, csrf: true, timeoutMs: 8000 });
}

export function laterDuel(apiUrl: string, code: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/duels/" + encodeURIComponent(code) + "/later", { body: {}, csrf: true });
}

export function declineDuel(apiUrl: string, code: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/duels/" + encodeURIComponent(code) + "/decline", { body: {}, csrf: true });
}

export function cancelDuel(apiUrl: string, code: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/duels/" + encodeURIComponent(code) + "/cancel", { body: {}, csrf: true });
}

// my say over challenges: the switch and the players I stopped challenging me
export function getDuelSettings(apiUrl: string): Promise<DuelSettings> {
  return call<DuelSettings>(apiUrl, "GET", "/v1/duels/settings");
}

// off declines what waits now
export function setDuelSettings(apiUrl: string, acceptChallenges: boolean): Promise<DuelSettings> {
  return call<DuelSettings>(apiUrl, "PUT", "/v1/duels/settings", { body: { acceptChallenges }, csrf: true });
}

// `name`: theirs as the game shows it, for the list; their waiting challenges are declined
export function blockDuels(apiUrl: string, userId: string, name: string | null): Promise<DuelSettings> {
  return call<DuelSettings>(apiUrl, "PUT", "/v1/duels/blocks/" + encodeURIComponent(userId), { body: name ? { name } : {}, csrf: true });
}

export function unblockDuels(apiUrl: string, userId: string): Promise<DuelSettings> {
  return call<DuelSettings>(apiUrl, "DELETE", "/v1/duels/blocks/" + encodeURIComponent(userId), { csrf: true });
}

// the bell (features.news): the newest posts sent to players, which are unread; opening it reads them
export function getNewsBell(apiUrl: string): Promise<unknown> {
  return call<unknown>(apiUrl, "GET", "/v1/me/news", { timeoutMs: 6000 });
}

export function markNewsSeen(apiUrl: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/me/news/seen", { csrf: true, timeoutMs: 6000 });
}

// how many play in the official rooms now, and from how many the page says so
export function getLive(apiUrl: string): Promise<LiveAnswer> {
  return call<LiveAnswer>(apiUrl, "GET", "/v1/live", { timeoutMs: 3000 });
}

// the community Discord server (The Grid) the home screen links to: its invite and Discord's
// approximate counts, as the platform last asked Discord (null while unknown); 404 when none
export function getDiscordCommunity(apiUrl: string): Promise<{ invite: string; members: number | null; online: number | null }> {
  return call(apiUrl, "GET", "/v1/community/discord", { timeoutMs: 4000 });
}

// the Server Browser's list: servers.json's entries with what each server answered
// last (players, size, region, version, who is on, a URL to time for its ping); 404 when the
// platform keeps no list
export function getServers(apiUrl: string): Promise<{ servers: ServerEntry[] }> {
  return call<{ servers: ServerEntry[] }>(apiUrl, "GET", "/v1/servers", { timeoutMs: 4000 });
}

// ---- Direct Connect ----

// what a server answers at host:port (the platform asks it over UDP, as the board's poller does)
export interface DirectProbe {
  host: string;
  port: number;
  address: string | null; // the address the host resolved to (the blocklist is checked on it too)
  reachable: boolean;
  name: string | null;
  players: number | null;
  maxPlayers: number | null;
  version: string | null;
  browser: "ok" | "closed" | "unknown"; // the signalling port: answers, refuses, not known
  signalPort: number | null;
  blocked: boolean;
}

export function getDirectProbe(apiUrl: string, host: string, port: number, signal?: AbortSignal): Promise<DirectProbe> {
  const query = "?" + new URLSearchParams({ host, port: String(port) }).toString();
  return call<DirectProbe>(apiUrl, "GET", "/v1/direct/probe" + query, { timeoutMs: 6000, signal });
}

// hosts the page refuses to join: sha256 (hex) of the normalized host
export function getDirectBlocklist(apiUrl: string): Promise<{ v: number; hashes: string[] }> {
  return call<{ v: number; hashes: string[] }>(apiUrl, "GET", "/v1/direct/blocklist", { timeoutMs: 4000 });
}

export interface DirectReportBody {
  address: string; // host:port, or the server's wss:// URL
  targetName: string; // the player as the server shows them (codes stripped)
  reason: string; // the contract's reason codes, as POST /v1/reports
  note?: string;
  chat: Array<{ name: string; text: string; at: number }>; // this page's own lines, at most 50, the last 10 minutes
}

// a report about a player on a server that is not ours (needs a session: account or guest)
export function createDirectReport(apiUrl: string, body: DirectReportBody): Promise<{ id: string; status: string } | null> {
  return call<{ id: string; status: string } | null>(apiUrl, "POST", "/v1/direct/reports", { body, csrf: true });
}

// the seat could not be used (the player backed out; `failed`: the room could not be reached,
// which the platform counts toward skipping that room)
export function releaseSeat(apiUrl: string, reservationId: string, failed = false): Promise<null> {
  const path = "/v1/quickplay/reservations/" + encodeURIComponent(reservationId) + (failed ? "?reason=failed" : "");
  return call<null>(apiUrl, "DELETE", path, { csrf: true, timeoutMs: 3000 });
}

// ---- accounts (contract section 1, "Accounts", "Sessions", "External sign-in") ----

export interface RegisterBody {
  email: string;
  password: string;
  handle: string;
  name?: string;
  sharedComputer: boolean;
  newsEmails?: boolean; // the unticked "email me news" box
}

export function register(apiUrl: string, body: RegisterBody): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/register", { body, csrf: true, timeoutMs: 15000 });
}

// from a guest session the guest is merged into the account (claimGuest defaults to true)
export function signIn(apiUrl: string, login: string, password: string, sharedComputer: boolean): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/signin", { body: { login, password, sharedComputer }, csrf: true, timeoutMs: 15000 });
}

export function logout(apiUrl: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/logout", { csrf: true });
}

export function logoutAll(apiUrl: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/logout-all", { csrf: true });
}

export function verifyEmail(apiUrl: string, userId: string, token: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/verify-email", { body: { userId, token }, csrf: true });
}

export function resendVerification(apiUrl: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/resend-verification", { csrf: true });
}

export function forgotPassword(apiUrl: string, email: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/forgot-password", { body: { email }, csrf: true });
}

export function resetPassword(apiUrl: string, userId: string, token: string, password: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/auth/reset-password", { body: { userId, token, password }, csrf: true, timeoutMs: 15000 });
}

export function getAccount(apiUrl: string): Promise<AccountInfo> {
  return call<AccountInfo>(apiUrl, "GET", "/v1/me/account");
}

export function changePassword(apiUrl: string, currentPassword: string | null, newPassword: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/me/password", {
    body: currentPassword === null ? { newPassword } : { currentPassword, newPassword },
    csrf: true,
    timeoutMs: 15000
  });
}

// news emails on or off (opt-in); source "discord" when chosen inside the Discord Activity
export function setNewsEmails(apiUrl: string, on: boolean, source: "account" | "discord" | "ask"): Promise<null> {
  return call<null>(apiUrl, "PUT", "/v1/me/news-emails", { body: { on, source }, csrf: true });
}

export function unlinkProvider(apiUrl: string, provider: string): Promise<null> {
  return call<null>(apiUrl, "DELETE", "/v1/me/logins/" + encodeURIComponent(provider), { csrf: true });
}

export function chooseHandle(apiUrl: string, handle: string): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/handle", { body: { handle }, csrf: true });
}

export function getSessions(apiUrl: string): Promise<SessionInfo[]> {
  return call<SessionInfo[]>(apiUrl, "GET", "/v1/me/sessions");
}

export function revokeSession(apiUrl: string, id: string): Promise<null> {
  return call<null>(apiUrl, "DELETE", "/v1/me/sessions/" + encodeURIComponent(id), { csrf: true });
}

// Linked IDs (features.linking): a one-time code for /link, the linked old ids, unlinking one
export function createLinkCode(apiUrl: string): Promise<LinkCode> {
  return call<LinkCode>(apiUrl, "POST", "/v1/me/link-codes", { csrf: true });
}

export function getLinkedIds(apiUrl: string): Promise<LinkedIdInfo[]> {
  return call<LinkedIdInfo[]>(apiUrl, "GET", "/v1/me/linked-ids");
}

// features.linkByPassword: link an old id with its password; the page answers the authority's salt
// (platform/linking.ts) and only the answer is sent
export function startLinkCheck(apiUrl: string, gid: string): Promise<PasswordCheck> {
  return call<PasswordCheck>(apiUrl, "POST", "/v1/me/link-checks", { body: { gid }, csrf: true });
}

export function finishLinkCheck(apiUrl: string, check: string, hash: string): Promise<LinkedIdInfo> {
  return call<LinkedIdInfo>(apiUrl, "POST", "/v1/me/linked-ids", { body: { check, hash }, csrf: true });
}

export function unlinkId(apiUrl: string, gid: string): Promise<null> {
  return call<null>(apiUrl, "DELETE", "/v1/me/linked-ids/" + encodeURIComponent(gid), { csrf: true });
}

// game passwords (features.gamePasswords): for the desktop game, through the platform's authority
export function getGamePasswords(apiUrl: string): Promise<GamePasswordInfo[]> {
  return call<GamePasswordInfo[]>(apiUrl, "GET", "/v1/me/game-passwords");
}

export function createGamePassword(apiUrl: string, label: string): Promise<NewGamePassword> {
  return call<NewGamePassword>(apiUrl, "POST", "/v1/me/game-passwords", { body: { label }, csrf: true });
}

// the words again, to type them into the game
export function revealGamePassword(apiUrl: string, id: string): Promise<NewGamePassword> {
  return call<NewGamePassword>(apiUrl, "POST", "/v1/me/game-passwords/" + encodeURIComponent(id) + "/reveal", { csrf: true });
}

export function revokeGamePassword(apiUrl: string, id: string): Promise<null> {
  return call<null>(apiUrl, "DELETE", "/v1/me/game-passwords/" + encodeURIComponent(id), { csrf: true });
}

// the external sign-in waiting for "Link <provider> to <handle>?", or null (404)
export async function getPendingLink(apiUrl: string): Promise<PendingLink | null> {
  try {
    return await call<PendingLink>(apiUrl, "GET", "/v1/auth/external/pending");
  } catch (e) {
    if (e instanceof PlatformError && e.status === 404) return null;
    throw e;
  }
}

export function confirmLink(apiUrl: string): Promise<PlatformUser> {
  return call<PlatformUser>(apiUrl, "POST", "/v1/auth/external/confirm-link", { csrf: true });
}

// where the sign-in popup starts (a navigation, not a fetch)
export function externalStartUrl(apiUrl: string, provider: string, mode: "signin" | "link", sharedComputer: boolean): string {
  const query = new URLSearchParams({ mode, sharedComputer: String(sharedComputer), origin: location.origin });
  return apiUrl + "/v1/auth/external/" + encodeURIComponent(provider) + "/start?" + query.toString();
}

// friends (features.friends; accounts only, a guest gets 403 guest) and mutes (features.mutes)
export function getFriends(apiUrl: string): Promise<FriendsList> {
  return call<FriendsList>(apiUrl, "GET", "/v1/me/friends");
}

// by handle (or global id), or a room's player by network id (the account its server verified)
export function askFriend(apiUrl: string, who: { handle: string } | { serverId: string; netId: number }): Promise<AskAnswer> {
  return call<AskAnswer>(apiUrl, "POST", "/v1/me/friend-requests", { body: who, csrf: true });
}

export function acceptFriend(apiUrl: string, requestId: number): Promise<FriendInfo> {
  return call<FriendInfo>(apiUrl, "POST", `/v1/me/friend-requests/${requestId}/accept`, { csrf: true });
}

// declines a request to this account, or cancels one it sent
export function dropFriendRequest(apiUrl: string, requestId: number): Promise<null> {
  return call<null>(apiUrl, "DELETE", `/v1/me/friend-requests/${requestId}`, { csrf: true });
}

export function removeFriend(apiUrl: string, userId: string): Promise<null> {
  return call<null>(apiUrl, "DELETE", "/v1/me/friends/" + encodeURIComponent(userId), { csrf: true });
}

export function getBlocks(apiUrl: string): Promise<ListedPerson[]> {
  return call<ListedPerson[]>(apiUrl, "GET", "/v1/me/blocks");
}

export function setBlocked(apiUrl: string, userId: string, blocked: boolean): Promise<null> {
  return call<null>(apiUrl, blocked ? "PUT" : "DELETE", "/v1/me/blocks/" + encodeURIComponent(userId), { csrf: true });
}

export function getMutes(apiUrl: string): Promise<ListedPerson[]> {
  return call<ListedPerson[]>(apiUrl, "GET", "/v1/me/mutes");
}

export function setMuted(apiUrl: string, userId: string, muted: boolean): Promise<null> {
  return call<null>(apiUrl, muted ? "PUT" : "DELETE", "/v1/me/mutes/" + encodeURIComponent(userId), { csrf: true });
}

// leaderboards (features.leaderboards)
export function getRoomMatch(apiUrl: string, serverId: string): Promise<MatchTally> {
  return call<MatchTally>(apiUrl, "GET", `/v1/rooms/${encodeURIComponent(serverId)}/match`);
}

export function getBoards(apiUrl: string): Promise<BoardsInfo> {
  return call<BoardsInfo>(apiUrl, "GET", "/v1/leaderboards");
}

export function getBoard(apiUrl: string, board: string, window: BoardWindow, sort: BoardSort, from?: number, count?: number): Promise<BoardPage> {
  const query = new URLSearchParams({ sort });
  if (from) query.set("from", String(from));
  if (count) query.set("count", String(count));
  return call<BoardPage>(apiUrl, "GET", `/v1/leaderboards/${encodeURIComponent(board)}/${window}?${query}`);
}

export function getMyPlaces(apiUrl: string, board?: string, sort?: BoardSort): Promise<MyPlaces> {
  const query = new URLSearchParams();
  if (board) query.set("board", board);
  if (sort) query.set("sort", sort);
  return call<MyPlaces>(apiUrl, "GET", "/v1/leaderboards/me" + (query.size ? "?" + query : ""));
}

// who a room's players are, by network id (features.friends or features.mutes)
export async function getRoomPlayers(apiUrl: string, serverId: string, netIds: number[]): Promise<RoomPlayerInfo[]> {
  const answer = await call<{ players: RoomPlayerInfo[] }>(apiUrl, "GET", `/v1/rooms/${encodeURIComponent(serverId)}/players?netIds=${netIds.join(",")}`);
  return Array.isArray(answer?.players) ? answer.players : [];
}

// presence: what this page is doing, and the account's two switches
export function reportPresence(apiUrl: string, state: PageState, color: string | null): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/me/presence", { body: color ? { state, color } : { state }, csrf: true });
}

export function getPrivacy(apiUrl: string): Promise<PrivacyInfo> {
  return call<PrivacyInfo>(apiUrl, "GET", "/v1/me/privacy");
}

export function setPrivacy(apiUrl: string, change: Partial<PrivacyInfo>): Promise<PrivacyInfo> {
  return call<PrivacyInfo>(apiUrl, "PUT", "/v1/me/privacy", { body: change, csrf: true });
}

// direct messages between friends (features.friends, accounts; contract "Messages")
export function sendMessage(apiUrl: string, to: string, text: string): Promise<MessageInfo> {
  return call<MessageInfo>(apiUrl, "POST", "/v1/me/messages", { body: { to, text }, csrf: true });
}

export function getConversations(apiUrl: string): Promise<ConversationsList> {
  return call<ConversationsList>(apiUrl, "GET", "/v1/me/conversations");
}

export function getConversation(apiUrl: string, userId: string, before?: number): Promise<MessageInfo[]> {
  return call<MessageInfo[]>(apiUrl, "GET", "/v1/me/conversations/" + encodeURIComponent(userId) + (before ? "?before=" + before : ""));
}

export function readConversation(apiUrl: string, userId: string): Promise<null> {
  return call<null>(apiUrl, "POST", "/v1/me/conversations/" + encodeURIComponent(userId) + "/read", { csrf: true });
}

// the account's event stream (server-sent events, the session cookie): an EventSource's address
export function streamUrl(apiUrl: string): string {
  return apiUrl + "/v1/me/stream";
}

// ---- a Discord Activity (src/discord.ts) ----

export interface DiscordActivitySession {
  accessToken: string; // Discord's, for the SDK's authenticate()
  sessionToken: string; // ours: the bearer session (setBearer)
  expiresAt: string;
  me: PlatformUser;
}

// Discord's authorization code (and its PKCE verifier) for a platform session as that
// Discord user: their account, made the first time
export function discordActivitySignIn(apiUrl: string, code: string, codeVerifier: string, page?: string): Promise<DiscordActivitySession> {
  return call<DiscordActivitySession>(apiUrl, "POST", "/v1/auth/discord-activity" + pageQuery(page), { body: codeVerifier ? { code, codeVerifier } : { code }, csrf: true, timeoutMs: 15000 });
}
