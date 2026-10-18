// servers.json next to the page (API.md "servers.json"): [{name, url, description}],
// plus the optional platform login fields `serverId` (the server's id at the
// platform) and `platformLogin: true` (it accepts login tokens; see platform/).
// AA.loadServers() would do, but it drops the `lobby` block the page keeps
// (players, ping, region, ...) for the day a lobby feed writes one
// (`GET /servers`). Without it the board shows dashes: nothing is invented.
//
// The Server Browser reads every entry as a ServerView: what the board and its
// details pane show, with null wherever the feed does not say. bestOrder() is the
// "Best" sort (rooms with friends first, then bookmarked ones, people playing, no count, full,
// and the empty rooms last: the board folds those away; a server a browser cannot join is not
// listed), and the filters are pure functions of the list.
import type { ServerEntry, ServerLobby } from "./aa/types";
import { stripColors } from "./components/Colored";
import { throughProxy } from "./discord";
import { DISCORD } from "./links";
import type { FriendsList } from "./platform/types";
import type { PlainKey } from "./i18n";

export const FALLBACK_SERVERS: ServerEntry[] = [
  { name: "this host", url: "/signal", description: "wss://<host>/signal through Caddy" }
];

// `ok` is false when servers.json could not be read (the board says so and lists the fallback)
export async function loadServers(): Promise<{ list: ServerEntry[]; ok: boolean }> {
  try {
    const response = await fetch("./servers.json", { cache: "no-cache" });
    if (!response.ok) throw new Error("HTTP " + response.status);
    const list: unknown = await response.json();
    if (!Array.isArray(list)) return { list: FALLBACK_SERVERS, ok: false };
    const servers = list
      .filter((s): s is ServerEntry => !!s && typeof s.url === "string" && typeof s.name === "string")
      .map((s) => ({
        name: String(s.name),
        url: String(s.url),
        description: s.description ? String(s.description) : "",
        serverId: typeof s.serverId === "string" && s.serverId ? s.serverId : undefined,
        platformLogin: s.platformLogin === true ? true : undefined,
        lobby: s.lobby && typeof s.lobby === "object" ? s.lobby : undefined
      }));
    return servers.length ? { list: servers, ok: true } : { list: FALLBACK_SERVERS, ok: false };
  } catch {
    return { list: FALLBACK_SERVERS, ok: false };
  }
}

// ---- invite codes ---------------------------------------------------------------------
// Any listed server has an invite code: six characters worked out from its signalling URL, so
// everyone gets the same code for the same server and nothing has to be stored (an invite
// link is ?join=<code>, platform/quickplay.ts inviteLink). A link only ever reaches a server on
// this page's own list; a server whose URL changes gets a new code.

const CODE_LETTERS = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // Crockford's base 32: no I, L, O, U

export function serverCode(url: string): string {
  // FNV-1a, 32 bits: plenty to tell a few dozen servers apart
  let h = 0x811c9dc5;
  for (let i = 0; i < url.length; i++) {
    h ^= url.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  let code = "";
  for (let i = 0; i < 6; i++) {
    code += CODE_LETTERS[h & 31];
    h >>>= 5;
  }
  return code;
}

// the listed server an invite names: by its platform server id (the links before codes), else by
// its code (any case; an O typed for a 0, an I or L for a 1)
export function invitedServer(servers: ServerEntry[], room: string): ServerEntry | undefined {
  const byId = servers.find((s) => s.serverId === room);
  if (byId) return byId;
  const code = room.toUpperCase().replace(/O/g, "0").replace(/[IL]/g, "1");
  return servers.find((s) => serverCode(s.url) === code);
}

// the Server Browser's list inside a Discord Activity: only the rooms that take game packets over
// their signalling WebSocket (the board's lobby.webSocket), since Discord allows no WebRTC; the
// whole list everywhere else
export function discordJoinable(list: ServerEntry[]): ServerEntry[] {
  return DISCORD ? list.filter((s) => s.lobby?.webSocket === true) : list;
}

// the signalling URL to hand to aa.connect(): a path is resolved against this host
export function signalUrl(server: ServerEntry): string {
  const AA = window.AA;
  // inside a Discord Activity: the proxy's mapped path (src/discord.ts)
  return throughProxy(AA ? AA.resolveSignalUrl(server.url) : server.url);
}

// ---- the board's view of a server ----------------------------------------------------

export interface RosterPlayer {
  name: string; // with the game's colour codes
  handle: string | null; // the platform account's handle when the player is logged in with it
  color: string | null; // the cycle colour, "#rrggbb"
  score: number | null;
  bot: boolean;
  friend: boolean; // the feed said so, or the handle is a friend's
}

export interface ServerView {
  key: string; // the signalling URL: what a refresh keeps the focus on
  entry: ServerEntry;
  name: string; // as the server writes it, colour codes kept (Colored draws them)
  plain: string; // the name without codes, lower case: the search's and the name sort's
  description: string;
  official: boolean; // a Retrocycles room: platform sign-in, progress, our moderation
  moderated: boolean; // a community server verified on our platform
  browser: boolean; // a browser can join it (it has a signalling URL); the board lists only these
  humans: number | null; // players who are not bots; null: not reported
  bots: number | null;
  active: number | null; // humans not spectating, where the feed knows
  max: number | null;
  region: string | null;
  ping: number | null;
  pingEstimated: boolean; // by region, not measured to this server
  pingUrl: string | null; // a host near the server the page times for its ping
  version: string | null;
  release: string | null; // the full release (the build's tail too), else the version
  roster: RosterPlayer[] | null; // null: the server does not publish its players
  friends: number; // friends of the player in this room now
}

export type Tier = "friends" | "bookmarked" | "playing" | "unknown" | "full" | "empty";

// the tiers' heads, as keys of the catalogue (i18n, rendered with t())
export const TIER_TITLE: Record<Tier, PlainKey> = {
  friends: "connect.tier.friends",
  bookmarked: "connect.tier.bookmarked",
  playing: "connect.tier.playing",
  unknown: "connect.tier.unknown",
  full: "connect.tier.full",
  empty: "connect.tier.empty"
};

// EMPTY last: the board shows it folded, a head with its count (ServerBrowser.tsx)
export const TIER_ORDER: Tier[] = ["friends", "bookmarked", "playing", "unknown", "full", "empty"];

function num(v: unknown): number | null {
  return typeof v === "number" && isFinite(v) ? v : null;
}

function str(v: unknown): string | null {
  return typeof v === "string" && v.trim() ? v.trim() : null;
}

// friends in the official room with this server id (the platform's presence)
function friendsIn(entry: ServerEntry, friends: FriendsList | null): number {
  if (!friends || !entry.serverId) return 0;
  return friends.friends.filter((f) => f.presence?.room?.serverId === entry.serverId).length;
}

// the handles of the player's friends, lower case: a roster's logged-in players are matched on them
export function friendHandles(friends: FriendsList | null): ReadonlySet<string> {
  return new Set(friends ? friends.friends.map((f) => f.user.handle.toLowerCase()) : []);
}

// pings: the round trips the board timed, by ping URL (ServerBrowser.tsx)
export function toView(
  entry: ServerEntry,
  friends: FriendsList | null,
  pings?: ReadonlyMap<string, number>,
  handles: ReadonlySet<string> = friendHandles(friends)
): ServerView {
  const l: ServerLobby = entry.lobby || {};
  const roster = Array.isArray(l.roster)
    ? l.roster.map((p) => {
        const handle = str(p.handle);
        return {
          name: String(p.name ?? ""),
          handle,
          color: str(p.color),
          score: num(p.score),
          bot: p.bot === true,
          friend: p.friend === true || (handle !== null && handles.has(handle.toLowerCase()))
        };
      })
    : null;
  const humans = num(l.humans) ?? (roster ? roster.filter((p) => !p.bot).length : num(l.players));
  const bots = num(l.bots) ?? (roster ? roster.filter((p) => p.bot).length : null);
  const counted = friendsIn(entry, friends);
  return {
    key: entry.url,
    entry,
    name: entry.name,
    plain: stripColors(entry.name).toLowerCase(),
    description: entry.description,
    // servers.json is our own list: an entry with a platform id is an official room
    official: typeof l.official === "boolean" ? l.official : !!entry.serverId,
    // the board's trusted tier: the entry's own field, else its lobby's
    moderated: entry.moderated === true || (entry.moderated !== false && l.moderated === true),
    browser: l.browser !== false,
    humans,
    bots,
    active: num(l.active),
    max: num(l.maxPlayers),
    region: str(l.region),
    ping: num(l.ping) ?? timed(l.pingUrl, pings),
    pingEstimated: l.pingEstimated === true,
    pingUrl: str(l.pingUrl),
    version: str(l.version),
    release: str(l.release) ?? str(l.version),
    roster,
    friends: Math.max(counted, roster ? roster.filter((p) => p.friend).length : 0, num(l.friends) ?? 0)
  };
}

function timed(url: unknown, pings?: ReadonlyMap<string, number>): number | null {
  const ms = typeof url === "string" && pings ? pings.get(url) : undefined;
  return ms !== undefined && Number.isFinite(ms) ? Math.round(ms) : null;
}

export function isFull(s: ServerView): boolean {
  return s.humans !== null && s.max !== null && s.max > 0 && s.humans >= s.max;
}

// a room with friends in it heads the board, bookmarked or full as it may be; a bookmarked room
// is never folded away with the empty ones
export function tierOf(s: ServerView, bookmarks: ReadonlySet<string>): Tier {
  if (s.friends > 0) return "friends";
  if (bookmarks.has(s.key)) return "bookmarked";
  if (isFull(s)) return "full";
  if (s.humans === null) return "unknown";
  return s.humans > 0 ? "playing" : "empty";
}

// who is really playing: active players where the feed knows, else the humans
function liveness(s: ServerView): number {
  return s.active ?? s.humans ?? -1;
}

function byPing(a: ServerView, b: ServerView): number {
  if (a.ping === null && b.ping === null) return 0;
  if (a.ping === null) return 1;
  if (b.ping === null) return -1;
  return a.ping - b.ping;
}

function byName(a: ServerView, b: ServerView): number {
  return a.plain < b.plain ? -1 : a.plain > b.plain ? 1 : 0;
}

// within a tier, friends lift a room, then people playing (more first),
// then ping; an empty room by ping; a full room by people, then ping
function withinTier(a: ServerView, b: ServerView): number {
  const fr = (b.friends > 0 ? 1 : 0) - (a.friends > 0 ? 1 : 0);
  if (fr) return fr;
  const live = liveness(b) - liveness(a);
  if (live) return live;
  return byPing(a, b) || byName(a, b);
}

export interface Group {
  tier: Tier;
  servers: ServerView[];
}

export function bestOrder(servers: ServerView[], bookmarks: ReadonlySet<string>): Group[] {
  const groups = new Map<Tier, ServerView[]>();
  for (const s of servers) {
    const t = tierOf(s, bookmarks);
    const g = groups.get(t);
    if (g) g.push(s);
    else groups.set(t, [s]);
  }
  return TIER_ORDER.filter((t) => groups.has(t)).map((t) => ({ tier: t, servers: groups.get(t)!.sort(withinTier) }));
}

export type SortKey = "best" | "name" | "players" | "ping" | "region";

// a column head's sort: one flat list (no tiers); unknown values always sink
export function sortBy(servers: ServerView[], key: Exclude<SortKey, "best">, asc: boolean): ServerView[] {
  const dir = asc ? 1 : -1;
  const value = (s: ServerView): number | string | null =>
    key === "players" ? s.humans : key === "ping" ? s.ping : key === "region" ? s.region?.toLowerCase() ?? null : s.plain;
  return servers.slice().sort((a, b) => {
    const x = value(a);
    const y = value(b);
    if (x === null && y === null) return byName(a, b);
    if (x === null) return 1;
    if (y === null) return -1;
    return x < y ? -dir : x > y ? dir : byName(a, b);
  });
}

// ---- filters ----------------------------------------------------------------------------

export type Scope = "all" | "official" | "community";

// HIDE EMPTY and HIDE FULL are gone: full rooms are listed, empty ones fold
// into the board's last group; a stored filter from before keeps its region, its two flags are
// dropped (and leave the stored copy at the next save)
export interface Filters {
  scope: Scope;
  region: string | null;
}

export const NO_FILTERS: Filters = { scope: "all", region: null };

const FILTERS_KEY = "rc.serverFilters";

export function loadFilters(): Filters {
  try {
    const raw = JSON.parse(localStorage.getItem(FILTERS_KEY) || "null");
    if (!raw || typeof raw !== "object") return NO_FILTERS;
    return {
      // the OFFICIAL / COMMUNITY tabs are hidden for now: a scope saved
      // before would hide servers with no way to see why
      scope: "all",
      region: typeof raw.region === "string" ? raw.region : null
    };
  } catch {
    return NO_FILTERS;
  }
}

export function saveFilters(f: Filters) {
  try {
    localStorage.setItem(FILTERS_KEY, JSON.stringify({ scope: f.scope, region: f.region }));
  } catch {
    // storage refused (a private window): the filters last the visit
  }
}

export function filtersOn(f: Filters): boolean {
  return f.scope !== "all" || f.region !== null;
}

// the EMPTY group open: for this tab's session (sessionStorage), folded again on a new visit
const EMPTY_OPEN_KEY = "rc.serverEmptyOpen";

export function loadEmptyOpen(): boolean {
  try {
    return sessionStorage.getItem(EMPTY_OPEN_KEY) === "1";
  } catch {
    return false;
  }
}

export function storeEmptyOpen(open: boolean) {
  try {
    if (open) sessionStorage.setItem(EMPTY_OPEN_KEY, "1");
    else sessionStorage.removeItem(EMPTY_OPEN_KEY);
  } catch {
    // storage refused: open for as long as the page is
  }
}

export function inScope(s: ServerView, scope: Scope): boolean {
  return scope === "all" || (scope === "official") === s.official;
}

export function applyFilters(servers: ServerView[], f: Filters, query: string): ServerView[] {
  const q = query.trim().toLowerCase();
  return servers.filter(
    (s) =>
      inScope(s, f.scope) &&
      (f.region === null || s.region === f.region) &&
      (!q || s.plain.includes(q) || s.description.toLowerCase().includes(q))
  );
}

// the values a choice cell cycles through: null (ANY) then what the list carries
export function choices(servers: ServerView[], field: "region"): Array<string | null> {
  const seen = new Set<string>();
  for (const s of servers) {
    const v = s[field];
    if (v) seen.add(v);
  }
  return [null, ...Array.from(seen).sort()];
}

// ---- bookmarked servers -------------------

// the key from when they were called saved: the bookmarks made then are kept
const SAVED_KEY = "rc.serverSaved";

export function loadBookmarks(): Set<string> {
  try {
    const raw = JSON.parse(localStorage.getItem(SAVED_KEY) || "[]");
    return new Set(Array.isArray(raw) ? raw.filter((k): k is string => typeof k === "string") : []);
  } catch {
    return new Set();
  }
}

export function storeBookmarks(bookmarks: ReadonlySet<string>) {
  try {
    localStorage.setItem(SAVED_KEY, JSON.stringify(Array.from(bookmarks)));
  } catch {
    // storage refused: bookmarked for the visit
  }
}
