// A phone's on-screen keyboard comes up only for a field focused inside the tap itself; the chat
// line is drawn a frame later (React), too late. So the touch controls' chat cell focuses this
// stand-in field in its touchend, which brings the keyboard up, and the chat line then takes the
// focus from it: a focus moving from one field to another keeps the keyboard up. Invisible,
// 16 px (iOS zooms into a smaller field), out of the tab order; let go after a second if no
// field took over (the panel did not open).
let proxy: HTMLInputElement | null = null;

export function raiseKeyboard() {
  if (!proxy) {
    proxy = document.createElement("input");
    proxy.type = "text";
    proxy.tabIndex = -1;
    proxy.autocomplete = "off";
    proxy.setAttribute("aria-hidden", "true");
    proxy.className = "soft-keyboard";
    document.body.appendChild(proxy);
  }
  proxy.focus({ preventScroll: true });
  const mine = proxy;
  window.setTimeout(() => {
    if (document.activeElement === mine) mine.blur();
  }, 1000);
}
