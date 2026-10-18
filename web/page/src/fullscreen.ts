// Full screen on a phone or tablet.
// - Where the browser lets a page go full screen (Android, an iPad: the Fullscreen API), pressing
//   Play does: fullscreenOnPlay(), called from the press itself (it needs a user gesture; a Play
//   without one, the /play link's, is refused quietly).
// - An iPhone's Safari keeps the Fullscreen API to video. There the page gives itself room to scroll
//   under its fixed layers (html.is-scroll-hide, page.css) and tries a scroll on load, on a turn of
//   the phone and as a game starts (nudgeScroll): Safari folds its bars away when the page scrolls,
//   and a swipe on the arena now can do it in any case; they come back when it scrolls back up.
//   The home screen hint (components/HomeScreenHint.tsx) offers the real way: Add to Home Screen
//   (public/manifest.json), which opens with no browser bars at all.
import { touchPrimary } from "./compact";
import { DISCORD, EMBED } from "./links";

// launched from the home screen (no browser bars)
export function standalone(): boolean {
  return (
    (window.matchMedia?.("(display-mode: fullscreen), (display-mode: standalone)").matches ?? false) ||
    (navigator as Navigator & { standalone?: boolean }).standalone === true
  );
}

// an iPhone's (or iPod's) own Safari: not Chrome, Firefox, Edge, Opera or Google on iOS, nor an
// app's web view (which has no Version/ ... Safari/ in its user agent)
export function iphoneSafari(): boolean {
  const ua = navigator.userAgent;
  return /iPhone|iPod/.test(ua) && /Version\//.test(ua) && /Safari\//.test(ua) && !/CriOS|FxiOS|EdgiOS|OPiOS|GSA\//.test(ua);
}

export function fullscreenOnPlay() {
  // Discord owns its Activity's frame
  if (DISCORD || !touchPrimary() || standalone()) return;
  const doc = document as Document & { webkitFullscreenElement?: Element | null };
  if (document.fullscreenElement || doc.webkitFullscreenElement) return;
  const root = document.documentElement as HTMLElement & { webkitRequestFullscreen?: () => void };
  try {
    if (typeof root.requestFullscreen === "function" && document.fullscreenEnabled) {
      void root.requestFullscreen({ navigationUI: "hide" }).catch(() => {});
    } else {
      root.webkitRequestFullscreen?.();
    }
  } catch {
    // not offered here (an iPhone): the scroll and the home screen do what they can
  }
}

let scrollHide = false;

// iPhone Safari outside the home screen and the embed: room to scroll, and a first try
export function startScrollHide() {
  if (scrollHide || EMBED || standalone() || !iphoneSafari()) return;
  scrollHide = true;
  document.documentElement.classList.add("is-scroll-hide");
  nudgeScroll();
  window.addEventListener("orientationchange", nudgeScroll);
  window.addEventListener("pageshow", nudgeScroll);
}

// scroll a pixel down, after the layout settled, unless the page is scrolled already
export function nudgeScroll() {
  if (!scrollHide) return;
  window.setTimeout(() => {
    if (window.scrollY < 1) window.scrollTo(0, 1);
  }, 350);
}

// ---- Esc in full screen: the game's menu, not out of full screen ----------
// Chromium's Keyboard Lock hands Escape to the page while it is full screen (the browser says
// "press and hold Esc to exit"; a held Esc still leaves), so the game's INGAME_MENU key works
// there. Firefox and Safari keep Esc for themselves: it leaves full screen, and the page opens the
// menu as it goes (escapeLeft). The page's own ways out (leaveFullscreen) don't open it.

type KeyboardLock = { lock?(codes?: string[]): Promise<void>; unlock?(): void };
const keyboardLock = (): KeyboardLock | undefined => (navigator as Navigator & { keyboard?: KeyboardLock }).keyboard;

let ownExitAt = -Infinity;

// the page leaves full screen on purpose (a switch, a button, the game's fullscreen key)
export function leaveFullscreen() {
  ownExitAt = performance.now();
  const doc = document as Document & { webkitExitFullscreen?: () => void };
  try {
    if (document.exitFullscreen) void document.exitFullscreen().catch(() => {});
    else doc.webkitExitFullscreen?.();
  } catch {
    // not full screen after all
  }
}

// the whole page goes fullscreen (the game's canvas and the page's layers over it): the game's
// TOGGLE_FULLSCREEN key, its event in a game (useClient), the page's own handler outside one
// (App). One press, one toggle: a second call within 300 ms is the same press
let toggledAt = -Infinity;
export function toggleFullscreen() {
  if (performance.now() - toggledAt < 300) return;
  toggledAt = performance.now();
  try {
    if (document.fullscreenElement) leaveFullscreen();
    else void document.documentElement.requestFullscreen({ navigationUI: "hide" }).catch((e: Error) => console.info("fullscreen refused: " + e.message));
  } catch (e) {
    console.info("fullscreen unavailable: " + String(e));
  }
}

// while a game runs: Esc locked to the page in full screen; onEsc for a locked Esc the game's
// keys would have had (not one inside the page's own keys: the menu, a sheet, a field), since the
// client never takes Esc in full screen; onLeft when the browser took Esc to leave full screen
// anyway. Returns the cleanup.
export function escapeInFullscreen(onLeft: () => void, onEsc: () => void): () => void {
  const kb = keyboardLock();
  let locked = false;
  let wasFull = false;
  const update = () => {
    const doc = document as Document & { webkitFullscreenElement?: Element | null };
    const full = !!(document.fullscreenElement || doc.webkitFullscreenElement);
    const left = wasFull && !full;
    wasFull = full;
    if (full) {
      if (kb?.lock && !locked) {
        kb.lock(["Escape"]).then(
          () => (locked = true),
          () => (locked = false) // a frame (the X embed, Discord) or a browser that refuses
        );
      }
      return;
    }
    const wasLocked = locked;
    locked = false;
    // out of full screen: by the page's own way, a held Esc (locked), or the browser's Esc
    if (left && !wasLocked && performance.now() - ownExitAt > 1000) onLeft();
  };
  const esc = (e: KeyboardEvent) => {
    if (!locked || e.code !== "Escape" || e.repeat || !document.fullscreenElement) return;
    const t = e.target instanceof Element ? e.target : null;
    if (t && (t.closest('[data-aa-keys="page"]') || t.matches("input, textarea, [contenteditable]"))) return;
    onEsc();
  };
  update();
  window.addEventListener("keydown", esc);
  document.addEventListener("fullscreenchange", update);
  document.addEventListener("webkitfullscreenchange", update);
  return () => {
    window.removeEventListener("keydown", esc);
    document.removeEventListener("fullscreenchange", update);
    document.removeEventListener("webkitfullscreenchange", update);
    if (locked) kb?.unlock?.();
    locked = false;
  };
}
