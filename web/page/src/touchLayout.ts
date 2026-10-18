// Which touch buttons a phone or tablet plays with (Settings, CONTROLS):
// - standard: at each side a turn and a double turn above it (TouchControls);
// - keyboard: closer to the desktop keys (d f, space, j k), one row at the foot: two turn-left
//   keys, a wider brake, two turn-right keys, every turn a single one (and the rail shows the
//   brakes again);
// - alternating: the same row as the desktop's ALTERNATING keys (d j left, f k right): left,
//   right, the brake, left, right.
// A page setting in localStorage (rc.touchLayout, a bare word); a change reaches the game screen at
// once (useTouchLayout listens).
import { useEffect, useState } from "react";

export type TouchLayout = "standard" | "keyboard" | "alternating";

// the layouts with a row of keys and a brake
export const rowLayout = (layout: TouchLayout) => layout !== "standard";

const KEY = "rc.touchLayout";
const EVENT = "rc-touch-layout";
let memo: TouchLayout = "standard"; // blocked storage: this page load only

export function touchLayout(): TouchLayout {
  try {
    const v = window.localStorage.getItem(KEY);
    return v === "keyboard" || v === "alternating" || v === "standard" ? v : memo;
  } catch {
    return memo;
  }
}

export function setTouchLayout(layout: TouchLayout) {
  memo = layout;
  try {
    if (layout === "standard") window.localStorage.removeItem(KEY);
    else window.localStorage.setItem(KEY, layout);
  } catch {
    // the memo keeps it for this page load
  }
  window.dispatchEvent(new Event(EVENT));
}

export function useTouchLayout(): TouchLayout {
  const [layout, setLayout] = useState(touchLayout);
  useEffect(() => {
    const update = () => setLayout(touchLayout());
    window.addEventListener(EVENT, update);
    window.addEventListener("storage", update);
    return () => {
      window.removeEventListener(EVENT, update);
      window.removeEventListener("storage", update);
    };
  }, []);
  return layout;
}
