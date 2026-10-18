// External sign-in in a popup (platform-contract/README.md, "External sign-in (popup)"),
// so the running game is never reloaded. The popup goes platform -> provider ->
// platform, whose result page reports {type: "rc-auth", status, provider, needsHandle,
// error} one of two ways:
//   - window.opener.postMessage(result, <page origin>) when the page still is the
//     popup's opener (a host without Cross-Origin-Opener-Policy, e.g. the dev server);
//   - on a cross-origin isolated page (COOP same-origin, the deploy), which loses the
//     opener as soon as the popup leaves its origin, by navigating to the relay page
//     public/auth-complete.html on this origin, which posts the result on
//     BroadcastChannel("rc-auth") and closes.
// The page listens on both. The message carries no credential (the session is the
// cookie the platform already set); it only says what to ask the platform next, so the
// page always confirms with /v1/me (and /v1/auth/external/pending for confirm_link).
import type { ExternalResult } from "./types";

const CHANNEL = "rc-auth";

function asResult(data: unknown): ExternalResult | null {
  if (!data || typeof data !== "object") return null;
  const m = data as Record<string, unknown>;
  if (m.type !== "rc-auth") return null;
  if (m.status !== "signed_in" && m.status !== "linked" && m.status !== "confirm_link" && m.status !== "error") return null;
  return {
    type: "rc-auth",
    status: m.status,
    provider: typeof m.provider === "string" ? m.provider : undefined,
    needsHandle: m.needsHandle === true,
    error: typeof m.error === "string" ? m.error : undefined
  };
}

// listen for popup results; `platformOrigin` is the only origin a postMessage is taken from
export function onExternalResult(platformOrigin: string, handler: (result: ExternalResult) => void): () => void {
  const onMessage = (event: MessageEvent) => {
    if (event.origin !== platformOrigin) return;
    const result = asResult(event.data);
    if (result) handler(result);
  };
  window.addEventListener("message", onMessage);
  let channel: BroadcastChannel | null = null;
  try {
    channel = new BroadcastChannel(CHANNEL);
    channel.onmessage = (event: MessageEvent) => {
      const result = asResult(event.data);
      if (result) handler(result);
    };
  } catch {
    channel = null; // no BroadcastChannel: the focus fallback (Profile.tsx) still notices the session
  }
  return () => {
    window.removeEventListener("message", onMessage);
    channel?.close();
  };
}

// open the popup; must run inside the user's click / key press (popup blockers). The
// return value is not useful on an isolated page (the proxy reads as closed once the
// popup leaves this origin), so the page never polls it.
export function openExternalPopup(url: string): void {
  const width = 520;
  const height = 700;
  const left = Math.max(0, Math.round(window.screenX + (window.outerWidth - width) / 2));
  const top = Math.max(0, Math.round(window.screenY + (window.outerHeight - height) / 3));
  window.open(url, "rc-auth", "popup,width=" + width + ",height=" + height + ",left=" + left + ",top=" + top);
}
