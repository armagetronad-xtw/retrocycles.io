// The page <-> client contract, typed. Names and shapes are copied from
// web/wasm/API.md verbatim; only what the play page uses is declared here.
// The runtime object is the one aa_api.js builds (AA.prepare), a plain global.

export type SessionState =
  | "idle"
  | "connecting"
  | "syncing"
  | "playing"
  | "fetching"
  | "disconnected"
  | "local"
  | "scene";

export interface AaPlayer {
  id: number;
  name: string;
  coloredName: string;
  color: [number, number, number];
  score: number;
  team: string | null;
  teamId: number | null;
  nextTeamId: number | null;
  ping: number | null;
  local: boolean;
  bot?: boolean; // an AI player (absent from clients built before the field: treat as false)
  alive?: boolean; // riding now; false while dead or waiting to respawn (absent from clients built before the field)
  spectating: boolean;
  chatting: boolean;
  active: boolean;
}

// a WebRTC RTCIceServer as connect() takes it (API.md "ICE servers")
export interface IceServer {
  urls: string | string[]; // stun:, stuns:, turn:, turns: URLs
  username?: string;
  credential?: string;
}

// connect()'s options (API.md "Functions")
export interface ConnectOptions {
  loginToken?: string; // a platform login token for this connection (API.md "Platform login")
  iceServers?: IceServer[]; // STUN/TURN servers for this connection (API.md "ICE servers")
  iceTransportPolicy?: "all" | "relay";
  transport?: "webrtc" | "websocket"; // "websocket": game packets on the signalling socket (API.md "WebSocket transport")
}

// the selected ICE candidate pair of the current/last network session (state object `ice`)
export interface AaIceRoute {
  local: "host" | "srflx" | "prflx" | "relay" | "websocket" | string; // "relay": through a TURN server; "websocket": no WebRTC
  remote: string;
  protocol: string; // "udp" | "tcp"
  relayProtocol: "udp" | "tcp" | "tls" | null; // browser -> TURN server, for a relay
}

// "The state object" in API.md
export interface AaState {
  state: SessionState;
  reason: string;
  resource: string | null;
  url: string;
  ice: AaIceRoute | null;
  netstate: 0 | 1 | 2;
  ping: number | null;
  gametime: number | null;
  fps: number | null;
  synced: boolean;
  // a respawn game whose server holds a joining player's first cycle until this client said its
  // timer is in sync (RESPAWN_JOIN_READY, client_ready): seconds until it comes (below 0 once due),
  // null when none is due; absent on older bundles (API.md)
  joinIn?: number | null;
  authPending: boolean;
  sceneState: 0 | 1 | 2 | 3 | 4;
  sceneTransitioning: boolean;
  players: AaPlayer[];
}

export interface AaSceneEvent {
  state: 1 | 2 | 3 | 4;
  transitioning: boolean;
}

export interface AaConsoleEvent {
  line: string;
  text: string;
}

// the game's chat key (action CHAT) or a template instant chat: the page opens its chat line
export interface AaChatOpenEvent {
  prefill: string;
}

export interface AaErrorEvent {
  text: string;
}

// the `handshake` event: a network session's handshake reached a step (API.md "Events")
export interface AaHandshakeEvent {
  step: "signalling" | "ice";
}

// the `move` event: the room sends this client to another room (API.md "Moves")
export interface AaMoveEvent {
  moveId: string;
  serverId: string;
  name: string; // may carry the game's colour codes
  signalUrl: string;
  reason: string; // merge, drain, closer, admin
}

// the `round` event: the game's round rhythm (API.md "Events"): a round came into play on
// this client, or left play while the session goes on; they alternate, `started` first
export interface AaRoundEvent {
  state: "started" | "ended";
}

// one entry of getBindings() (API.md "Functions")
export interface AaBinding {
  keysym: number;
  keyName: string;
  code: string | null; // the DOM KeyboardEvent.code
  action: string; // "CYCLE_TURN_LEFT", ...
  player: number; // 1-4 for player actions, 0 for global ones
}

// the `authRequest` event: the server wants a password (API.md "Events"); never carries one
export interface AaAuthRequestEvent {
  username: string; // the account the server asks for ("name@forums")
  serverMessage: string; // its prompt ("Login with Authority forums")
  method: string; // the scrambling method
  failed: boolean; // the previous answer was refused
}

export interface AaMessageEvent {
  title: string;
  body: string;
  timeout: number;
}

// one entry of getActions(): a bindable action and the game's own words for it
export interface AaAction {
  name: string;
  kind: "player" | "camera" | "global";
  description: string;
  help: string;
  analog: boolean;
}

export interface AaConfigItem {
  name: string;
  value: string;
  savable: boolean;
  writable: boolean;
  requiredLevel?: string;
}

export type AudioState = "none" | "suspended" | "running" | "closed";

export type AaListener<T = unknown> = (data: T, type: string) => void;

// the subset of the API object the page calls (API.md "Functions")
// getCycle(): the watched cycle's meters (API.md "getCycle")
export interface AaCycle {
  own: boolean; // the player's own cycle (false: one watched while dead or spectating)
  alive: boolean;
  speed: number; // m/s
  rubber: number; // used, 0..rubberMax
  rubberMax: number; // CYCLE_RUBBER
  rubberMalus: number;
  brake: number; // the reservoir, 1 full .. 0 empty
  braking: boolean;
  // seconds until the player's dead cycle respawns (below 0 while it is on its way), null when
  // none is due; absent on older bundles
  respawnIn?: number | null;
}

export interface Aa {
  ready: boolean;
  persistence: "idbfs" | "memfs" | "none";
  connect(url: string, options?: ConnectOptions): boolean;
  platformLogin(token: string): boolean;
  // answer the pending authRequest; the wasm scrambles the password before it leaves
  auth(username: string, password: string): boolean;
  authCancel(): boolean;
  disconnect(): void;
  startLocal(): boolean;
  stopLocal(): void;
  scene(k: number): boolean;
  sceneStop(): void;
  getState(): AaState;
  // the cycle the camera follows, as the HUD reads it (API.md "getCycle"); absent on older bundles
  getCycle?(): AaCycle | null;
  // a turn as the turn keys make it: dir < 0 left, else right; count 2 a double turn (API.md "turn")
  turn?(dir: number, count: 1 | 2): number;
  // the brake held (true) or let go (false), as the brake key (API.md "brake")
  brake?(on: boolean): boolean;
  chat(text: string): boolean;
  setChatting(on: boolean): void;
  // chat() sends UTF-8 as typed (a room that carries it: an official one), else Latin-1;
  // absent from bundles built before
  setChatUtf8?(on: boolean): void;
  setPageScores?(on: boolean): void; // the page draws the score table; `scores` events
  getConfig(name: string): AaConfigItem | null;
  getBindings(): AaBinding[];
  // every bindable action (API.md "Functions"; Settings' key map)
  getActions(): AaAction[];
  // binds `key` (a DOM KeyboardEvent.code or an SDL keysym) to the action; replaces what the key did
  setBinding(action: string, player: number, key: string | number): boolean;
  unbind(key: string | number): boolean;
  keysymFromCode(code: string): number;
  codeFromKeysym(keysym: number): string | null;
  // user.cfg as the game would write it now, without PASSWORD lines; the browser's autoexec lines appended
  exportConfig(): string;
  console(line: string): boolean;
  setConfig(name: string, value: string): boolean;
  // every configuration value whose name starts with prefix (commands like SAY left out)
  listConfig(prefix: string): AaConfigItem[];
  saveConfig(): void;
  unlockAudio(): void;
  getAudioState(): AudioState;
  on(type: string, fn: AaListener<never>): () => void;
  off(type: string, fn: AaListener<never>): void;
  stripColors(text: string): string;
  colorSpans(text: string): Array<[string, string | null]>;
}

// the static half of aa_api.js
export interface AaStatic {
  prepare(module: EmscriptenModule, options?: { resourceProxy?: string; persist?: boolean }): Aa;
  stripColors(text: string): string;
  colorSpans(text: string): Array<[string, string | null]>;
  resolveSignalUrl(url: string): string;
  loadServers(url?: string): Promise<Array<{ name: string; url: string; description: string; serverId?: string; platformLogin?: boolean }>>;
  normalizeIceServers(list: unknown): IceServer[];
}

// what the page puts on the Emscripten Module object before client_opt.js runs
// (client.html's setup, API.md "Setup (page side)")
export interface EmscriptenModule {
  arguments: string[];
  canvas: HTMLCanvasElement;
  print(text: string): void;
  printErr(text: string): void;
  setStatus?(text: string): void;
  onExit?(code: number): void;
  onAbort?(what: unknown): void;
  onRuntimeInitialized?(): void;
  [key: string]: unknown;
}

// servers.json (API.md "servers.json"): name, url, description, and for platform
// login the server's `serverId` at the platform and `platformLogin: true` (the page
// sends a login token only to such entries). A `lobby` block is not part of the
// contract; it is read only when a feed supplies one.
export interface ServerEntry {
  name: string;
  url: string;
  description: string;
  serverId?: string;
  platformLogin?: boolean;
  lobby?: ServerLobby;
  // a private room: its invite code, and the ticket the platform's answer brought
  // (single use: the connect takes it, a later connect asks for a fresh one)
  privateCode?: string;
  ticket?: string;
  // the platform's board (GET /v1/servers): a trusted community server, verified by
  // its key and heartbeats (the lobby's `moderated` says the same where the board puts it there)
  moderated?: boolean | null;
  // a server reached by Direct Connect, not from the board
  direct?: {
    key: string; // the address as kept: host:port, or the wss:// URL
    host: string;
    gateway: boolean; // through the platform's signalling gateway: WebRTC only
    signalPort: number | null;
    browser: "ok" | "closed" | "unknown" | null; // the probe's word on its browser port
  };
}

// what a lobby feed may say about a server; every
// field is optional, and the board shows a dash for what it does not say
export interface ServerLobby {
  players?: number; // all players (the master server's count; bots included)
  humans?: number; // players who are not bots
  bots?: number;
  active?: number; // humans playing, not spectating
  maxPlayers?: number;
  ping?: number; // ms
  pingEstimated?: boolean; // by region, not measured to this server
  region?: string;
  official?: boolean;
  moderated?: boolean; // verified on our platform (a heartbeat with a server key), never self-declared
  browser?: boolean; // false: no signalling URL, desktop clients only (the board leaves it out)
  webSocket?: boolean; // the room takes game packets over its signalling WebSocket: the only way in from Discord
  pingUrl?: string; // a host near the server that answers CORS HEAD requests: the page times it for the ping
  version?: string; // the release without its build tail (0.2.9-sty+ct+ap+io)
  release?: string; // the release as the server writes it (0.2.9-sty+ct+ap+io unix dedicated)
  friends?: number;
  // handle: the account handle of a player logged in with this platform (null: a guest, another login, none)
  roster?: Array<{ name?: string; handle?: string | null; color?: string | null; score?: number | null; bot?: boolean; friend?: boolean }>;
}

declare global {
  interface Window {
    AA?: AaStatic;
    Module?: EmscriptenModule;
  }
}
