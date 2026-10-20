// First-party usage events for the platform's funnel numbers (platform-contract/README.md,
// "Analytics events"). Tiny on purpose: a queue, a batch every 10 s, the rest
// by navigator.sendBeacon when the page is hidden or closed. It never blocks play and never
// retries: a failed send is dropped silently.
//
// - Only with a platform whose `features.analytics` is on. Community mode (no platform.json,
//   an unreachable platform, the feature off) sends nothing: events made before the platform
//   answered are dropped with the queue. A browser with Global Privacy Control sends nothing.
//   Where the platform says consent is needed (config googleAnalytics.consentRequired: the EEA,
//   the UK, Switzerland, or a country it cannot tell), nothing is sent until the cookie banner's
//   Accept (googleTag.ts): the events wait in the queue meanwhile, Decline drops them. The X
//   embed and Discord never show the banner, so there such visitors send nothing.
//   There is no switch in the Profile to turn usage statistics off yet.
// - The page session id is random per page load and lives only in this module's memory (no
//   storage, no cookie). The user is the platform's session cookie, never named in the body.
// - An automated browser (navigator.webdriver: Playwright, Selenium, headless checks) sends
//   nothing either, so test runs do not count as players; `?analytics=debug` still logs there.
// - Props are the contract's: codes, numbers and booleans only. Never a name, a reason as the
//   client words it, a URL beyond an origin, a token or a credential.
// - `?analytics=debug` logs every batch to the console ("analytics: {...}") instead of sending
//   it; `<html data-analytics>` says pending/on/debug/off.
import type { SaveTrigger } from "./savePrompt";
import { platformSession, platformState, setPageLoad } from "./session";
import { DISCORD, EMBED } from "../links";
import { cookieChoice, gaEvent, onCookieChoice } from "../googleTag";
import { type ErrorWhere, whereOfFile, whereOfStack } from "./errorWhere";

export type EventName =
  | "page_view"
  | "play_pressed"
  | "bundle_loaded"
  | "scene_ready"
  | "quickplay_result"
  | "connect_stage"
  | "first_turn"
  | "claim_started"
  | "claim_done"
  | "save_prompt"
  | "save_pressed"
  | "session_end"
  | "first_action"
  | "region_latency"
  | "play_stats"
  | "move_result"
  | "move_offer"
  | "room_joined"
  | "link_copied"
  | "full_game_opened"
  | "client_error";

export type Props = Record<string, string | number | boolean | null | undefined>;

interface QueuedEvent {
  seq: number;
  name: EventName;
  t: number;
  props: Record<string, string | number | boolean>;
}

const FLUSH_MS = 10000;
const MAX_BATCH = 50; // the platform's limit
const MAX_QUEUE = 500; // while the platform has not answered yet: later events are dropped

type Mode = "pending" | "on" | "debug" | "off";

let mode: Mode = "pending";
let debug = false;
let seq = 0;
let queue: QueuedEvent[] = [];
let timer = 0;
let started = false;

// the page session: 128 random bits, base64url, this page load only
const page = (() => {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
})();

function setMode(next: Mode) {
  mode = next;
  document.documentElement.dataset.analytics = next;
}

function clean(props: Props | undefined): Record<string, string | number | boolean> {
  const out: Record<string, string | number | boolean> = {};
  if (!props) return out;
  for (const [key, value] of Object.entries(props)) {
    if (value === null || value === undefined) continue;
    out[key] = typeof value === "number" ? Math.max(0, Math.round(value)) : value;
  }
  return out;
}

// queue one event; nothing happens (and nothing is kept) once analytics are off
export function track(name: EventName, props?: Props) {
  // Google Analytics' copy of the few it has (googleTag.ts), whatever this module's mode
  gaEvent(name, props);
  if (mode === "off") return;
  if (queue.length >= MAX_QUEUE) return;
  queue.push({ seq: ++seq, name, t: Date.now(), props: clean(props) });
  schedule();
}

// milliseconds since the page's navigation start (the click on the link)
export function sinceNavigation(): number {
  return Math.round(performance.now());
}

function schedule() {
  if ((mode !== "on" && mode !== "debug") || timer || !queue.length) return;
  timer = window.setTimeout(() => {
    timer = 0;
    flush(false);
  }, FLUSH_MS);
}

// send what is queued: fetch (keepalive) normally, sendBeacon when the page is going away
export function flush(leaving: boolean) {
  if (mode !== "on" && mode !== "debug") return;
  if (timer) {
    window.clearTimeout(timer);
    timer = 0;
  }
  const platform = platformState();
  while (queue.length) {
    const events = queue.splice(0, MAX_BATCH);
    const body = JSON.stringify({ page, events });
    if (mode === "debug" || platform.mode !== "platform") {
      if (mode === "debug") console.info("analytics: " + body);
      continue;
    }
    const url = platform.apiUrl + "/v1/events";
    try {
      // text/plain: a CORS "simple" request (no preflight) that a beacon can send too; the
      // platform checks the Origin instead of a CSRF header (the contract)
      if (leaving && typeof navigator.sendBeacon === "function" && navigator.sendBeacon(url, body)) continue;
      void fetch(url, {
        method: "POST",
        body,
        credentials: "include",
        keepalive: true,
        cache: "no-store",
        headers: { "Content-Type": "text/plain;charset=UTF-8" }
      }).catch(() => {});
    } catch {
      // dropped: analytics never cost the player anything
    }
  }
}

// ---- what the page reports about itself --------------------------------------------------

// a browser driven by automation (WebDriver, Playwright, Puppeteer): a test run, not a player
export function automated(): boolean {
  return navigator.webdriver === true;
}

export function deviceClass(): "phone" | "tablet" | "desktop" {
  const coarse = window.matchMedia?.("(pointer: coarse)").matches ?? false;
  const shortest = Math.min(window.screen?.width || window.innerWidth, window.screen?.height || window.innerHeight);
  if (!coarse) return "desktop";
  return shortest < 600 ? "phone" : "tablet";
}

// the family only, from the user agent (which is not sent)
function browserFamily(): "chrome" | "firefox" | "safari" | "edge" | "other" {
  const ua = navigator.userAgent;
  if (/Edg(e|A|iOS)?\//.test(ua)) return "edge";
  if (/Firefox\/|FxiOS\//.test(ua)) return "firefox";
  if (/Chrome\/|CriOS\/|Chromium\//.test(ua)) return "chrome";
  if (/Safari\//.test(ua)) return "safari";
  return "other";
}

// the campaign code of `?c=` when it is one (the contract's pattern), else nothing
function campaign(): string | undefined {
  const c = new URLSearchParams(location.search).get("c");
  const code = c ? c.trim().toLowerCase() : "";
  return /^[a-z0-9_-]{1,32}$/.test(code) ? code : undefined;
}

// the link id of `?l=` beside a campaign code (a creator's link forwards with both), else nothing
function link(): string | undefined {
  const l = (new URLSearchParams(location.search).get("l") ?? "").trim().toLowerCase();
  return campaign() && /^[a-z0-9_-]{1,32}$/.test(l) ? l : undefined;
}

// the link's utm_source / utm_medium / utm_campaign (a post, an ad, a newsletter), lower case with
// blanks as "-", each only when it is then a plain code (the contract's pattern): an address, a
// path or anything else stays out
function utm(): { utmSource?: string; utmMedium?: string; utmCampaign?: string } {
  const query = new URLSearchParams(location.search);
  const code = (name: string) => {
    const v = (query.get(name) ?? "").trim().toLowerCase().replace(/\s+/g, "-");
    return /^[a-z0-9][a-z0-9._-]{0,63}$/.test(v) ? v : undefined;
  };
  return { utmSource: code("utm_source"), utmMedium: code("utm_medium"), utmCampaign: code("utm_campaign") };
}

// the referring site's origin, only when it is another site
function referrer(): string | undefined {
  try {
    if (!document.referrer) return undefined;
    const url = new URL(document.referrer);
    if (url.origin === location.origin) return undefined;
    const origin = (url.protocol + "//" + url.host).toLowerCase();
    return /^https?:\/\/[a-z0-9.-]{1,100}(:[0-9]{1,5})?$/.test(origin) ? origin : undefined;
  } catch {
    return undefined;
  }
}

// ---- the session: matches and rounds for session_end -------------------------------------
// matches: games joined (the contract's meaning: a session reaching `playing` or `local`,
// counted once per session by App.tsx); rounds: the API's `round` events with state
// "started" (a round coming into play on this client, the joined one included), the one
// definition the XP line uses too. Matches as the platform scores them (match_ended) are
// the platform's; the page sees them as `/v1/me` progression.lastMatch.

let matches = 0;
let rounds = 0;
let endSent = false;

export function countMatch() {
  matches++;
}

export function countRound() {
  rounds++;
}

function sessionEnd() {
  if (endSent) return;
  endSent = true;
  endGame(true); // the running game's ping so far goes with the last batch
  const now = performance.now();
  track("session_end", {
    ms: sinceNavigation(),
    matches,
    rounds,
    screen,
    screenMs: now - screenAt,
    last: last ?? undefined,
    lastMs: last ? now - lastAt : undefined,
    actions
  });
  flush(true);
}

// ---- where the page was and what was last pressed, for session_end ----------------------------
// screen: the page's view as App.tsx names it (home, servers, game, game_menu, profile, ...),
// set on every change. last: the last control pressed, "<screen>:<control>", where the control
// is its data-track, else its first class name (the page's own words, never its text: a row's
// text can be a server's or a player's name), or a code App.tsx notes for what keys do. Both
// stay in memory and only go out with session_end. The first press of the page load goes out
// at once as first_action (what a visitor tries first: Training, Quick Play, the Server Browser).

const CODE = /^[a-z0-9_.:-]{1,64}$/;
let screen = "loading";
let screenAt = 0;
let last: string | null = null;
let lastAt = 0;
let actions = 0;
let firstNoted = false;

export function setScreen(next: string) {
  if (next === screen || !CODE.test(next)) return;
  screen = next;
  screenAt = performance.now();
}

// something the player did, in the screen it was done in
export function noteAction(control: string) {
  const code = (screen + ":" + control).toLowerCase();
  if (!CODE.test(code)) return;
  last = code;
  lastAt = performance.now();
  actions++;
  // the first press, once its own handlers have noted theirs too (a menu tile's click notes its
  // class name first, then App.tsx the mode it picked): that press's last, most specific code.
  // The cookie banner's buttons answer a question rather than choose something: not counted
  if (!firstNoted && !/^[^:]*:ck-/.test(code)) {
    firstNoted = true;
    const at = sinceNavigation();
    window.setTimeout(() => track("first_action", { control: last ?? code, ms: at }), 0);
  }
}

const PRESSABLE = "[data-track],button,a[href],[role=button],[role=option],[role=tab],[role=switch],[role=menuitem],input,select,textarea,label";

function onPress(e: Event) {
  const target = e.target instanceof Element ? e.target.closest(PRESSABLE) : null;
  if (!target) return;
  const control = target.getAttribute("data-track") || target.classList.item(0) || target.tagName.toLowerCase();
  noteAction(control);
}

// ---- play_stats: one network game's ping, as the page measured it ----------------
// App.tsx starts a game when a connect attempt reaches `playing`, samples getState().ping
// every few seconds while it runs, and ends it at `disconnected` (a leave, a lost
// connection). Hiding the page ends it too (the last batch may be the page's last word); a
// game still running when the page shows again goes on as a new part of the same attempt.

const MAX_PING_SAMPLES = 3600;

interface GameStats {
  attempt: number;
  region?: string; // the room's region from the Quick Play answer; none for the server browser
  startedAt: number;
  pings: number[];
  fps: number[]; // the client's frame rate at the same readings
}

let game: GameStats | null = null;
let resumable: { attempt: number; region?: string } | null = null;

export function startGame(attempt: number, region?: string | null) {
  endGame(false);
  game = { attempt, region: region && /^[a-z0-9_-]{1,32}$/.test(region) ? region : undefined, startedAt: performance.now(), pings: [], fps: [] };
}

// one reading of the client's ping (ms; null when it has none yet) and frame rate (getState().fps)
export function sampleGame(ping: number | null | undefined, fps?: number | null) {
  if (!game) return;
  if (fps !== null && fps !== undefined && Number.isFinite(fps) && fps > 0 && game.fps.length < MAX_PING_SAMPLES) game.fps.push(Math.min(1000, fps));
  if (ping === null || ping === undefined || !Number.isFinite(ping) || ping < 0) return;
  if (game.pings.length < MAX_PING_SAMPLES) game.pings.push(Math.min(60000, ping));
}

// nearest rank of a sorted list
function rank(sorted: number[], p: number): number {
  return sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil(p * sorted.length) - 1))];
}

// the game is over (or the page is hidden: `hidden`, it may go on when the page shows again)
export function endGame(hidden = false) {
  const g = game;
  game = null;
  resumable = hidden && g ? { attempt: g.attempt, region: g.region } : null;
  if (!g || !g.pings.length) return;
  const sorted = [...g.pings].sort((a, b) => a - b);
  const fps = [...g.fps].sort((a, b) => a - b);
  track("play_stats", {
    attempt: g.attempt,
    region: g.region,
    pingMedian: rank(sorted, 0.5),
    pingP90: rank(sorted, 0.9),
    seconds: Math.min(86400, (performance.now() - g.startedAt) / 1000),
    // the frame rate the game ran at: its median and its slowest 5 % (stutter)
    fpsMedian: fps.length ? Math.round(rank(fps, 0.5)) : undefined,
    fpsP5: fps.length ? Math.round(rank(fps, 0.05)) : undefined
  });
}

// ---- client_error: what broke, never the message (no free text leaves the page) ----------------
// A lost WebGL context, a wasm abort (out of memory or another), an uncaught script error or
// promise rejection from the page's own files. At most a few per page load. A script error or a
// rejection says which of our files it came from (where: errorWhere.ts), a fixed code.

export type ClientErrorKind = "webgl_lost" | "out_of_memory" | "wasm_abort" | "script" | "promise";
const MAX_ERRORS = 5;
let errorsSent = 0;

export function clientError(kind: ClientErrorKind, where?: ErrorWhere) {
  if (errorsSent >= MAX_ERRORS) return;
  errorsSent++;
  track("client_error", { kind, ms: sinceNavigation(), where });
}

export function watchErrors(canvas: HTMLCanvasElement | null) {
  canvas?.addEventListener("webglcontextlost", () => clientError("webgl_lost"));
  // only the page's own scripts: an extension's or a Google tag's errors are not ours
  window.addEventListener("error", (e) => {
    if (e.target !== window) return;
    const where = whereOfFile(e.filename, location.origin);
    if (where) clientError("script", where);
  });
  window.addEventListener("unhandledrejection", (e) => {
    const reason: unknown = e.reason;
    const where = whereOfStack(reason instanceof Error ? reason.stack : undefined, location.origin);
    if (where) clientError("promise", where);
  });
}

function resumeGame() {
  if (!resumable || game) return;
  const { attempt, region } = resumable;
  resumable = null;
  startGame(attempt, region);
}

// once per page load, as early as possible: the page_view, then the platform decides
export function startAnalytics() {
  if (started) return;
  started = true;
  const asked = new URLSearchParams(location.search).get("analytics");
  debug = asked === "debug";
  const gpc = (navigator as Navigator & { globalPrivacyControl?: boolean }).globalPrivacyControl === true;
  // embed: the page in a social post's frame (links.ts), so the platform can tell the X embed from the page itself
  const view: Props = { campaign: campaign(), link: link(), referrer: referrer(), ...utm(), device: deviceClass(), browser: browserFamily(), ...(DISCORD ? { discord: true } : EMBED ? { embed: true } : {}) };
  // the platform's copy of this load: its first call carries the page session and the
  // page_view, so a load whose events never arrive (a blocker, Global Privacy Control, the cookie
  // question not accepted) still counts. Not for test runs, nor in debug (logged instead)
  const automatedRun = automated();
  if (debug) console.info("analytics: page load " + JSON.stringify({ page, view: clean(view), gpc }));
  else if (!automatedRun) setPageLoad({ page, view: JSON.stringify(clean(view)), gpc });
  if (gpc || (automatedRun && !debug)) {
    setMode("off");
    return;
  }
  setMode("pending");
  track("page_view", view);

  // hidden: the last batch goes now (a phone may never come back to the tab); the
  // session's numbers so far go with it, again at every hide (the platform keeps the last)
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") sessionEnd();
    else {
      endSent = false;
      resumeGame();
    }
  });
  window.addEventListener("pagehide", sessionEnd);
  // capture: before the control's own handler, which may note something more exact
  document.addEventListener("click", onPress, true);

  platformSession().then((state) => {
    const on = state.mode === "platform" && state.config.features.analytics === true;
    if (!on) {
      queue = [];
      setMode("off");
      return;
    }
    const required = state.config.googleAnalytics?.consentRequired === true;
    const choice = cookieChoice();
    if (required && choice !== "granted") {
      if (choice === "denied") queue = [];
      setMode(choice === "denied" ? "off" : "pending");
    } else {
      setMode(debug ? "debug" : "on");
      schedule();
    }
    if (required) {
      onCookieChoice((next) => {
        if (next === "granted") {
          setMode(debug ? "debug" : "on");
          schedule();
        } else {
          queue = [];
          setMode("off");
        }
      });
    }
  });
}

// ---- connect attempts: connect_stage / quickplay_result ------------------------------------

export type Stage = "quickplay" | "signalling" | "ice" | "handshake" | "sync";

export type Reason =
  | "cancelled"
  | "timeout"
  | "signalling"
  | "ice"
  | "login_denied"
  | "login_timeout"
  | "no_game_state"
  | "connection_lost"
  | "full"
  | "refused"
  | "error"
  | "other";

export type ConnectMode = "quick" | "browser";

export interface IceRoute {
  local: string;
  relayProtocol: string | null;
}

let attempts = 0;

// the code for a failed stage, from the client's reason (API.md state machine); the text itself is never sent
export function reasonOf(clientReason: string, stage: Stage): Reason {
  const r = clientReason.toLowerCase();
  if (/too many players|server (is )?full|\bfull\b/.test(r)) return "full";
  if (r.startsWith("data channel did not open") || r === "handshake not started") return stage === "ice" ? "ice" : "signalling";
  if (r.startsWith("login denied")) return "login_denied";
  if (r.startsWith("login timeout")) return "login_timeout";
  if (r.startsWith("no game state")) return "no_game_state";
  if (r.startsWith("connection lost")) return "connection_lost";
  if (r.startsWith("exception")) return "error";
  return "other";
}

/* One connect attempt (a Quick Play press, or a server chosen in the browser): each stage it
   reaches is reported once, ok with its own duration, or the one that failed with a reason;
   nothing after the end. The route comes from getState().ice once the channel is open. */
export class ConnectAttempt {
  readonly attempt: number;
  readonly mode: ConnectMode;
  readonly pressedAt = performance.now();
  private stage: Stage | null = null;
  private stageAt = 0;
  private ended = false;
  private route: IceRoute | null = null;
  private quickTries = 0;
  private noRoomSent = false;

  // transport "websocket": this connect goes over the signalling WebSocket because WebRTC failed
  // earlier (webrtcFallback.ts)
  constructor(mode: ConnectMode, transport?: "websocket") {
    this.attempt = ++attempts;
    this.mode = mode;
    track("play_pressed", { mode, attempt: this.attempt, transport });
  }

  get done(): boolean {
    return this.ended;
  }

  get current(): Stage | null {
    return this.stage;
  }

  begin(stage: Stage) {
    if (this.ended) return;
    this.stage = stage;
    this.stageAt = performance.now();
  }

  setRoute(ice: IceRoute | null) {
    if (ice && ice.local) this.route = ice;
  }

  private routeProps(): Props {
    if (!this.route) return {};
    const relay = this.route.local === "relay";
    const protocol = this.route.relayProtocol;
    return { route: relay ? "relay" : this.route.local === "websocket" ? "websocket" : "direct", relayProtocol: relay && protocol && /^(udp|tcp|tls)$/.test(protocol) ? protocol : undefined };
  }

  // the current stage completed (it must be `stage`: a late event of another stage is ignored)
  ok(stage: Stage) {
    if (this.ended || this.stage !== stage) return;
    track("connect_stage", { attempt: this.attempt, stage, ok: true, ms: performance.now() - this.stageAt, ...this.routeProps() });
    this.stage = null;
    if (stage === "sync") this.ended = true;
  }

  // the current stage failed: the attempt is over
  fail(reason: Reason) {
    if (this.ended) return;
    this.ended = true;
    if (!this.stage) return;
    track("connect_stage", { attempt: this.attempt, stage: this.stage, ok: false, ms: performance.now() - this.stageAt, reason, ...this.routeProps() });
  }

  // Quick Play through the platform: the finding step's news
  quickTry() {
    this.quickTries++;
  }

  quickNoRoom() {
    if (this.noRoomSent || this.ended) return;
    this.noRoomSent = true;
    track("quickplay_result", { attempt: this.attempt, result: "no_room", ms: performance.now() - this.pressedAt, tries: this.quickTries });
  }

  quickResult(result: "assigned" | "giveup" | "error" | "cancelled", region?: string | null) {
    if (this.ended) return;
    track("quickplay_result", {
      attempt: this.attempt,
      result,
      ms: performance.now() - this.pressedAt,
      tries: this.quickTries,
      region: region && /^[a-z0-9_-]{1,32}$/.test(region) ? region : undefined
    });
  }
}

// ---- guest -> account ----------------------------------------------------------------------

export type ClaimKind = "signup" | "signin";

// a guest started / finished making an account (method: "password" or a provider id)
export function claim(done: boolean, method: string, kind: ClaimKind) {
  const code = method.toLowerCase();
  if (!/^[a-z0-9_-]{1,32}$/.test(code)) return;
  track(done ? "claim_done" : "claim_started", { method: code, kind, from: claimFrom?.from, trigger: claimFrom?.trigger });
}

// ---- the ask to save (savePrompt.ts) ----------------------------------------------------------

export type ClaimFrom = "summary" | "chip" | "leaderboard";

// where the Profile that a claim starts in was opened from: the match summary's "save your
// progress" (with the ask's trigger) or the chip; kept until the next opening
let claimFrom: { from: ClaimFrom; trigger?: SaveTrigger } | null = null;

export function claimOpened(from: ClaimFrom, trigger?: SaveTrigger) {
  claimFrom = { from, trigger };
  if (from === "summary" && trigger) track("save_pressed", { trigger });
}

// an offer of a closer server at a round end: shown, accepted, dismissed, or gone
// unanswered (its 10 s passed, or the next round started)
export function offerEvent(action: "shown" | "accepted" | "dismissed" | "expired", region: string) {
  if (/^[a-z0-9_-]{1,32}$/.test(region)) track("move_offer", { action, region });
}

// a guest's match summary asked to save (matches: the guest's finished matches)
export function savePrompt(trigger: SaveTrigger, matches: number) {
  track("save_prompt", { trigger, matches });
}
