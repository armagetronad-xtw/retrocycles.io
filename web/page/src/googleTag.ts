// Google Analytics (GA4), beside the first-party events (platform/analytics.ts): the platform
// names the measurement id and whether this visitor must consent to Google's cookies first
// (GET /v1/platform/config googleAnalytics: the EEA, the UK, Switzerland, or a country it cannot
// tell; platform README "Google Analytics").
//
// - Consent not needed: the tag runs with analytics_storage granted (the _ga cookies), no banner.
// - Consent needed: analytics_storage denied (cookieless pings) until the visitor presses Accept
//   on the banner (components/CookieBanner.tsx); Decline keeps it denied. The choice is kept in
//   localStorage (rc.cookies: "granted" or "denied"; blocked storage asks again next time).
//   /?cookies shows the banner again anywhere, so a choice can be changed (the privacy page links it).
// - Always: the ad signals denied; nothing with Global Privacy Control, in community mode, or
//   inside Discord (its proxy lets the Activity reach its own origin only), or in an automated
//   browser (a test run). The X embed loads the tag but never shows the banner (a post's card
//   is no place for one): there it stays denied.
// - No inline script (the Content-Security-Policy has none): gtag() is defined here and gtag.js
//   is added as a script from www.googletagmanager.com, crossorigin so it passes the page's
//   cross-origin isolation (it answers with CORS and CORP headers).
// - What GA sees besides its page view: a few of the usage events, under GA's names (gaEvent,
//   called by analytics.ts track(): play_start, game_played, sign_up / login, share), each with
//   `surface` (browser, mobile, x-browser, x-mobile; a custom dimension in GA). Events made before
//   the platform answered wait for it; without a tag they are dropped.
// - The page's own campaign codes (?c=, the usage events' campaign) become GA's campaign when the
//   address has no utm_ parameters of its own: page_location carries them (campaignLocation).
//   page_location is always set, to the path and the campaign only: no other parameter reaches GA.
import { platformSession } from "./platform/session";
import { automated, deviceClass, type EventName, type Props } from "./platform/analytics";
import { DISCORD, EMBED } from "./links";

declare global {
  interface Window {
    dataLayer?: unknown[];
  }
}

export type CookieChoice = "granted" | "denied";

const KEY = "rc.cookies";

let started = false;
let measurementId: string | null = null;
// GA's events waiting for the platform's answer (null once it answered)
let waiting: [string, Record<string, string | number>][] | null = [];
let bannerUp = false;
const listeners = new Set<() => void>();

function readChoice(): CookieChoice | null {
  try {
    const v = localStorage.getItem(KEY);
    return v === "granted" || v === "denied" ? v : null;
  } catch {
    return null;
  }
}

function writeChoice(choice: CookieChoice) {
  try {
    localStorage.setItem(KEY, choice);
  } catch {
    // asked again next time
  }
}

// gtag.js reads the Arguments objects themselves from the data layer, not arrays
function gtag(..._args: unknown[]) {
  (window.dataLayer = window.dataLayer || []).push(arguments);
}

function setBanner(up: boolean) {
  if (bannerUp === up) return;
  bannerUp = up;
  for (const listener of listeners) listener();
}

export function cookieBannerUp(): boolean {
  return bannerUp;
}

export function subscribeCookieBanner(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

// Google's cookies on this host and the domains above it (gtag sets them on the widest one it can)
function clearGoogleCookies() {
  const names = document.cookie
    .split(";")
    .map((c) => c.split("=")[0].trim())
    .filter((n) => n === "_ga" || n.startsWith("_ga_") || n === "_gid");
  const labels = location.hostname.split(".");
  const domains = [""];
  for (let i = 0; i < labels.length - 1; i++) domains.push("; domain=." + labels.slice(i).join("."));
  for (const name of names) {
    for (const domain of domains) document.cookie = `${name}=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/${domain}`;
  }
}

// the stored answer to the banner (null: none yet); analytics.ts holds the usage events on it too
export function cookieChoice(): CookieChoice | null {
  return readChoice();
}

const choiceListeners = new Set<(choice: CookieChoice) => void>();

export function onCookieChoice(listener: (choice: CookieChoice) => void) {
  choiceListeners.add(listener);
}

// the banner's Accept / Decline
export function chooseCookies(choice: CookieChoice) {
  writeChoice(choice);
  setBanner(false);
  for (const listener of choiceListeners) listener(choice);
  if (!measurementId) return;
  gtag("consent", "update", { analytics_storage: choice });
  if (choice === "denied") clearGoogleCookies();
}

// once per page load, after the platform has answered
export function startGoogleTag() {
  if (started) return;
  started = true;
  if (DISCORD || automated() || (navigator as Navigator & { globalPrivacyControl?: boolean }).globalPrivacyControl === true) {
    waiting = null;
    return;
  }

  // /?cookies: ask again (taken off the address, so a copied link does not carry it)
  const params = new URLSearchParams(location.search);
  const askAgain = params.has("cookies");
  if (askAgain) {
    params.delete("cookies");
    const query = params.toString();
    history.replaceState(history.state, "", location.pathname + (query ? "?" + query : "") + location.hash);
  }
  // read now: the page takes its own parameters (?play, ?account) off the address as it starts
  const campaignUrl = campaignLocation();

  void platformSession().then((state) => {
    if (state.mode !== "platform") return;
    const ga = state.config.googleAnalytics;
    if (!ga || typeof ga.measurementId !== "string" || !/^G-[A-Z0-9]+$/.test(ga.measurementId)) return;
    measurementId = ga.measurementId;
    const required = ga.consentRequired !== false;
    const stored = readChoice();
    const granted = required ? stored === "granted" : stored !== "denied";

    gtag("consent", "default", {
      analytics_storage: granted ? "granted" : "denied",
      ad_storage: "denied",
      ad_user_data: "denied",
      ad_personalization: "denied"
    });
    gtag("js", new Date());
    // the config's parameters go with every event of the tag (page_view included)
    gtag("config", measurementId, { surface: surface(), page_location: campaignUrl });
    for (const [name, params] of waiting ?? []) gtag("event", name, params);
    waiting = null;

    const script = document.createElement("script");
    script.async = true;
    script.crossOrigin = "anonymous";
    script.src = "https://www.googletagmanager.com/gtag/js?id=" + encodeURIComponent(measurementId);
    document.head.appendChild(script);

    if (!EMBED && (askAgain || (required && stored === null))) setBanner(true);
  }).finally(() => {
    if (!measurementId) waiting = null;
  });
}

// ---- what GA is told -------------------------------------------------------------------------

function surface(): "browser" | "mobile" | "x-browser" | "x-mobile" {
  const mobile = deviceClass() !== "desktop";
  return EMBED ? (mobile ? "x-mobile" : "x-browser") : mobile ? "mobile" : "browser";
}

// the page's ?c= codes as GA's source / medium / campaign (links.ts fullPageLink, tools/social.ts)
function campaignOf(code: string): { source: string; medium: string } {
  if (code === "x-card") return { source: "x.com", medium: "social" };
  if (code.startsWith("embed")) return { source: "x.com", medium: "embed" };
  if (code.startsWith("discord")) return { source: "discord.com", medium: "activity" };
  if (code === "invite") return { source: "invite", medium: "referral" };
  return { source: code, medium: "link" };
}

// the address GA is told: the path and the campaign alone (the address's own utm_ parameters, else
// the ?c= code as utm_), never the rest, which can hold private codes (?duel=, ?join=, ?connect=)
function campaignLocation(): string {
  const params = new URLSearchParams(location.search);
  const url = new URL(location.origin + location.pathname);
  const utm = [...params].filter(([k]) => k.startsWith("utm_"));
  const code = (params.get("c") ?? "").trim().toLowerCase();
  if (utm.length > 0) {
    for (const [k, v] of utm) url.searchParams.append(k, v);
  } else if (/^[a-z0-9_-]{1,32}$/.test(code)) {
    const { source, medium } = campaignOf(code);
    url.searchParams.set("utm_source", source);
    url.searchParams.set("utm_medium", medium);
    url.searchParams.set("utm_campaign", code);
  }
  return url.href;
}

// a usage event (analytics.ts track()), as GA's event when GA has one for it
export function gaEvent(name: EventName, props?: Props) {
  if (waiting === null && !measurementId) return;
  let event: string | null = null;
  const params: Record<string, string | number> = {};
  switch (name) {
    case "play_pressed":
      event = "play_start";
      if (typeof props?.mode === "string") params.mode = props.mode;
      break;
    case "first_turn":
      event = "game_played";
      break;
    case "claim_done":
      event = props?.kind === "signin" ? "login" : "sign_up";
      if (typeof props?.method === "string") params.method = props.method;
      break;
    case "link_copied":
      event = "share";
      params.method = "link";
      if (typeof props?.what === "string") params.content_type = props.what;
      break;
  }
  if (!event) return;
  if (waiting) {
    if (waiting.length < 20) waiting.push([event, params]);
    return;
  }
  gtag("event", event, params);
}
