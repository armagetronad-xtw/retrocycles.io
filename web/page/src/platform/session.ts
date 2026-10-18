// The page's platform session: read platform.json, ask the platform what it offers,
// make sure a guest session exists, keep who is signed in (a small store the account
// UI updates), and mint login tokens and ICE servers for connections. Everything degrades to community mode (the page as it was before the
// platform) on any failure.
import type { ConnectOptions, IceServer, ServerEntry } from "../aa/types";
import { PlatformError, discordActivitySignIn, ensureGuest, getGameToken, getIceServers, getMe, getPlatformConfig, setBearer } from "./client";
import type { PageConfig, PlatformConfig, PlatformState, PlatformUser } from "./types";
import { DISCORD, EMBED, WEBSOCKET_TRANSPORT } from "../links";
import { DISCORD_API_URL, discordAuthenticate, discordAuthorize, setProxyHosts, watchDiscord } from "../discord";
import { currentLang, t } from "../i18n";

// aa_api.js' own filter (API.md "ICE servers") when it is loaded, else the same rule:
// urls a string or a list of stun:/stuns:/turn:/turns: URLs, username/credential strings
function iceServersFrom(list: unknown): IceServer[] {
  if (window.AA) return window.AA.normalizeIceServers(list);
  if (!Array.isArray(list)) return [];
  const out: IceServer[] = [];
  for (const s of list) {
    if (!s || typeof s !== "object") continue;
    const entry = s as { urls?: unknown; username?: unknown; credential?: unknown };
    const urls = (Array.isArray(entry.urls) ? entry.urls : [entry.urls]).filter(
      (u): u is string => typeof u === "string" && /^(stuns?|turns?):/.test(u)
    );
    if (!urls.length) continue;
    out.push({
      urls,
      ...(typeof entry.username === "string" ? { username: entry.username } : {}),
      ...(typeof entry.credential === "string" ? { credential: entry.credential } : {})
    });
  }
  return out;
}

// platform.json next to the page, e.g. {"apiUrl": "http://localhost:5080"} or, for a
// community host with its own relay, {"iceServers": [{"urls": "turn:...", "username":
// ..., "credential": ...}]}; runtime configuration so a static host needs nothing (no
// file -> community mode, no ICE servers)
async function readPageConfig(): Promise<PageConfig> {
  const none: PageConfig = { apiUrl: null, iceServers: [], iceTransportPolicy: "all" };
  try {
    const response = await fetch("./platform.json", { cache: "no-cache" });
    if (!response.ok) return none;
    const data: unknown = await response.json();
    if (!data || typeof data !== "object") return none;
    const raw = data as { apiUrl?: unknown; iceServers?: unknown; iceTransportPolicy?: unknown };
    const apiUrl = typeof raw.apiUrl === "string" && /^https?:\/\//.test(raw.apiUrl) ? raw.apiUrl.replace(/\/+$/, "") : null;
    return {
      apiUrl,
      iceServers: iceServersFrom(raw.iceServers),
      iceTransportPolicy: raw.iceTransportPolicy === "relay" ? "relay" : "all"
    };
  } catch {
    return none;
  }
}

let pageConfig: Promise<PageConfig> | null = null;

// once per page load
export function loadPageConfig(): Promise<PageConfig> {
  if (!pageConfig) pageConfig = readPageConfig();
  return pageConfig;
}

// ---- the store: what the page knows about its platform, and who is signed in ----
// The first answer comes from start() (once per page load); sign-in, sign-out, "New
// player" and refreshes replace the user. Components read it with usePlatform().

let current: PlatformState = { mode: "loading" };

// this load's page session and page_view for the platform's copy of it: the config
// call carries them, the sign-in call names the user. Set by analytics before the platform
// starts (startAnalytics); null: none (a test run)
export interface PageLoad {
  page: string;
  view: string;
  gpc: boolean;
}
let pageLoad: PageLoad | null = null;
export function setPageLoad(load: PageLoad | null) {
  pageLoad = load;
}
const listeners = new Set<() => void>();

function publish(next: PlatformState) {
  current = next;
  for (const listener of listeners) listener();
}

export function platformState(): PlatformState {
  return current;
}

export function subscribePlatform(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

// the signed-in user changed (sign-in, sign-up, sign-out, New player, a refresh)
export function setPlatformUser(user: PlatformUser | null) {
  if (current.mode !== "platform") return;
  const was = current.user;
  if (was === user || (was && user && was.id === user.id && was.gid === user.gid && was.name === user.name && was.isGuest === user.isGuest)) return;
  publish({ ...current, user });
}

// who the cookie says is signed in now (another tab, a revoked session, a popup that
// finished without reaching us); failures leave the state as it is
export async function refreshPlatformUser(): Promise<PlatformUser | null> {
  if (current.mode !== "platform") return null;
  try {
    const user = await getMe(current.apiUrl);
    setPlatformUser(user);
    return user;
  } catch (e) {
    console.info("platform: could not refresh the session (" + String(e instanceof Error ? e.message : e) + ")");
    return current.mode === "platform" ? current.user : null;
  }
}

async function start(): Promise<PlatformState> {
  const page = await loadPageConfig();
  if (!page.apiUrl) return { mode: "community" };
  // inside Discord the API is only reachable through the /api mapping (src/discord.ts)
  setProxyHosts(page.apiUrl);
  const apiUrl = DISCORD ? DISCORD_API_URL : page.apiUrl;

  let config: PlatformConfig;
  try {
    config = await getPlatformConfig(apiUrl, pageLoad);
  } catch (e) {
    console.info("platform: unreachable, community mode (" + String(e instanceof Error ? e.message : e) + ")");
    return { mode: "community" };
  }
  if (!config || typeof config !== "object" || !config.features) {
    console.info("platform: malformed config, community mode");
    return { mode: "community" };
  }
  if (!Array.isArray(config.providers)) config.providers = [];

  // in another site's frame (links.ts) the session cookie is a third-party one, which
  // browsers drop: no session, nothing that needs one. Quick Play, the ICE servers and the
  // usage events work without (an anonymous seat, a connect without a login token)
  const sessionless = () => {
    config.features = {
      ...config.features,
      guests: false,
      accounts: false,
      platformLogin: false,
      progression: false,
      moveOffers: false,
      linking: false,
      linkByPassword: false,
      gamePasswords: false,
      friends: false,
      mutes: false
    };
    return { mode: "platform" as const, apiUrl, config, user: null };
  };

  // a Discord Activity: Discord signs the player in (their account, made the first time) and
  // the session is a bearer token; no guests (one would need our cookie), and the sign-in
  // forms stay out of the way. Declined or failed: as the other embeds, without a session
  if (DISCORD) {
    try {
      const grant = await discordAuthorize();
      if (grant) {
        const session = await discordActivitySignIn(apiUrl, grant.code, grant.verifier, pageLoad?.page);
        setBearer(session.sessionToken);
        void discordAuthenticate(session.accessToken).then(watchDiscord);
        config.features = { ...config.features, guests: false, linkByPassword: false, gamePasswords: false };
        return { mode: "platform", apiUrl, config, user: session.me };
      }
    } catch (e) {
      console.info("platform: no Discord sign-in (" + String(e instanceof Error ? e.message : e) + ")");
    }
    return sessionless();
  }

  if (EMBED) return sessionless();

  // a guest session for every visitor (idempotent: a valid cookie returns its user);
  // without guests only an existing session counts
  let user: PlatformUser | null = null;
  try {
    user = config.features.guests ? await ensureGuest(apiUrl, pageLoad?.page) : await getMe(apiUrl, pageLoad?.page);
  } catch (e) {
    console.info("platform: no session (" + String(e instanceof Error ? e.message : e) + ")");
  }
  return { mode: "platform", apiUrl, config, user };
}

let started: Promise<PlatformState> | null = null;

// once per page load; every caller shares the same answer (the first state; later
// changes of the user are in platformState())
export function platformSession(): Promise<PlatformState> {
  if (!started)
    started = start().then((state) => {
      publish(state);
      return state;
    });
  return started;
}

// a session for the next connect: signed out (Sign out, a revoked session), the page
// makes a guest again, as on a first visit (also before Quick Play, so the seat is the
// user's and their login on the room releases it)
export async function sessionUser(): Promise<PlatformUser | null> {
  await platformSession();
  const platform = current;
  if (platform.mode !== "platform") return null;
  if (platform.user || !platform.config.features.guests) return platform.user;
  try {
    const user = await ensureGuest(platform.apiUrl);
    setPlatformUser(user);
    return user;
  } catch (e) {
    console.info("platform: no guest session (" + String(e instanceof Error ? e.message : e) + ")");
    return null;
  }
}

// the platform refused a login token because the user is banned from that server
// (contract: game-token 403 `banned`); the page shows it and does not connect
export class Banned extends Error {
  constructor(
    readonly until: number | null, // Unix seconds; null: permanent
    readonly reason: string // the platform's text for the player ("" if none)
  ) {
    super("banned");
    this.name = "Banned";
  }
}

const MONTHS = ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"];

// the ban's end on the player's own clock: in English written out in ASCII for the game's
// face ("2 oct 2026, 14:00"); in another language as Intl writes a short date and time there
function banUntil(d: Date): string {
  const lang = currentLang();
  if (lang !== "en") return new Intl.DateTimeFormat(lang, { day: "numeric", month: "short", year: "numeric", hour: "2-digit", minute: "2-digit", hourCycle: "h23" }).format(d);
  const two = (n: number) => String(n).padStart(2, "0");
  return d.getDate() + " " + MONTHS[d.getMonth()] + " " + d.getFullYear() + ", " + two(d.getHours()) + ":" + two(d.getMinutes());
}

// the ban in the reversed cell: until when and the platform's reason (its own words, as sent)
export function banText(ban: Banned): string {
  const text = ban.until !== null ? t("profile.ban.until", { when: banUntil(new Date(ban.until * 1000)) }) : t("profile.ban.forGood");
  return ban.reason ? t("profile.ban.withReason", { ban: text, reason: ban.reason }) : text;
}

// the login token to connect to `server` with, or null: only for servers.json entries
// that accept platform login, only with a platform session, fetched fresh every time,
// so it is always for whoever is signed in right now. Failures are a console note,
// never a reason not to connect, except a ban: that rejects with Banned.
export async function loginTokenFor(server: ServerEntry): Promise<string | null> {
  if (!server.platformLogin || !server.serverId) return null;
  await platformSession();
  if (current.mode !== "platform" || !current.config.features.platformLogin) return null;
  for (let attempt = 0; attempt < 2; attempt++) {
    const user = await sessionUser();
    if (!user || current.mode !== "platform") return null;
    try {
      const { token } = await getGameToken(current.apiUrl, server.serverId);
      return typeof token === "string" && token ? token : null;
    } catch (e) {
      // the session ended elsewhere (signed out everywhere, revoked, expired): once more as a guest
      if (e instanceof PlatformError && e.status === 401 && attempt === 0) {
        setPlatformUser(null);
        continue;
      }
      if (e instanceof PlatformError && e.status === 403 && e.code === "banned") {
        const until = e.problem.until;
        const reason = e.problem.reason;
        throw new Banned(typeof until === "number" && until > 0 ? until : null, typeof reason === "string" ? reason.trim() : "");
      }
      console.info("platform: no login token for " + server.serverId + ", connecting without (" + String(e instanceof Error ? e.message : e) + ")");
      return null;
    }
  }
  return null;
}

// the ICE servers for the next connect (API.md "ICE servers"), fetched fresh every time
// and never kept: the platform's relay with short-lived credentials when it has one,
// else the static list of platform.json, else none (the browser then needs outbound UDP
// to the server, as before). Failures are a console note, never a reason not to connect.
// On the WebSocket transport (links.ts WEBSOCKET_TRANSPORT) there is no peer connection to
// give ICE servers to. serverId: the room about to be joined (its region's relay). webSocket:
// this connect goes over the WebSocket because WebRTC could not get through (webrtcFallback.ts).
export async function iceOptionsFor(serverId?: string, webSocket = false): Promise<Pick<ConnectOptions, "iceServers" | "iceTransportPolicy" | "transport">> {
  if (WEBSOCKET_TRANSPORT || webSocket) return { transport: "websocket" };
  const page = await loadPageConfig();
  let iceServers: IceServer[] = [];
  await platformSession();
  const platform = current;
  if (platform.mode === "platform") {
    try {
      iceServers = iceServersFrom((await getIceServers(platform.apiUrl, serverId)).iceServers);
    } catch (e) {
      console.info("platform: no ICE servers (" + String(e instanceof Error ? e.message : e) + ")" + (page.iceServers.length ? ", using platform.json's" : ""));
    }
  }
  if (!iceServers.length) iceServers = page.iceServers;
  const options: Pick<ConnectOptions, "iceServers" | "iceTransportPolicy"> = {};
  if (iceServers.length) options.iceServers = iceServers;
  if (page.iceTransportPolicy === "relay") options.iceTransportPolicy = "relay";
  return options;
}
