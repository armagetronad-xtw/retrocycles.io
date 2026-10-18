// The Training screen: every time Training (the local game against the AI,
// aa.startLocal) starts, the important controls over the game, the game paused behind them;
// any key, click or tap goes on. The keys are the player's own (keys.ts keyItems, KeyCaps);
// on a touch device the on-screen controls in a few words instead (TouchControls.tsx).
//
// The pause is the desktop client's PAUSE_GAME action (gGame.cpp pausegame_func, a toggle of
// eTIMER_PAUSE_BUTTON; bound to the Pause key by default.cfg): the API has no pause call, so
// the page presses the key the action is bound to, a synthetic keydown/keyup on window, where
// the client's key handler listens (sdl_shim.cpp OnKey). The client drops keys while the focus
// is inside data-aa-keys="page", so the press is made with the focus on the canvas. A toggle
// can come out wrong (a press the client missed, or one left over from an earlier game), so
// the round clock (state.gametime) is watched: while the screen is up it must stand still,
// after it it must run, and a wrong state is pressed again.
//
// The screen has the keys (data-aa-keys="page", focused): the dismissing key never reaches the
// game (its keyup is dropped too: the client never saw it go down).
import { useEffect, useRef } from "react";
import type { Aa } from "../aa/types";
import { actionSay, keyItems, type ActionWords, type HintItem } from "../keys";
import { t, type PlainKey } from "../i18n";
import { touchPrimary } from "../compact";
import { rowLayout, type TouchLayout } from "../touchLayout";
import { KeyGroups } from "./KeyCaps";

const SECTIONS: Array<{ head: PlainKey; actions: ActionWords[] }> = [
  {
    head: "game.training.drive",
    actions: [
      actionSay("CYCLE_TURN_LEFT", "game.keys.turnLeft"),
      actionSay("CYCLE_TURN_RIGHT", "game.keys.turnRight"),
      actionSay("CYCLE_BRAKE", "game.keys.brake")
    ]
  },
  {
    head: "game.training.look",
    actions: [
      actionSay("GLANCE_LEFT", "game.keys.glanceLeft"),
      actionSay("GLANCE_RIGHT", "game.keys.glanceRight"),
      actionSay("GLANCE_BACK", "game.keys.glanceBack")
    ]
  },
  {
    head: "game.training.page",
    actions: [
      actionSay("INGAME_MENU", "game.keys.menu"),
      actionSay("CHAT", "game.keys.chat"),
      actionSay("SCORE", "game.keys.scoreboard"),
      actionSay("TOGGLE_FULLSCREEN", "game.keys.fullscreen")
    ]
  }
];

// the key PAUSE_GAME is bound to (Pause unless the player moved it)
function pauseCode(aa: Aa): string {
  try {
    return aa.getBindings().find((b) => b.action === "PAUSE_GAME" && b.code)?.code ?? "Pause";
  } catch {
    return "Pause";
  }
}

// one press of the pause key, as the client's key handler takes it
function pressPause(aa: Aa) {
  const code = pauseCode(aa);
  const back = document.activeElement as HTMLElement | null;
  document.getElementById("canvas")?.focus({ preventScroll: true });
  for (const type of ["keydown", "keyup"]) window.dispatchEvent(new KeyboardEvent(type, { code, key: code }));
  back?.focus({ preventScroll: true });
}

// seconds of the countdown run before the pause (the arena faded in)
const LEAD = 0.6;

const gametime = (aa: Aa): number | null => {
  try {
    const s = aa.getState();
    return s.state === "local" ? s.gametime : null;
  } catch {
    return null;
  }
};

export function Training({ aa, layout, onClose }: { aa: Aa; layout: TouchLayout; onClose(): void }) {
  const ref = useRef<HTMLDivElement>(null);
  const touch = touchPrimary();
  const shownAt = useRef(performance.now());
  const closed = useRef(false);

  // paused while up. The press waits until the round's clock has run LEAD seconds of the
  // countdown (the cycles stand still until 0): at its very start the game has not faded the
  // arena in yet, and a pause there leaves the screen over black. A clock that stands still
  // for a while is paused already (an earlier toggle left on) and is not pressed. Then the
  // clock is watched: a running one is pressed again
  useEffect(() => {
    ref.current?.focus({ preventScroll: true });
    let from: number | null = null; // where the clock started running from (a reset goes back)
    let last: number | null = null;
    let movedAt = performance.now();
    let pressedAt = 0;
    let presses = 0;
    const t = window.setInterval(() => {
      if (closed.current) return;
      const now = gametime(aa);
      const running = now !== null && last !== null && now !== last;
      if (running) movedAt = performance.now();
      if (now !== null && (from === null || now < from)) from = now;
      last = now;
      if (pressedAt === 0) {
        const ran = now !== null && from !== null && now - from >= LEAD;
        // (counted from the countdown on: before the round the clock stands at 0 while it loads)
        const still = from !== null && from < 0 && performance.now() - movedAt > 1500;
        if (ran) {
          pressPause(aa);
          presses++;
        }
        if (ran || still) pressedAt = performance.now();
        return;
      }
      if (running && presses < 4 && performance.now() - pressedAt > 600) {
        pressPause(aa);
        pressedAt = performance.now();
        presses++;
      }
    }, 50);
    return () => window.clearInterval(t);
  }, [aa]);

  // on: a clock that stands still is pressed (and pressed again if the press was missed)
  const close = () => {
    if (closed.current || performance.now() - shownAt.current < 300) return;
    closed.current = true;
    document.getElementById("canvas")?.focus({ preventScroll: true });
    const still = (then: () => void) => {
      const before = gametime(aa);
      window.setTimeout(() => {
        const after = gametime(aa);
        if (before !== null && after === before) then();
      }, 150);
    };
    still(() => {
      pressPause(aa);
      window.setTimeout(() => still(() => pressPause(aa)), 450);
    });
    onClose();
  };

  const items: HintItem[][] = touch ? [] : SECTIONS.map((s) => keyItems(aa, s.actions, 2));

  return (
    <div
      className="training"
      id="training"
      ref={ref}
      tabIndex={-1}
      data-aa-keys="page"
      role="dialog"
      aria-modal="true"
      aria-labelledby="training-title"
      onKeyDown={(e) => {
        if (e.repeat) return;
        e.preventDefault();
        e.stopPropagation();
        close();
      }}
      // the keys stay here while it is up (App gives the canvas the focus as the game starts,
      // after this mounted; pressPause borrows it for a moment)
      onBlur={() =>
        window.setTimeout(() => {
          if (!closed.current && ref.current && document.activeElement !== ref.current) ref.current.focus({ preventScroll: true });
        }, 0)
      }
      onPointerDown={(e) => {
        e.preventDefault();
        close();
      }}
    >
      <div className="training-body">
        <h2 className="training-title" id="training-title">
          {t("game.training.title")}
        </h2>
        <p className="training-say">{t("game.training.say")}</p>
        {touch ? (
          <dl className="training-touch">
            <dt>{t("game.training.turn")}</dt>
            <dd>{rowLayout(layout) ? t("game.training.turnRow") : t("game.training.turnSides")}</dd>
            {rowLayout(layout) && (
              <>
                <dt>{t("game.training.brake")}</dt>
                <dd>{t("game.training.brakeHold")}</dd>
              </>
            )}
            <dt>{t("game.training.cells")}</dt>
            <dd>{t("game.training.cellsWhere")}</dd>
          </dl>
        ) : (
          <div className="training-sections">
            {SECTIONS.map((s, i) =>
              items[i].length ? (
                <section className="training-section" key={s.head}>
                  <h3 className="training-head">{t(s.head)}</h3>
                  <KeyGroups className="training-keys" items={items[i]} />
                </section>
              ) : null
            )}
          </div>
        )}
        <p className="training-go">{touch ? t("game.training.tap") : t("game.training.press")}</p>
      </div>
    </div>
  );
}
