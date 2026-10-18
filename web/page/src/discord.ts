// The page inside a Discord Activity (links.ts DISCORD). Discord serves it from
// https://<client id>.discordsays.com through its proxy and lets it reach nothing else: every
// other host has to go through the app's URL mappings, set in Discord's Developer Portal:
//
//   /api        -> the platform API's host           (api.retrocycles.io)
//   /gs/{host}  -> {host}.<the API's parent domain>   (a game host: signalling + game)
//   /           -> the page                           (retrocycles.io/discord)
//
// throughProxy() turns the absolute URLs the platform and servers.json hand out into those
// paths. The SDK (@discord/embedded-app-sdk) is loaded here alone, on demand, so it never
// weighs on the page outside Discord. Sign-in: authorize() in Discord (its consent sheet the
// first time), the code to the platform (platform/session.ts), authenticate() with the
// access token it hands back. The platform session is then a bearer token kept in memory
// (platform/client.ts): Discord's frame has no cookie of ours.
import type { DiscordSDK } from "@discord/embedded-app-sdk";
import { useSyncExternalStore } from "react";
import { DISCORD } from "./links";
import { hintLang, t } from "./i18n";

const query = new URLSearchParams(location.search);

// the Activity's application: the first label of <client id>.discordsays.com
export const DISCORD_CLIENT_ID: string = DISCORD ? location.hostname.split(".")[0] : "";

// everyone in one launch of the Activity shares it (Quick Play puts them in one room)
export const DISCORD_INSTANCE: string = DISCORD ? query.get("instance_id") ?? "" : "";

// "desktop" or "mobile", as Discord says
export const DISCORD_PLATFORM: string = DISCORD ? query.get("platform") ?? "" : "";

// the platform API through the /api mapping
export const DISCORD_API_URL: string = DISCORD ? location.origin + "/api" : "";

const READY_TIMEOUT_MS = 10000;

let sdk: Promise<DiscordSDK | null> | null = null;

// the SDK, handshaken with Discord; null outside Discord or when it does not answer
export function discordSdk(): Promise<DiscordSDK | null> {
  if (!DISCORD) return Promise.resolve(null);
  if (!sdk)
    sdk = import("@discord/embedded-app-sdk")
      .then(async ({ DiscordSDK }) => {
        const client = new DiscordSDK(DISCORD_CLIENT_ID, { disableConsoleLogOverride: true });
        let timer = 0;
        const timeout = new Promise<never>((_, reject) => {
          timer = window.setTimeout(() => reject(new Error("no answer from Discord")), READY_TIMEOUT_MS);
        });
        try {
          await Promise.race([client.ready(), timeout]);
        } finally {
          window.clearTimeout(timer);
        }
        return client;
      })
      .catch((e) => {
        console.info("discord: the SDK is not there (" + String(e instanceof Error ? e.message : e) + ")");
        return null;
      });
  return sdk;
}

// ---- the URL mappings ----

// the platform API's parent domain ("retrocycles.io" for https://api.retrocycles.io); the
// /gs/{host} mapping reaches its subdomains. Set once platform.json is read.
let parentDomain = "";
let apiHost = "";

export function setProxyHosts(apiUrl: string | null) {
  if (!apiUrl) return;
  try {
    apiHost = new URL(apiUrl).hostname.toLowerCase();
    parentDomain = apiHost.split(".").slice(1).join(".");
  } catch {
    apiHost = parentDomain = "";
  }
}

// an absolute http(s)/ws(s) URL on the API's host or a sibling of it, as the mapped path on
// this origin; anything else unchanged (outside Discord: always unchanged)
export function throughProxy(url: string): string {
  if (!DISCORD || !parentDomain) return url;
  let parsed: URL;
  try {
    parsed = new URL(url);
  } catch {
    return url;
  }
  const host = parsed.hostname.toLowerCase();
  let prefix: string;
  if (host === apiHost) prefix = "/api";
  else if (host.endsWith("." + parentDomain) && /^[a-z0-9-]+$/.test(host.slice(0, -parentDomain.length - 1)))
    prefix = "/gs/" + host.slice(0, -parentDomain.length - 1);
  else return url;
  // the scheme of this origin (Discord's is https; a local check's may be http)
  const secure = location.protocol === "https:";
  const socket = parsed.protocol === "ws:" || parsed.protocol === "wss:";
  const scheme = socket ? (secure ? "wss://" : "ws://") : secure ? "https://" : "http://";
  return scheme + location.host + prefix + parsed.pathname + parsed.search;
}

// ---- sign-in ----

function base64url(bytes: Uint8Array): string {
  let text = "";
  for (const b of bytes) text += String.fromCharCode(b);
  return btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export interface DiscordGrant {
  code: string;
  verifier: string; // PKCE: the platform sends it with the code ("" without PKCE)
}

// Discord's authorization code for this app (identify, email); the consent sheet the first
// time, silent after. With PKCE where WebCrypto is there (a secure context, which Discord's
// https origin is). null outside Discord; throws when the player declines.
export async function discordAuthorize(): Promise<DiscordGrant | null> {
  const client = await discordSdk();
  if (!client) return null;
  let verifier = "";
  let pkce = {};
  if (globalThis.crypto?.subtle) {
    verifier = base64url(crypto.getRandomValues(new Uint8Array(32)));
    const challenge = base64url(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))));
    pkce = { code_challenge: challenge, code_challenge_method: "S256" as const };
  }
  const { code } = await client.commands.authorize({
    client_id: DISCORD_CLIENT_ID,
    response_type: "code",
    state: "",
    prompt: "none",
    // identify: who it is; email: the account's email, as site sign-in with Discord has it (the
    // platform keeps it only when Discord verified it); rpc.activities.write: what their profile
    // says they are playing
    scope: ["identify", "email", "rpc.activities.write"],
    ...pkce
  });
  return { code, verifier };
}

// hands Discord the access token the platform got for us, which the SDK's other commands need
export async function discordAuthenticate(accessToken: string): Promise<void> {
  const client = await discordSdk();
  if (!client || !accessToken) return;
  try {
    const auth = await client.commands.authenticate({ access_token: accessToken });
    publish({ self: auth.user.id });
    // Discord's own language for the page's words, unless the player chose one (i18n hintLang;
    // the identify scope covers it); never in the way
    void client.commands
      .userSettingsGetLocale()
      .then((r) => {
        if (r && typeof r.locale === "string" && r.locale) hintLang(r.locale);
      })
      .catch(() => {});
  } catch (e) {
    console.info("discord: authenticate failed (" + String(e instanceof Error ? e.message : e) + ")");
  }
}

// ---- the frame ----

// a link out of Discord: its own confirmation, then the system browser
export function openOutside(url: string) {
  void discordSdk().then((client) => {
    if (client) void client.commands.openExternalLink({ url }).catch(() => {});
    else window.open(url, "_blank", "noopener");
  });
}

// ---- what Discord tells the page while it runs (useDiscord) ----

export interface DiscordParticipant {
  id: string;
  name: string; // the server nickname, else the display name, else the username
}

export interface DiscordState {
  self: string | null; // the player's Discord user id, once authenticated
  participants: DiscordParticipant[]; // everyone in this launch, the player included
  layout: "focused" | "pip" | "grid"; // pip / grid: Discord shows the Activity small, no steering
  thermal: number; // 0 nominal, 1 fair, 2 serious, 3 critical (mobile, Android 10+, iOS)
}

let state: DiscordState = { self: null, participants: [], layout: "focused", thermal: 0 };
const listeners = new Set<() => void>();

function publish(next: Partial<DiscordState>) {
  state = { ...state, ...next };
  for (const listener of listeners) listener();
}

export function discordState(): DiscordState {
  return state;
}

export function subscribeDiscord(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

interface RawParticipant {
  id: string;
  username: string;
  global_name?: string | null;
  nickname?: string;
}

function participantsFrom(list: RawParticipant[] | undefined): DiscordParticipant[] {
  return (list ?? []).map((p) => ({ id: p.id, name: p.nickname || p.global_name || p.username }));
}

let watching = false;

// after sign-in (the commands below need authenticate()): who is here, the layout, the heat;
// each failure is a console note, never a reason the page stops
export async function watchDiscord(): Promise<void> {
  const client = await discordSdk();
  if (!client || watching) return;
  watching = true;
  const note = (what: string) => (e: unknown) => console.info("discord: " + what + " (" + String(e instanceof Error ? e.message : e) + ")");

  // renamed in Discord's client; an older client knows only the old name
  try {
    publish({ participants: participantsFrom((await client.commands.getActivityInstanceConnectedParticipants()).participants) });
  } catch {
    await client.commands
      .getInstanceConnectedParticipants()
      .then((r) => publish({ participants: participantsFrom(r.participants) }))
      .catch(note("no participants"));
  }
  await client
    .subscribe("ACTIVITY_INSTANCE_PARTICIPANTS_UPDATE", (d) => publish({ participants: participantsFrom(d.participants) }))
    .catch(note("no participant updates"));

  await client
    .subscribe("ACTIVITY_LAYOUT_MODE_UPDATE", (d) => publish({ layout: d.layout_mode === 1 ? "pip" : d.layout_mode === 2 ? "grid" : "focused" }))
    .catch(note("no layout updates"));

  if (DISCORD_PLATFORM === "mobile") {
    await client.subscribe("THERMAL_STATE_UPDATE", (d) => publish({ thermal: Math.max(0, Number(d.thermal_state) || 0) })).catch(note("no thermal state"));
    await client.commands
      .getPlatformBehaviors()
      .then((b) => console.info("discord: iOS keyboard resizes the view: " + String(b.iosKeyboardResizesView ?? "unknown")))
      .catch(() => {});
  }
}

// ---- rich presence, invites ----

// what the player's Discord profile says they are doing (needs rpc.activities.write)
export function discordPresence(details: string, stateText: string | null, since: number | null) {
  void discordSdk().then((client) => {
    if (!client) return;
    void client.commands
      .setActivity({
        activity: {
          type: 0,
          details,
          ...(stateText ? { state: stateText } : {}),
          ...(since ? { timestamps: { start: since } } : {})
        }
      })
      .catch((e: unknown) => console.info("discord: no presence (" + String(e instanceof Error ? e.message : e) + ")"));
  });
}

// a share link's custom_id: the room the person who shared it was in ("room:<serverId>")
export const DISCORD_SHARED_ROOM: string | null = (() => {
  const custom = DISCORD ? query.get("custom_id") ?? "" : "";
  const match = /^room:([a-z0-9_-]{1,64})$/i.exec(custom);
  return match ? match[1] : null;
})();

// invite to this launch (Discord's own dialog: the friends who accept land in the same room);
// where Discord does not offer it (a DM, no permission), a link that opens the Activity in the
// room the player is in
export async function discordInvite(serverId: string | null): Promise<boolean> {
  const client = await discordSdk();
  if (!client) return false;
  try {
    await client.commands.openInviteDialog();
    return true;
  } catch {
    try {
      const shared = await client.commands.shareLink({
        message: t("home.discord.shareMessage"),
        ...(serverId ? { custom_id: "room:" + serverId } : {})
      });
      return shared.success;
    } catch (e) {
      console.info("discord: no invite (" + String(e instanceof Error ? e.message : e) + ")");
      return false;
    }
  }
}

// the store for rendering
export function useDiscord(): DiscordState {
  return useSyncExternalStore(subscribeDiscord, discordState);
}
