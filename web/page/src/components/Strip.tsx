// The menu: the play modes as big rows down the left, over a dark falloff, like lanes of
// the grid. A lightcycle wall stands round three sides of the focused row (menuCycle.ts);
// it comes in from the screen edge when the menu shows, and moving the focus runs it down or
// up the rows' shared side line to the new row, its tail pulling off the old one. Quick Play
// is a step bigger than the other rows, so it reads first wherever the wall is. Hovering or
// focusing mode k tells the page, which plays the attract scene to state k (App.tsx);
// clicking selects.
// Quick Play carries how many play now, when the platform says enough do (livePlayers).
// The nav's children are the mode rows (in MODES order) and then the note: App.tsx finds
// row i as `children[i]`.
import { forwardRef, useLayoutEffect, useRef } from "react";
import { MODES, type Mode } from "../modes";
import { t } from "../i18n";
import { LOOP_GAP, MenuWall, type Loop } from "./menuCycle";

export interface Note {
  text: string;
  quiet: boolean;
  tile: number;
}

interface StripProps {
  focused: number;
  note: Note | null;
  live: number | null;
  onFocusTile(index: number, moveDom: boolean): void;
  onSelect(mode: Mode): void;
}

function unit(): number {
  // --u (faces.css): 1px at 1536 wide, clamped to 0.8-1.25px
  return Math.min(1.25, Math.max(0.8, window.innerWidth * 0.000651));
}

// each row's box for the wall, in the menu's px: LOOP_GAP outside the row on every side
function loopsOf(menu: HTMLElement, nav: HTMLElement, u: number): Loop[] {
  const m = menu.getBoundingClientRect();
  const g = LOOP_GAP * u;
  const loops: Loop[] = [];
  for (let i = 0; i < MODES.length; i++) {
    const row = nav.children[i] as HTMLElement | undefined;
    if (!row) break;
    const box = row.getBoundingClientRect();
    loops.push({
      l: Math.round(box.left - m.left - g),
      t: Math.round(box.top - m.top - g),
      r: Math.round(box.right - m.left + g),
      b: Math.round(box.bottom - m.top + g)
    });
  }
  return loops;
}

export const Strip = forwardRef<HTMLElement, StripProps>(function Strip({ focused, note, live, onFocusTile, onSelect }, ref) {
  const menuRef = useRef<HTMLDivElement>(null);
  const navRef = useRef<HTMLElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const wallRef = useRef<MenuWall | null>(null);
  const loopsRef = useRef<Loop[]>([]);
  const focusedRef = useRef(focused);
  const moveRef = useRef<() => void>(() => {});

  // the trail: it stands still round the focused row, and frames run only while it moves
  // (an entrance, a move to another row). Each time the menu shows (after loading, a stage,
  // the game, a sheet) it comes in from the screen edge again. With reduced motion it is
  // simply there, round the focused row.
  useLayoutEffect(() => {
    const menu = menuRef.current;
    const nav = navRef.current;
    const canvas = canvasRef.current;
    const ctx = canvas?.getContext("2d");
    if (!menu || !nav || !canvas || !ctx) return;
    const still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const wall = new MenuWall(unit());
    wallRef.current = wall;
    let frame = 0;
    let shown = false;
    let w = 0;
    let h = 0;

    const measure = () => {
      const u = unit();
      wall.setUnit(u);
      const dpr = window.devicePixelRatio || 1;
      w = menu.offsetWidth;
      // the last row's wall hangs below the menu
      h = menu.offsetHeight + Math.ceil(32 * u);
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      canvas.style.width = w + "px";
      canvas.style.height = h + "px";
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      loopsRef.current = loopsOf(menu, nav, u);
    };
    const loop = () => loopsRef.current[focusedRef.current];
    const visible = () =>
      !document.hidden &&
      (canvas.checkVisibility ? canvas.checkVisibility({ opacityProperty: true, visibilityProperty: true }) : true);

    const paint = () => {
      ctx.clearRect(0, 0, w, h);
      wall.draw(ctx);
    };
    const tick = (now: number) => {
      frame = 0;
      const more = wall.update(now);
      paint();
      if (more) frame = requestAnimationFrame(tick);
    };
    const run = () => {
      if (!frame) frame = requestAnimationFrame(tick);
    };
    // the menu came into view: the entrance (or, still, the wall in place)
    const show = () => {
      if (!visible()) {
        shown = false;
        return;
      }
      if (shown) return;
      const l = loop();
      if (!l) return;
      shown = true;
      if (still) {
        wall.rest(l);
        paint();
      } else {
        wall.enter(l, performance.now());
        run();
      }
    };
    // the focus moved to row focusedRef.current
    moveRef.current = () => {
      const l = loop();
      if (!l) return;
      if (!shown || still) {
        wall.rest(l);
        if (shown) paint();
        return;
      }
      wall.retarget(l, performance.now());
      run();
    };

    measure();
    show();
    const relayout = () => {
      const was = JSON.stringify(loopsRef.current);
      measure();
      const l = loop();
      if (l && JSON.stringify(loopsRef.current) !== was) wall.rest(l);
      paint();
    };
    const watch = new ResizeObserver(relayout);
    watch.observe(menu);
    window.addEventListener("resize", relayout);
    // the menu shows and hides by the body's classes (and its fades end later: a class
    // change is checked again once they are over)
    let late = 0;
    const classes = new MutationObserver(() => {
      show();
      clearTimeout(late);
      late = window.setTimeout(show, 400);
    });
    classes.observe(document.body, { attributes: true, attributeFilter: ["class"] });
    // the faces settle the rows' sizes (Akshar is font-display: block)
    document.fonts?.ready.then(relayout);
    return () => {
      cancelAnimationFrame(frame);
      clearTimeout(late);
      watch.disconnect();
      classes.disconnect();
      window.removeEventListener("resize", relayout);
      wallRef.current = null;
    };
  }, []);

  // a new focus: the wall runs to that row
  useLayoutEffect(() => {
    focusedRef.current = focused;
    moveRef.current();
  }, [focused]);

  // the falloff behind the menu ends where the menu ends (page.css .frame::before): its
  // bottom edge, in px from the top, as --menu-bottom on the root, kept current as the
  // rows or the viewport change
  useLayoutEffect(() => {
    const menu = menuRef.current;
    if (!menu) return;
    const root = document.documentElement;
    const mark = () => root.style.setProperty("--menu-bottom", Math.round(menu.getBoundingClientRect().bottom) + "px");
    mark();
    const watch = new ResizeObserver(mark);
    watch.observe(menu);
    window.addEventListener("resize", mark);
    return () => {
      watch.disconnect();
      window.removeEventListener("resize", mark);
      root.style.removeProperty("--menu-bottom");
    };
  }, []);

  return (
    <div className="home-menu" id="home-menu" ref={menuRef}>
      <canvas className="trail" aria-hidden="true" ref={canvasRef} />

      <nav
        className="strip"
        id="strip"
        aria-label={t("home.aria.modes")}
        ref={(el) => {
          navRef.current = el;
          if (typeof ref === "function") ref(el);
          else if (ref) ref.current = el;
        }}
      >
        {MODES.map((m, i) => (
          <button
            key={m.mode}
            className={"tile" + (focused === i ? " is-focus" : "")}
            type="button"
            id={"tile-" + m.mode}
            data-mode={m.mode}
            data-scene={m.scene}
            aria-current={focused === i ? "true" : undefined}
            onMouseEnter={() => onFocusTile(i, false)}
            onFocus={() => onFocusTile(i, false)}
            onClick={() => onSelect(m.mode)}
          >
            <span className="tile-label">{m.title}</span>
            {m.mode === "quick" && live !== null && (
              <span className="tile-count" id="live-count">
                {t("home.livePlaying", { live })}
              </span>
            )}
          </button>
        ))}
        <p className={"tile-note" + (note && note.quiet ? " is-quiet" : "")} id="tile-note" role="status" hidden={!note}>
          {note ? note.text : ""}
        </p>
      </nav>

    </div>
  );
});
