// Cross-origin isolation and the tab X opens. The site is served isolated (COOP +
// COEP: the 5 us game clock), except the embed, Discord, and the embed's way out (/?out), which
// X's sandboxed frame opens in a tab that stays sandboxed for its life; Chrome refuses an isolated
// page there (ERR_BLOCKED_BY_RESPONSE), an address typed into that tab too. The platform's Caddy
// serves a request carrying the cookie rc_out=1 unisolated, and sets it on /?out.
//
// A cookie is the browser's, not the tab's, so the page keeps it right per tab: the tab that came
// through X (marked in sessionStorage, which is the tab's) sets it whenever it loads or takes the
// focus, so it is there before an address is typed into it; any other tab clears it as it loads or
// takes the focus, and a tab that was served unisolated because of it loads once more, isolated,
// before anything else starts (once a tab: a browser that never isolates does not loop).
import { EMBED } from "./links";

const COOKIE = "rc_out";
const MARK = "rc.outTab";
const RELOADED = "rc.isolationReload";

function setCookie(on: boolean) {
  document.cookie = COOKIE + (on ? "=1" : "=; Max-Age=0") + "; Path=/; Secure; SameSite=Lax";
}

function hasCookie(): boolean {
  return document.cookie.split(/;\s*/).includes(COOKIE + "=1");
}

function session(name: string, value?: string): string | null {
  try {
    if (value !== undefined) sessionStorage.setItem(name, value);
    return sessionStorage.getItem(name);
  } catch {
    return null;
  }
}

// true when the page is reloading itself (main.tsx then starts nothing)
export function keepIsolation(): boolean {
  if (EMBED) return false;
  if (new URLSearchParams(location.search).has("out")) session(MARK, "1");
  const outTab = session(MARK) === "1";
  const keep = () => setCookie(outTab);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") keep();
  });
  window.addEventListener("focus", keep);
  if (outTab) {
    setCookie(true);
    return false;
  }
  const had = hasCookie();
  setCookie(false);
  if (crossOriginIsolated) {
    try {
      sessionStorage.removeItem(RELOADED);
    } catch {
      // no storage: no guard to clear
    }
  }
  if (had && !crossOriginIsolated && session(RELOADED) === null && session(RELOADED, "1") === "1") {
    location.reload();
    return true;
  }
  return false;
}
