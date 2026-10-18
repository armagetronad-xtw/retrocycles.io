// Direct Connect's addresses: what a player may type, how it is kept,
// and which host a signalling URL reaches. Pure, and without imports: the tests load it under
// node (tests/directAddress.test.ts); platform/direct.ts and the page use it.
//
// Typed, an address is one of:
//   host             a name or an IPv4 address; the game port is the default, 4534
//   host:port        the game port players already know from the desktop game
//   [ipv6]:port      an IPv6 address in brackets (bare, without a port, it may go without them)
//   wss://...        a server's own signalling URL, with its own certificate: straight to it
// A host:port goes through the platform's signalling gateway to the server's signalling port
// (the game port + 1 unless the player names another), and the game itself goes straight
// between the browser and the server over WebRTC.

export const DEFAULT_GAME_PORT = 4534;

export type DirectAddress =
  // key: how the page keeps it (My servers, the warning seen): host:port, IPv6 in brackets
  | { kind: "ip"; host: string; port: number; key: string }
  // a server's own wss:// URL; host: its host, for the blocklist and the move rule
  | { kind: "wss"; url: string; host: string; port: number | null; key: string };

export type AddressError = "empty" | "invalid" | "port";

const NAME = /^(?=.{1,253}$)([a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)(\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*$/;

function isIpv6(host: string): boolean {
  return host.includes(":");
}

// the host as the platform normalizes it before hashing (its README "Direct Connect"): a name in
// lower case without a trailing dot, an IDN as punycode; an IPv4 address as a dotted quad, read as
// a resolver reads it (127.1, 0x7f.0.0.1, 2130706433: the WHATWG URL parser does the same); an
// IPv6 address in RFC 5952 text without brackets or zone, an IPv4-mapped one as its IPv4
// address. null when it is none of these
export function normalizeHost(raw: string): string | null {
  let host = raw.trim();
  if (host.startsWith("[") && host.endsWith("]")) host = host.slice(1, -1);
  if (!host) return null;
  if (isIpv6(host)) {
    host = host.replace(/%.*$/, "").toLowerCase(); // a zone id is dropped
    if (!/^[0-9a-f:.]+$/.test(host)) return null;
    let v6: string;
    try {
      v6 = new URL("http://[" + host + "]/").hostname.slice(1, -1);
    } catch {
      return null;
    }
    // IPv4-mapped (::ffff:a.b.c.d, which the parser writes as ::ffff:hhhh:hhhh): the IPv4 address
    const mapped = v6.match(/^::ffff:([0-9a-f]{1,4}):([0-9a-f]{1,4})$/);
    if (mapped) {
      const hi = parseInt(mapped[1], 16);
      const lo = parseInt(mapped[2], 16);
      return [hi >> 8, hi & 255, lo >> 8, lo & 255].join(".");
    }
    return v6;
  }
  let parsed: string;
  try {
    // lower case, punycode for an IDN, an IPv4 address in any form as a dotted quad
    parsed = new URL("http://" + host + "/").hostname;
  } catch {
    return null;
  }
  if (parsed.endsWith(".")) parsed = parsed.slice(0, -1);
  if (!parsed || parsed.includes("[")) return null;
  if (/^\d+\.\d+\.\d+\.\d+$/.test(parsed)) return parsed;
  return NAME.test(parsed) ? parsed : null;
}

// what the blocklist is checked for: the host itself and every wildcard above it (a name:
// *.b.example.com, *.example.com, *.com for a.b.example.com; an IPv4 address: 1.2.3.*, 1.2.*,
// 1.* for 1.2.3.4); an IPv6 address only as itself. The platform's DirectBlocklist.Candidates
export function blockCandidates(host: string): string[] {
  const out = [host];
  if (isIpv6(host)) return out;
  const parts = host.split(".");
  if (/^\d+\.\d+\.\d+\.\d+$/.test(host)) {
    for (let n = 3; n >= 1; n--) out.push(parts.slice(0, n).join(".") + ".*");
    return out;
  }
  for (let i = 1; i < parts.length; i++) out.push("*." + parts.slice(i).join("."));
  return out;
}

// host:port as people write it, IPv6 in brackets
export function hostPort(host: string, port: number): string {
  return (isIpv6(host) ? "[" + host + "]" : host) + ":" + port;
}

function portOf(text: string): number | null {
  if (!/^\d{1,5}$/.test(text)) return null;
  const n = Number(text);
  return n >= 1 && n <= 65535 ? n : null;
}

export function parseAddress(input: string): DirectAddress | { error: AddressError } {
  const text = input.trim();
  if (!text) return { error: "empty" };
  if (/^wss:\/\//i.test(text)) {
    try {
      const url = new URL(text);
      const host = normalizeHost(url.hostname);
      if (!host || url.username || url.password) return { error: "invalid" };
      return { kind: "wss", url: url.href, host, port: url.port ? Number(url.port) : null, key: url.href };
    } catch {
      return { error: "invalid" };
    }
  }
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(text)) return { error: "invalid" }; // http://, ws://: not an address to type
  let host: string;
  let port = DEFAULT_GAME_PORT;
  const bracket = text.match(/^\[([^\]]+)\](?::(.*))?$/);
  if (bracket) {
    host = bracket[1];
    if (bracket[2] !== undefined) {
      const p = portOf(bracket[2]);
      if (p === null) return { error: "port" };
      port = p;
    }
  } else if ((text.match(/:/g) ?? []).length > 1) {
    host = text; // a bare IPv6 address: no port
  } else {
    const at = text.lastIndexOf(":");
    host = at >= 0 ? text.slice(0, at) : text;
    if (at >= 0) {
      const p = portOf(text.slice(at + 1));
      if (p === null) return { error: "port" };
      port = p;
    }
  }
  const normal = normalizeHost(host);
  // a name needs two labels (the platform refuses localhost and container names as targets)
  if (!normal || (!isIpv6(normal) && !normal.includes("."))) return { error: "invalid" };
  return { kind: "ip", host: normal, port, key: hostPort(normal, port) };
}

// the signalling port: the one the player named under More, else the game port + 1 (the
// server's WEBRTC_SIGNAL_PORT 0)
export function signalPortFor(port: number, chosen: number | null): number {
  return chosen ?? Math.min(65535, port + 1);
}

// the gateway's address for an IP-only server (platform contract: GET /v1/direct/signal?to=)
export function gatewayUrl(apiUrl: string, host: string, signalPort: number): string {
  const base = apiUrl.replace(/^http/i, "ws").replace(/\/+$/, "");
  return base + "/v1/direct/signal?to=" + encodeURIComponent(hostPort(host, signalPort));
}

// whether a signalling URL is the gateway's (any host): /v1/direct/signal with a `to`
export function gatewayTarget(url: string, base: string): { host: string; port: number } | null {
  try {
    const u = new URL(url, base);
    if (!/\/v1\/direct\/signal$/.test(u.pathname)) return null;
    const to = u.searchParams.get("to");
    if (!to) return null;
    const parsed = parseAddress(to);
    return "error" in parsed || parsed.kind !== "ip" ? null : { host: parsed.host, port: parsed.port };
  } catch {
    return null;
  }
}

// the host a signalling URL reaches: the gateway's `to` host for a gateway URL, else the URL's
// own host (a path resolves against the page's address); null when it cannot be read
export function signalHost(url: string, base: string): string | null {
  const through = gatewayTarget(url, base);
  if (through) return through.host;
  try {
    return normalizeHost(new URL(url, base).hostname);
  } catch {
    return null;
  }
}

// where a signalling URL leads, as a player reads it: host:port (the gateway's target), else the
// URL's host and port, else the URL as it is
export function addressText(url: string, base: string): string {
  const through = gatewayTarget(url, base);
  if (through) return hostPort(through.host, through.port);
  try {
    return new URL(url, base).host || url;
  } catch {
    return url;
  }
}

//: a server may move a player to another port on its own host only
export function sameHost(from: string, to: string, base: string): boolean {
  const a = signalHost(from, base);
  const b = signalHost(to, base);
  return a !== null && a === b;
}
