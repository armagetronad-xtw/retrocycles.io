// Tab is moved by the page itself. The wasm client listens for keys on the window
// (capture phase) and preventDefault()s everything that is not a browser shortcut
// (API.md "Setup", the reserved list), Tab included, as soon as the runtime is up.
// The default focus move is therefore lost, and the page re-implements it: the
// visible focusable elements in DOM order, wrapping at both ends. Text fields keep
// their keys (the client leaves them alone while one has the focus). Links count (the
// documents' line under the queue, the sign-up's consent line); App.tsx follows them on
// Enter, which the client takes as well.
const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

function visible(el: HTMLElement): boolean {
  if (el.hidden || el.closest("[hidden]")) return false;
  const style = getComputedStyle(el);
  if (style.visibility === "hidden" || style.display === "none") return false;
  const rect = el.getBoundingClientRect();
  return rect.width > 0 && rect.height > 0;
}

export function moveFocus(backwards: boolean, within?: HTMLElement | null) {
  const root = within || document;
  const all = Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE)).filter(
    (el) => el.id !== "canvas" && visible(el)
  );
  if (!all.length) return;
  const at = all.indexOf(document.activeElement as HTMLElement);
  let next: number;
  if (at < 0) next = backwards ? all.length - 1 : 0;
  else next = (at + (backwards ? -1 : 1) + all.length) % all.length;
  const el = all[next];
  el.focus({ preventScroll: true });
  // the client's key policy also takes the browser's own scroll-to-focus: inside a body
  // that scrolls (the Profile's), bring the row into view (a 1 px checkbox's whole row)
  if (insideScroller(el)) (el.closest<HTMLElement>(".field, .acts") ?? el).scrollIntoView?.({ block: "nearest" });
}

// whether the next Tab (or Shift+Tab) inside a region would wrap round to its other end: a
// region that is not the only one hands the focus on instead (the chat dock beside a sheet)
export function wouldWrap(backwards: boolean, within: HTMLElement | null): boolean {
  if (!within || within.hidden) return true;
  const all = Array.from(within.querySelectorAll<HTMLElement>(FOCUSABLE)).filter((el) => visible(el));
  if (!all.length) return true;
  const at = all.indexOf(document.activeElement as HTMLElement);
  return at < 0 || (backwards ? at === 0 : at === all.length - 1);
}

// a region the focus may move into: shown and with something to focus
export function focusableIn(el: HTMLElement | null): el is HTMLElement {
  return !!el && !el.hidden && Array.from(el.querySelectorAll<HTMLElement>(FOCUSABLE)).some((x) => visible(x));
}

function insideScroller(el: HTMLElement): boolean {
  for (let p = el.parentElement; p; p = p.parentElement) {
    const y = getComputedStyle(p).overflowY;
    if (y === "auto" || y === "scroll") return true;
  }
  return false;
}

export function isTextField(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el || !el.tagName) return false;
  const tag = el.tagName;
  return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || el.isContentEditable;
}
