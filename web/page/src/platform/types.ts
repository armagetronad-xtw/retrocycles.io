// The page <-> platform contract, typed: platform-contract/README.md section 1
// (repository root). Names and shapes verbatim; only what the page uses.

import type { IceServer } from "../aa/types";

// platform.json next to the page: the page's runtime configuration (not build time).
// `apiUrl`: where the platform lives; absent, unusable or the file missing -> community
// mode. `iceServers` / `iceTransportPolicy`: static ICE servers (STUN, or TURN with the
// operator's own static credentials) for every connect, the fallback when the platform
// hands out none (web/wasm/API.md "ICE servers"); a community host's way to its own relay.
export interface PageConfig {
  apiUrl: string | null; // e.g. "http://localhost:5080", no trailing slash
  iceServers: IceServer[];
  iceTransportPolicy: "all" | "relay";
}

// GET /v1/net/ice-servers: TURN with short-lived credentials (kept in memory for one connect)
export interface IceServersResponse {
  iceServers: IceServer[];
  expiresAt: number; // Unix seconds
}

// GET /v1/platform/config
export interface PlatformFeatures {
  guests: boolean;
  accounts: boolean;
  platformLogin: boolean;
  directConnect?: boolean; // the /v1/direct endpoints (probe, gateway, blocklist, reports); absent: off
  progression: boolean;
  moderation: boolean;
  ranked: boolean;
  quickplay?: boolean; // Quick Play asks POST /v1/quickplay for a room (absent on older platforms: off)
  analytics?: boolean; // the page sends its usage events, POST /v1/events (absent: off)
  moveOffers?: boolean; // GET /v1/me may carry moveOffer, a closer server (absent: off)
  linking?: boolean; // Linked IDs: link codes, /link in official rooms, /v1/me/linked-ids (absent: off)
  linkByPassword?: boolean; // with linking: link an old id with its password on the page
  gamePasswords?: boolean; // the platform's armathentication authority and /v1/me/game-passwords (absent: off)
  friends?: boolean; // friends, requests, blocks and a room's players
  mutes?: boolean; // mutes kept by the platform, in every room
  privateRooms?: boolean; // private 1v1 sumo duels: a link, or a challenge in a room; /v1/duels
  leaderboards?: boolean; // /v1/leaderboards: the boards, the death note's places
  news?: boolean; // /v1/me/news, the bell, and the site's /news pages (absent: off)
}

// an external sign-in provider the platform offers (the page offers exactly these)
export interface ProviderInfo {
  id: string; // "google", "discord", ...
  name: string; // "Google"
}

export interface PlatformConfig {
  apiVersion: string;
  authority: string;
  features: PlatformFeatures;
  providers?: ProviderInfo[];
  // the page's Google tag (googleTag.ts); absent: none
  googleAnalytics?: { measurementId: string; consentRequired: boolean };
}

// POST /v1/auth/guest, GET /v1/me
export interface PlatformUser {
  id: string;
  handle: string; // "g-7f3a9c01" for guests
  gid: string; // "g-7f3a9c01@retrocycles"
  name: string;
  isGuest: boolean;
  progression?: Progression; // GET /v1/me with features.progression (platform/progression.ts keeps it)
  moveOffer?: MoveOffer; // GET /v1/me with features.moveOffers, when a closer server is offered (platform/offers.ts)
}

// ---- offers of a closer server (contract section 1, "Offers")

export interface MoveOffer {
  id: string;
  region: string; // a region id of GET /v1/regions
  regionName: string; // "Sydney"
  serverName: string; // the room the group would go to (may carry colour codes)
  pingHere: number; // ms where the player plays now (the room's measurement)
  pingThere: number; // ms the page measured to the region
  accepted: number; // players near the region who accepted
  needed: number; // how many the group needs
  expiresAt: number; // Unix seconds
  status: string; // "open" | "accepted" | "moving" | ...
}

// ---- progression (contract section 1, "Progression"): the platform's numbers, never the page's

export interface ProgressionStats {
  matches: number; // finished (present at the end)
  rounds: number;
  wins: number; // rounds won (place 1)
  kills: number;
  deaths: number;
  seconds: number; // time played
}

// one XP entry; `id` grows with every entry, `amount` may be negative (a correction)
export interface XpEntry {
  id: number;
  time: number;
  amount: number;
  reason: string; // round_played, round_won, kill, match_finished, first_match_of_day, adjustment, ...
  match: string | null;
}

export interface LastMatch {
  id: string;
  endedAt: number;
  result: string; // "completed" | "aborted"
  place: number | null;
  players: number;
  rounds: number;
  xp: number;
  levelBefore: number;
  levelAfter: number;
}

export interface Progression {
  level: number;
  xp: number;
  levelXp: number; // total XP where this level began
  nextLevelXp: number; // total XP where the next begins
  stats: ProgressionStats;
  recent: XpEntry[]; // newest first, at most 20
  lastMatch: LastMatch | null;
}

// GET /v1/me/account
export interface AccountInfo extends PlatformUser {
  email: string | null;
  emailVerified: boolean;
  hasPassword: boolean;
  needsHandle: boolean; // an external sign-up with a suggested handle: POST /v1/auth/handle once
  logins: Array<{ provider: string; name: string }>;
  createdAt: number;
  newsEmails?: boolean; // asked for news emails (opt-in); absent from an older platform
  newsEmailsAsk?: boolean; // never answered the news emails question: the home asks once
}

// GET /v1/me/sessions, most recently used first
export interface SessionInfo {
  id: string;
  createdAt: number;
  lastSeenAt: number;
  device: string; // "Chrome on Windows"
  current: boolean;
  sharedComputer: boolean;
}

// POST /v1/me/link-codes: the one-time code /link takes in an official room
export interface LinkCode {
  code: string; // "K7Q-4M2"
  expiresAt: number;
  minutes: number; // how long it works
}

// ---- friends and mutes (contract section 1, "Friends", "Mutes")

// a player as the platform knows them: the account (or guest) its servers verified
export interface Person {
  userId: string;
  handle: string; // a guest's is its g-... id
  name: string; // the display name; the game's name may differ (Latin-1)
  isGuest: boolean;
  level: number | null; // null without features.progression
}

// where a friend is: from their room's roster (playing, spectating) or their page
export type PresenceState = "offline" | "playing" | "spectating" | "menu" | "queue" | "local" | "community";

export interface PresenceInfo {
  state: PresenceState;
  room: { serverId: string; name: string; mode: string | null } | null; // the official room, when they share it
  joinable: boolean; // POST /v1/quickplay {friend} can seat this player there
  color: string | null; // their cycle colour, "#rrggbb"
  lastSeen: number | null;
  challengeable?: boolean; // their page hears a 1v1 challenge now: POST /v1/duels {friendId}
}

export interface FriendInfo {
  user: Person;
  since: number;
  presence?: PresenceInfo | null; // in GET /v1/me/friends (absent on older platforms)
}

// GET/PUT /v1/me/privacy
export interface PrivacyInfo {
  appearOffline: boolean;
  friendsCanJoin: boolean;
  friendsCanMessage: boolean; // absent on older platforms: treat as true
}

// ---- direct messages (contract section 1 "Messages")

export interface MessageInfo {
  id: number;
  from: string; // user ids
  to: string;
  text: string;
  createdAt: number;
  read: boolean;
}

export interface ConversationInfo {
  user: Person;
  last: MessageInfo;
  unread: number;
}

export interface ConversationsList {
  conversations: ConversationInfo[];
  unread: number;
}

// what the page says it is doing (POST /v1/me/presence)
export type PageState = "menu" | "queue" | "local" | "community";

export interface FriendRequestInfo {
  id: number;
  user: Person;
  createdAt: number;
}

// GET /v1/me/friends
export interface FriendsList {
  friends: FriendInfo[];
  incoming: FriendRequestInfo[];
  outgoing: FriendRequestInfo[];
}

// POST /v1/me/friend-requests
export interface AskAnswer {
  state: "outgoing" | "friends";
  request: FriendRequestInfo | null;
  friend: FriendInfo | null;
}

// GET /v1/me/blocks, /v1/me/mutes
export interface ListedPerson {
  user: Person;
  createdAt: number;
}

export type FriendState = "self" | "friends" | "outgoing" | "incoming" | "none";

// GET /v1/rooms/{serverId}/players: one per verified player
export interface RoomPlayerInfo {
  netId: number;
  user: Person;
  friend: FriendState;
  muted: boolean;
  blocked: boolean;
  browser?: boolean; // plays in a browser: only they can be challenged to a duel
}

// GET /v1/me/linked-ids, oldest first
export interface LinkedIdInfo {
  gid: string; // "cycler@forums", as the room reported it
  linkedAt: number;
  serverId: string; // the room it was linked in, or "page" (linked with its password)
}

// GET /v1/me/game-passwords, oldest first (never the password)
export interface GamePasswordInfo {
  id: string;
  label: string;
  createdAt: number;
  lastUsedAt: number | null;
}

// POST /v1/me/game-passwords, POST /v1/me/game-passwords/{id}/reveal: shown, never kept by the page
export interface NewGamePassword {
  id: string;
  label: string;
  createdAt: number;
  password: string; // four words, spaced; typed with spaces, dashes or nothing between, any of the usual cases
  login: string; // "nova@retrocycles": what the desktop game logs in as
}

// GET /v1/auth/external/pending: "Link <providerName> to <handle>?"
export interface PendingLink {
  provider: string;
  providerName: string;
  handle: string;
  email: string;
}

// the popup's result (postMessage from the platform, or BroadcastChannel from the relay page)
export interface ExternalResult {
  type: "rc-auth";
  status: "signed_in" | "linked" | "confirm_link" | "error";
  provider?: string;
  needsHandle?: boolean;
  error?: string;
}

// GET /v1/regions: where Quick Play places players, with the URL the page times per region
export interface RegionInfo {
  id: string;
  name: string;
  pingUrl: string;
}

export interface RegionsResponse {
  regions: RegionInfo[];
  default: string;
}

// POST /v1/quickplay: the room, and a seat held in it until expiresAt (or until the
// player arrives, or the page releases it)
export interface QuickPlayAnswer {
  roomId: string;
  serverId: string;
  name: string; // may carry the game's colour codes
  signalUrl: string; // ws:/wss: URL or a path on the page's host, like a servers.json url
  platformLogin: boolean;
  region: string;
  reservationId: string;
  expiresAt: number; // Unix seconds
}

// POST /v1/private-rooms and .../{code}/join: a private 1v1 room and one way in
export interface PrivateRoomAnswer {
  code: string; // the invite link's code (?duel=<code>)
  serverId: string;
  name: string; // may carry the game's colour codes
  signalUrl: string;
  region: string;
  platformLogin: boolean;
  ticket: string; // single use: goes on the signalling URL as ?ticket=
  ticketExpiresAt: number; // Unix seconds
}

// a duel: a link anyone may answer (to null), or a challenge to one player in a room.
// Nobody is put in a room until it is accepted; then `room` is the private room both join
export interface DuelPerson {
  userId: string;
  name: string;
  isGuest: boolean;
}

// arrived: a link someone opened, waiting for its creator's GO (no room yet)
export type DuelState = "pending" | "arrived" | "accepted" | "declined" | "cancelled" | "expired";
// how an arrival at a link ended without a room (the link is open again)
export type DuelArrivalEnd = "not_now" | "no_answer" | "left";

export interface DuelView {
  code: string; // the link's code (?duel=<code>), and the duel's name in every call
  state: DuelState;
  from: DuelPerson;
  to: DuelPerson | null;
  createdAt: number;
  expiresAt: number;
  room: { code: string; serverId: string; name: string; region: string } | null; // code: POST /v1/private-rooms/{code}/join
  arrivedAt?: number | null; // a link: when its latest opener (`to`) arrived (Unix seconds)
  arrivalEnd?: DuelArrivalEnd | null; // a link pending again: how that arrival ended
}

// GET /v1/duels/wait: the challenges to me, and my own newest duel
// GET /v1/duels/settings: off, nobody can challenge me; a blocked player can't challenge me nor I them
export interface DuelSettings {
  acceptChallenges: boolean;
  blocked: Array<{ userId: string; name: string; since: number }>;
}

export interface DuelWait {
  cursor: string;
  incoming: DuelView[];
  outgoing: DuelView | null;
  joining?: DuelView | null; // the link I opened latest, while its creator decides (and a while after)
}

// POST /v1/duels/{code}/accept: the duel, and the way into its room for this page
export interface DuelAcceptAnswer {
  duel: DuelView;
  room: PrivateRoomAnswer | null; // null: a link I arrived at (its creator says GO)
}

// GET /v1/live: the people in the official rooms now (cached 15 s by the platform)
export interface LiveAnswer {
  players: number;
  rooms: number;
  minShown: number; // the page shows "N playing" from this many on
}

// POST /v1/auth/game-token: 60 s, single use, for one server. Never stored or logged.
export interface GameToken {
  token: string;
  expiresAt: number; // Unix seconds
}

// what the page knows about its platform
export type PlatformState =
  | { mode: "loading" }
  // no platform.json, the platform unreachable or malformed: the page behaves as if
  // every feature were false (the contract's "no platform URL configured")
  | { mode: "community" }
  // a platform answered; user is null without a session (guests off, the guest
  // bootstrap failed, or signed out: the next connect then makes a guest)
  | { mode: "platform"; apiUrl: string; config: PlatformConfig; user: PlatformUser | null };

// ---- leaderboards

// GET /v1/rooms/{serverId}/match: kills and deaths so far in an official room's current match, by name
export interface MatchTally {
  match: number;
  startedAt: string;
  ended: boolean;
  players: { name: string; kills: number; deaths: number }[];
}

export type BoardWindow = "today" | "week" | "all-time";
export type BoardSort = "kills" | "kd" | "wins";

// one row of a board; move: places gained since the snapshot (negative: lost), null with new
export interface BoardRow {
  rank: number;
  name: string;
  handle: string | null; // null on a guest's own row
  kills: number;
  deaths: number;
  kd: number;
  wins: number;
  move: number | null;
  new: boolean;
  you: boolean;
  saved: boolean; // false on a guest's own row (only they see it)
  color: string | null; // the player's cycle colour (#rrggbb) as their page last reported it; null: unknown
}

// GET /v1/leaderboards/{board}/{window}
export interface BoardPage {
  board: string;
  boardName: string;
  window: BoardWindow;
  sort: BoardSort;
  resetsAt: string | null;
  ranked: number;
  leader: BoardRow | null;
  from: number;
  rows: BoardRow[];
  me: BoardRow | null;
  killsToQualify: number | null;
}

// GET /v1/leaderboards
export interface BoardsInfo {
  boards: { id: string; name: string }[];
  windows: { id: BoardWindow; resetsAt: string | null }[];
  sorts: BoardSort[];
}

// GET /v1/leaderboards/me: the caller's places by kills (the death note, the match summary)
export interface MyPlace {
  rank: number;
  move: number | null;
  new: boolean;
  killsToNext: number | null; // in the kills sort (the death note)
  next: string | null;
  toNext: number | null; // what passing the one ahead takes in the sort's terms: kills, a K/D difference, wins
}

export interface MyPlaces {
  board: string;
  boardName: string;
  sort: BoardSort;
  saved: boolean;
  today: MyPlace | null;
  week: MyPlace | null;
  allTime: MyPlace | null;
}
