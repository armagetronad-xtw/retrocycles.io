// On-screen controls for a touch device in a game (compact.ts touchPrimary), in
// the layout chosen in Settings, CONTROLS (touchLayout.ts):
// - standard: at each side two buttons under the thumb, a turn (the larger, at the foot) and a
//   double turn above it (two turns the cycle queues, as two quick presses: a turn-around);
// - keyboard: closer to the desktop keys (d f, space, j k), one row at the foot: two turn-left keys,
//   a wider brake, two turn-right keys, every turn a single one (two keys a side, as two fingers
//   play them on a keyboard); the brake holds while pressed (aa.brake) and lets go on release;
// - alternating: the same row as the desktop's ALTERNATING keys, left, right, the brake, left, right.
// And at the top right the chat cell (the chat key's: the chat line, the phone's keyboard up; a dot
// while chat came in unread) and the menu cell (the Esc key's); a phone has neither key.
// Every finger is its own key (a held brake and a turn, or two turns together,
// must all count): the layer listens to the touch events itself, not to the buttons' pointer
// events, which an iPhone ran one at a time. A touch acts on the button it lands on, at once
// (touchstart, not the click after it): a turn through aa.turn() (the turn keys' own action,
// whatever they are bound to); the brake is held until every finger on it lifts. Each finger stays
// with the button it landed on, wherever it moves. A mouse or a pen still works by pointer events.
// Nothing takes the focus, and the page neither scrolls, zooms nor selects under them
// (touch-action, hud.css). The compact HUD's rail stands between the two sides (standard) or above
// the row (keyboard).
import { useEffect, useRef } from "react";
import type { Aa } from "../aa/types";
import { rowLayout, type TouchLayout } from "../touchLayout";
import { raiseKeyboard } from "../softKeyboard";
import { t } from "../i18n";

type Dir = -1 | 1;

function Chevrons({ dir, count }: { dir: Dir; count: 1 | 2 }) {
  // a chevron per turn, pointing the way it turns
  const one = dir < 0 ? "M14 6l-6 6 6 6" : "M10 6l6 6-6 6";
  const two = dir < 0 ? "M11 6l-6 6 6 6M18 6l-6 6 6 6" : "M6 6l6 6-6 6M13 6l6 6-6 6";
  return (
    <svg width={count === 2 ? 30 : 34} height={count === 2 ? 30 : 34} viewBox="0 0 24 24" fill="none" stroke="currentColor"
      strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter" aria-hidden="true">
      <path d={count === 2 ? two : one} />
    </svg>
  );
}

// what a button does: data-act on it, read by the layer's handlers
function turnAct(dir: Dir, count: 1 | 2) {
  return "turn " + dir + " " + count;
}

function TurnButton({ dir, count, row, second }: { dir: Dir; count: 1 | 2; row?: boolean; second?: boolean }) {
  const side = dir < 0 ? "left" : "right";
  return (
    <button
      type="button"
      tabIndex={-1}
      className={"touch-btn" + (count === 2 ? " is-double" : "") + (row ? " is-row" : "")}
      id={"touch-" + side + (count === 2 ? "-double" : "") + (second ? "-2" : "")}
      aria-label={count === 2 ? t(dir < 0 ? "game.touch.doubleTurnLeft" : "game.touch.doubleTurnRight") : t(dir < 0 ? "game.touch.turnLeft" : "game.touch.turnRight")}
      data-act={turnAct(dir, count)}
    >
      <Chevrons dir={dir} count={count} />
    </button>
  );
}

// a client from before aa.brake (an old copy still loaded): the key is there, dimmed, and does nothing
function BrakeButton({ can }: { can: boolean }) {
  return (
    <button type="button" tabIndex={-1} className={"touch-btn is-row is-brake" + (can ? "" : " is-off")} id="touch-brake" aria-label={t("game.touch.brake")} data-act="brake">
      <span className="touch-brake-word">{t("game.touch.brakeWord")}</span>
    </button>
  );
}

export function TouchControls({ aa, layout, unread, onMenu, onChat, onTurn }: {
  aa: Aa; layout: TouchLayout; unread: boolean; onMenu(): void; onChat(): void;
  /** a turn button was pressed (analytics: the first turn of the page load) */
  onTurn?(): void;
}) {
  const layer = useRef<HTMLDivElement>(null);
  const menu = useRef(onMenu);
  menu.current = onMenu;
  const turned = useRef(onTurn);
  turned.current = onTurn;
  const chat = useRef(onChat);
  chat.current = onChat;
  const canBrake = typeof aa.brake === "function";

  useEffect(() => {
    const root = layer.current;
    if (!root) return;
    // the fingers (or the one mouse/pen pointer) down now, each with the button it landed on
    const held = new Map<string, HTMLElement>();
    let braking = false;

    const pressedNow = (el: HTMLElement) => [...held.values()].some((x) => x === el);
    const syncBrake = () => {
      const on = [...held.values()].some((el) => el.dataset.act === "brake");
      if (on === braking) return;
      braking = on;
      aa.brake?.(on);
    };
    const press = (key: string, el: HTMLElement) => {
      held.set(key, el);
      el.dataset.down = "";
      const act = el.dataset.act || "";
      // the chat cell acts on the lift: a field focused there brings the keyboard up
      if (act === "chat") return;
      if (act === "menu") menu.current();
      else if (act.startsWith("turn ")) {
        const [, dir, count] = act.split(" ");
        aa.turn?.(Number(dir), Number(count) === 2 ? 2 : 1);
        turned.current?.();
      } else if (act === "brake") syncBrake();
    };
    const lift = (key: string) => {
      const el = held.get(key);
      if (!el) return;
      held.delete(key);
      if (!pressedNow(el)) delete el.dataset.down;
      syncBrake();
      if (el.dataset.act === "chat") {
        raiseKeyboard();
        chat.current();
      }
    };
    const target = (node: EventTarget | null) => (node instanceof Element ? (node.closest("[data-act]") as HTMLElement | null) : null);

    const touchStart = (e: TouchEvent) => {
      let mine = false;
      for (const t of Array.from(e.changedTouches)) {
        const el = target(t.target);
        if (!el || !root.contains(el)) continue;
        mine = true;
        press("t" + t.identifier, el);
      }
      // no click, no double-tap zoom, no text selection after a press
      if (mine && e.cancelable) e.preventDefault();
    };
    const touchEnd = (e: TouchEvent) => {
      let mine = false;
      for (const t of Array.from(e.changedTouches)) {
        if (!held.has("t" + t.identifier)) continue;
        mine = true;
        lift("t" + t.identifier);
      }
      if (mine && e.cancelable) e.preventDefault();
    };
    // a held finger moving neither scrolls (iPhone Safari's bar-folding scroll) nor zooms
    const touchMove = (e: TouchEvent) => {
      if (e.cancelable && Array.from(e.changedTouches).some((t) => held.has("t" + t.identifier))) e.preventDefault();
    };
    // a mouse or a pen (a touch comes through the touch events above)
    const pointerDown = (e: PointerEvent) => {
      if (e.pointerType === "touch") return;
      const el = target(e.target);
      if (!el) return;
      e.preventDefault();
      press("p" + e.pointerId, el);
    };
    const pointerUp = (e: PointerEvent) => {
      if (e.pointerType !== "touch") lift("p" + e.pointerId);
    };

    root.addEventListener("touchstart", touchStart, { passive: false });
    root.addEventListener("touchmove", touchMove, { passive: false });
    root.addEventListener("touchend", touchEnd, { passive: false });
    root.addEventListener("touchcancel", touchEnd, { passive: false });
    root.addEventListener("pointerdown", pointerDown);
    window.addEventListener("pointerup", pointerUp);
    window.addEventListener("pointercancel", pointerUp);
    return () => {
      root.removeEventListener("touchstart", touchStart);
      root.removeEventListener("touchmove", touchMove);
      root.removeEventListener("touchend", touchEnd);
      root.removeEventListener("touchcancel", touchEnd);
      root.removeEventListener("pointerdown", pointerDown);
      window.removeEventListener("pointerup", pointerUp);
      window.removeEventListener("pointercancel", pointerUp);
      for (const el of held.values()) delete el.dataset.down;
      held.clear();
      // the controls going (the menu opened over a held brake) let the brake go
      if (braking) aa.brake?.(false);
    };
  }, [aa, layout]);

  return (
    <div className="touch" id="touch-controls" data-layout={layout} ref={layer} onContextMenu={(e) => e.preventDefault()}>
      <button type="button" tabIndex={-1} className="touch-menu is-chat" id="touch-chat" aria-label={unread ? t("game.touch.chatUnread") : t("game.touch.chat")} data-act="chat" data-unread={unread ? "" : undefined}>
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" strokeLinejoin="miter" aria-hidden="true">
          <path d="M4 5h16v11H9l-5 4z" />
        </svg>
      </button>
      <button type="button" tabIndex={-1} className="touch-menu" id="touch-menu" aria-label={t("game.touch.menu")} data-act="menu">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" aria-hidden="true">
          <path d="M4 7h16M4 12h16M4 17h16" />
        </svg>
      </button>
      {layout === "alternating" ? (
        <div className="touch-row">
          <TurnButton dir={-1} count={1} row />
          <TurnButton dir={1} count={1} row />
          <BrakeButton can={canBrake} />
          <TurnButton dir={-1} count={1} row second />
          <TurnButton dir={1} count={1} row second />
        </div>
      ) : rowLayout(layout) ? (
        <div className="touch-row">
          <TurnButton dir={-1} count={1} row />
          <TurnButton dir={-1} count={1} row second />
          <BrakeButton can={canBrake} />
          <TurnButton dir={1} count={1} row />
          <TurnButton dir={1} count={1} row second />
        </div>
      ) : (
        <>
          <div className="touch-side is-left">
            <TurnButton dir={-1} count={2} />
            <TurnButton dir={-1} count={1} />
          </div>
          <div className="touch-side is-right">
            <TurnButton dir={1} count={2} />
            <TurnButton dir={1} count={1} />
          </div>
        </>
      )}
    </div>
  );
}
