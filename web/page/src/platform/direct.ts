// Direct Connect: a server reached by its address, not by the board.
// - The probe (GET /v1/direct/probe): what the server at host:port says (name, players, version)
//   and whether its browser port answers, before anyone joins.
// - The blocklist (GET /v1/direct/blocklist): hashed hosts the page refuses, read every 5 minutes.
// - My servers and the warning seen, per address, in this browser (localStorage, wrapped).
// - Reports about a player on a server that is not ours (POST /v1/direct/reports), with this
//   page's own chat lines.
// All of it is behind the platform's flag: a 404 or no platform leaves the page quiet. A server's
// own wss:// URL still works without the platform; host:port needs the platform's gateway.
import type { ServerEntry } from "../aa/types";
import { createDirectReport, getDirectBlocklist, getDirectProbe, PlatformError, type DirectProbe } from "./client";
import { platformSession, platformState, sessionUser } from "./session";
import { blockCandidates, gatewayUrl, normalizeHost, parseAddress, signalPortFor, type DirectAddress } from "../directAddress";
import { DISCORD } from "../links";

// Direct Connect is hidden inside the Discord Activity: Discord allows no WebRTC, and an IP-only
// server takes nothing else
export const DIRECT_OFFERED = !DISCORD;

export type Probe =
  | { state: "probing" }
  // the platform has no probe (no platform, the flag off, unreachable): no preview
  | { state: "none" }
  | { state: "done"; probe: DirectProbe };

export interface DirectInfo {
  key: string; // the address as kept (host:port, or the wss:// URL)
  host: string; // normalized
  gateway: boolean; // through the platform's signalling gateway (WebRTC only, never the WebSocket transport)
  signalPort: number | null; // the one used through the gateway
  browser: DirectProbe["browser"] | null; // the probe's word on the browser port, when it had one
}

// the platform's flag (GET /v1/platform/config features.directConnect): off or absent, no probe,
// no blocklist, no gateway, no reports; a server's own wss:// address still works
export function directConnectOn(): boolean {
  const state = platformState();
  return state.mode === "platform" && state.config.features.directConnect === true;
}

// the API, only while the flag is on
async function apiUrl(): Promise<string | null> {
  const state = await platformSession();
  return state.mode === "platform" && state.config.features.directConnect === true ? state.apiUrl : null;
}

// ---- the probe ------------------------------------------------------------------------------

export async function probe(addr: DirectAddress, signal?: AbortSignal): Promise<Probe> {
  if (addr.kind !== "ip") return { state: "none" };

  const api = await apiUrl();
  if (!api) return { state: "none" };
  try {
    const answer = await getDirectProbe(api, addr.host, addr.port, signal);
    if (!answer || typeof answer.reachable !== "boolean") return { state: "none" };
    return { state: "done", probe: answer };
  } catch (e) {
    if (!(e instanceof PlatformError) || e.status !== 404) console.info("direct: no probe (" + String(e instanceof Error ? e.message : e) + ")");
    return { state: "none" };
  }
}

// ---- the blocklist ----------------------------------------------------------------------------

const BLOCKLIST_MS = 5 * 60 * 1000;
let blocklist: { at: number; hashes: Set<string> } | null = null;
let reading: Promise<Set<string>> | null = null;

async function hashes(): Promise<Set<string>> {
  if (blocklist && Date.now() - blocklist.at < BLOCKLIST_MS) return blocklist.hashes;
  if (reading) return reading;
  reading = (async () => {
    let set = new Set<string>();
    const api = await apiUrl();
    if (api) {
      try {
        const answer = await getDirectBlocklist(api);
        if (answer && Array.isArray(answer.hashes)) set = new Set(answer.hashes.filter((h): h is string => typeof h === "string").map((h) => h.toLowerCase()));
      } catch (e) {
        // the last list read stays in force until a new one comes in
        if (blocklist) set = blocklist.hashes;
        if (!(e instanceof PlatformError) || e.status !== 404) console.info("direct: no blocklist (" + String(e instanceof Error ? e.message : e) + ")");
      }
    }
    blocklist = { at: Date.now(), hashes: set };
    reading = null;
    return set;
  })();
  return reading;
}

async function sha256(text: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

// whether any of these hosts (the typed one, the address it resolved to) is on the blocklist:
// the host itself or a wildcard above it (*.example.com, 1.2.3.*; directAddress.ts blockCandidates)
export async function isBlocked(...hosts: Array<string | null | undefined>): Promise<boolean> {
  const list = await hashes();
  if (!list.size || !crypto?.subtle) return false;
  for (const raw of hosts) {
    const host = raw ? normalizeHost(raw) : null;
    if (!host) continue;
    for (const candidate of blockCandidates(host)) if (list.has(await sha256(candidate))) return true;
  }
  return false;
}

// ---- joining --------------------------------------------------------------------------------

// the entry to connect to: a server's own wss:// as it is; host:port through the gateway (null
// without the platform: there is no other way to an IP-only server from a page)
export function directEntry(addr: DirectAddress, chosenSignal: number | null, answer: DirectProbe | null): ServerEntry | null {
  const name = answer?.name?.trim() || (addr.kind === "ip" ? addr.key : addr.host);
  if (addr.kind === "wss")
    return { name, url: addr.url, description: "", direct: { key: addr.key, host: addr.host, gateway: false, signalPort: null, browser: null } };
  const platform = platformState();
  if (!directConnectOn()) return null;
  const signalPort = signalPortFor(addr.port, chosenSignal ?? answer?.signalPort ?? null);
  const api = platform.mode === "platform" ? platform.apiUrl : location.origin;
  return {
    name,
    url: gatewayUrl(api, addr.host, signalPort),
    description: "",
    direct: { key: addr.key, host: addr.host, gateway: true, signalPort, browser: chosenSignal !== null && chosenSignal !== answer?.signalPort ? "unknown" : answer?.browser ?? null }
  };
}

// a server's move to another port of its own host, when it came through the gateway: a plain
// ws:// or host:port target goes through the gateway too (the page reaches nothing else)
export function throughGateway(target: string, host: string): string | null {
  const platform = platformState();
  if (platform.mode !== "platform" || !directConnectOn()) return null;
  let port: number | null = null;
  try {
    const u = new URL(target);
    if (u.protocol === "ws:" && u.port) port = Number(u.port);
  } catch {
    const parsed = parseAddress(target);
    if (!("error" in parsed) && parsed.kind === "ip") port = parsed.port;
  }
  return port ? gatewayUrl(platform.apiUrl, host, port) : null;
}

// ---- My servers and the warning, in this browser -------------------------------------------

export interface SavedServer {
  address: string; // as parseAddress reads it again: host:port or the wss:// URL
  signalPort: number | null; // under More, when the player named one
  name: string | null; // the name the server last gave, for the row before a probe answers
}

const SAVED_KEY = "rc.directServers";
const WARNED_KEY = "rc.directWarned";


function readList<T>(key: string, ok: (v: unknown) => v is T): T[] {
  try {
    const raw = JSON.parse(localStorage.getItem(key) || "[]");
    return Array.isArray(raw) ? raw.filter(ok) : [];
  } catch {
    return [];
  }
}

function writeList(key: string, list: unknown[]) {
  try {
    localStorage.setItem(key, JSON.stringify(list));
  } catch {
    // storage refused (a private window): kept for the visit
  }
}

const isSaved = (v: unknown): v is SavedServer => {
  const s = v as SavedServer;
  return !!s && typeof s.address === "string" && !("error" in parseAddress(s.address));
};

export function loadSaved(): SavedServer[] {
  return readList(SAVED_KEY, isSaved).map((s) => ({
    address: s.address,
    signalPort: typeof s.signalPort === "number" ? s.signalPort : null,
    name: typeof s.name === "string" ? s.name.slice(0, 200) : null
  }));
}

export function storeSaved(list: SavedServer[]) {
  writeList(SAVED_KEY, list.slice(0, 50));
}

export function warnedFor(key: string): boolean {

  return readList(WARNED_KEY, (v): v is string => typeof v === "string").includes(key);
}

export function noteWarned(key: string) {
  const list = readList(WARNED_KEY, (v): v is string => typeof v === "string").filter((k) => k !== key);
  writeList(WARNED_KEY, [key, ...list].slice(0, 200));
}

// ?connect=host:port: fills the address and shows the warning, never joins. Read once and
// taken off the address bar
export function readConnectLink(): string | null {
  const query = new URLSearchParams(location.search);
  const value = query.get("connect");
  if (value === null) return null;
  query.delete("connect");
  const rest = query.toString();
  history.replaceState(history.state, "", location.pathname + (rest ? "?" + rest : "") + location.hash);
  return DIRECT_OFFERED && value.length <= 300 ? value : null;
}

// ---- reports ------------------------------------------------------------------------------

export interface DirectLine {
  name: string;
  text: string;
  at: number; // Unix seconds
}

const REPORT_LINES = 50;
const REPORT_WINDOW_S = 10 * 60;

// the lines a report carries: this page's own, at most 50 from the last 10 minutes
export function reportLines(lines: DirectLine[], now = Date.now() / 1000): DirectLine[] {
  return lines.filter((l) => now - l.at <= REPORT_WINDOW_S).slice(-REPORT_LINES);
}

export type DirectReportResult = "sent" | "limited" | "unavailable" | "failed";

export async function sendDirectReport(address: string, targetName: string, reason: string, note: string, lines: DirectLine[]): Promise<DirectReportResult> {

  const api = await apiUrl();
  if (!api) return "unavailable";
  // a session is needed: a guest's is made when there is none
  if (!(await sessionUser())) return "unavailable";
  try {
    await createDirectReport(api, { address, targetName, reason, ...(note ? { note } : {}), chat: reportLines(lines) });
    return "sent";
  } catch (e) {
    if (e instanceof PlatformError && e.status === 429) return "limited";
    if (e instanceof PlatformError && e.status === 404) return "unavailable";
    return "failed";
  }
}

// whether reports about players on servers that are not ours can be offered: a platform to take
// them (the endpoint itself says whether its flag is on)
export function directReportsOffered(): boolean {
  return directConnectOn();
}

