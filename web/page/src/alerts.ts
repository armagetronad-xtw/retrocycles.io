// An opponent arrived at the host's 1v1 link: a short arcade
// chirp, and while the tab is in the background its title alternates "OPPONENT READY!" with the
// page's own until the host comes back. No permission is asked for either: the sound's audio
// context is made on the press that made the link (a user gesture), and a title is the page's.
//
// The page keeps its title against the engine (main.tsx keepTitle): it restores whatever
// `wantedTitle()` says, so the flasher changes that, not document.title alone.
import { t } from "./i18n";

let wanted = typeof document === "undefined" ? "Retrocycles" : document.title;
// the page's own name, before any count is put in front of it
const plain = wanted;
let unreadBadge = 0;

// the title the page keeps (main.tsx restores it whenever something else writes one)
export function wantedTitle(): string {
  return wanted;
}

function show(title: string) {
  wanted = title;
  if (document.title !== title) document.title = title;
}

// ---- the chirp ---------------------------------------------------------------------------

let audio: AudioContext | null = null;

// make (or wake) the audio context on a press, so the chirp may play later without one
export function primeAlerts() {
  try {
    const Ctor = window.AudioContext ?? (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
    if (!Ctor) return;
    audio ??= new Ctor();
    if (audio.state === "suspended") void audio.resume().catch(() => {});
  } catch {
    audio = null;
  }
}

// three square-wave steps up and a held top note: a cabinet's "player ready", about half a second
export function arrivalChirp() {
  const ctx = audio;
  if (!ctx) return;
  if (ctx.state === "suspended") void ctx.resume().catch(() => {});
  const t0 = ctx.currentTime + 0.02;
  const out = ctx.createGain();
  out.gain.value = 0.12;
  out.connect(ctx.destination);
  const notes: Array<[number, number, number]> = [
    // [frequency Hz, start s, length s]
    [523.25, 0, 0.07], // C5
    [783.99, 0.075, 0.07], // G5
    [1046.5, 0.15, 0.07], // C6
    [1567.98, 0.225, 0.26] // G6, held
  ];
  for (const [freq, start, length] of notes) {
    const osc = ctx.createOscillator();
    const env = ctx.createGain();
    osc.type = "square";
    osc.frequency.value = freq;
    const a = t0 + start;
    env.gain.setValueAtTime(0, a);
    env.gain.linearRampToValueAtTime(1, a + 0.005);
    env.gain.setValueAtTime(1, a + length - 0.03);
    env.gain.exponentialRampToValueAtTime(0.001, a + length);
    osc.connect(env);
    env.connect(out);
    osc.start(a);
    osc.stop(a + length + 0.01);
  }
}

// ---- the flashing title --------------------------------------------------------------------

let flashing: { timer: number; base: string; text: string; onVisible: () => void } | null = null;

// alternate the title with `text` about once a second while the tab is hidden; nothing when it
// is in front (the screen says it already). It stops on its own when the host comes back.
// Background tabs throttle timers (to 1 s, later to a minute): the title is worked out from the
// clock at each tick, so a late tick never leaves it on the wrong one for long, and coming back
// always restores it.
export function flashTitle(text: string) {
  if (typeof document === "undefined" || !document.hidden) return;
  stopFlash();
  const base = wanted;
  const tick = () => show(Math.floor(Date.now() / 1000) % 2 === 0 ? text : (flashing?.base ?? base));
  const onVisible = () => {
    if (!document.hidden) stopFlash();
  };
  document.addEventListener("visibilitychange", onVisible);
  flashing = { timer: window.setInterval(tick, 1000), base, text, onVisible };
  show(text);
}

// the page's own title back (the host returned, or what was announced is over)
export function stopFlash() {
  if (!flashing) return;
  window.clearInterval(flashing.timer);
  document.removeEventListener("visibilitychange", flashing.onVisible);
  const base = flashing.base;
  flashing = null;
  show(base);
}

// unread direct messages in front of the title ("(2) Retrocycles", the chat dock), so a page in a
// background tab says it has something; the flasher alternates with the counted title
export function setUnreadBadge(n: number) {
  if (typeof document === "undefined" || n === unreadBadge) return;
  unreadBadge = n;
  const title = n > 0 ? "(" + n + ") " + plain : plain;
  if (flashing) flashing.base = title;
  else show(title);
}

// both at once: what an opponent's arrival does
export function alertArrival() {
  arrivalChirp();
  flashTitle(t("connect.duel.titleReady"));
}
