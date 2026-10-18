// The Settings sheet's controls: a row is a key at the left and its value at the right under a hairline, the
// focused row fills near-black with the 4u ink bar (settings.css). Every control takes the keys
// the desktop menus taught (Left / Right change the value, Enter acts) and the pointer; Up and
// Down are the sheet's (they move between rows), so the controls let them bubble.
import { useEffect, useId, useRef, useState, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent, type ReactNode } from "react";
import { REFERENCE_SPEED, type SliderRow } from "../../settings/schema";
import { num, t as tr, type PlainKey } from "../../i18n";
import { CYCLE_REAR, CYCLE_SIDE, CYCLE_TOP } from "./cycleShapes";
import { ColorInput } from "../ColorInput";

// an option as the controls show it: its words already in the player's language
export interface ShownOption {
  value: string;
  label: string;
}

// ---- the row ----------------------------------------------------------------------------------

interface ShellProps {
  id: string;
  label: ReactNode;
  // a value off its stock one: CHANGED at the right end, which resets it (RESET on hover / focus)
  changed?: boolean;
  onReset?(): void;
  disabled?: boolean;
  // a quiet caps note at the right end in the CHANGED place ("hud is off", "this server sets it")
  note?: string | null;
  hint?: ReactNode;
  // the control takes the whole row under its key (the rig, the swatches)
  wide?: boolean;
  tab?: string | null; // FIND's results: the row's tab and group
  children: ReactNode;
}

export function RowShell({ id, label, changed, onReset, disabled, note, hint, wide, tab, children }: ShellProps) {
  return (
    <div className={"st-row" + (wide ? " is-wide" : "") + (disabled ? " is-off" : "")} id={"st-" + id} data-row={id}>
      <span className="st-key" id={"st-" + id + "-key"}>
        {label}
        {tab && <span className="st-where">{tab}</span>}
      </span>
      <div className="st-ctl">{children}</div>
      <span className="st-end">
        {note ? (
          <span className="st-note">{note}</span>
        ) : changed && onReset ? (
          <button className="st-changed" type="button" tabIndex={-1} onClick={onReset} aria-label={tr("settings.resetStock")}>
            <span className="st-changed-is">{tr("settings.changed")}</span>
            <span className="st-changed-do">{tr("settings.reset")}</span>
          </button>
        ) : null}
      </span>
      {hint && <p className="st-hint">{hint}</p>}
    </div>
  );
}

// ---- on / off ---------------------------------------------------------------------------------

export function Switch({ id, on, disabled, onChange }: { id: string; on: boolean; disabled?: boolean; onChange(on: boolean): void }) {
  const key = (e: ReactKeyboardEvent) => {
    if (disabled) return;
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      e.preventDefault();
      e.stopPropagation();
      const next = e.key === "ArrowRight";
      if (next !== on) onChange(next);
    }
  };
  return (
    <button
      className={"st-switch" + (on ? " is-on" : "")}
      id={"st-" + id + "-ctl"}
      type="button"
      role="switch"
      aria-checked={on}
      aria-labelledby={"st-" + id + "-key"}
      disabled={disabled}
      data-nav=""
      onKeyDown={key}
      onClick={() => onChange(!on)}
    >
      <span className="st-switch-no">{tr("common.off")}</span>
      <span className="st-switch-yes">{tr("common.on")}</span>
    </button>
  );
}

// ---- a few options, all shown -----------------------------------------------------------------

export function Segment({ id, options, value, disabled, onChange }: { id: string; options: ShownOption[]; value: string; disabled?: boolean; onChange(v: string): void }) {
  const at = options.findIndex((o) => sameOption(o.value, value));
  const key = (e: ReactKeyboardEvent) => {
    if (disabled) return;
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    e.stopPropagation();
    const step = e.key === "ArrowRight" ? 1 : -1;
    const next = options[Math.max(0, Math.min(options.length - 1, (at < 0 ? 0 : at) + step))];
    if (next && !sameOption(next.value, value)) onChange(next.value);
  };
  return (
    <div className="st-seg" id={"st-" + id + "-ctl"} role="radiogroup" aria-labelledby={"st-" + id + "-key"} aria-disabled={disabled || undefined} tabIndex={disabled ? -1 : 0} data-nav="" onKeyDown={key}>
      {options.map((o) => {
        const chosen = sameOption(o.value, value);
        return (
          <span
            key={o.value}
            className={"st-seg-opt" + (chosen ? " is-on" : "")}
            role="radio"
            aria-checked={chosen}
            onClick={() => !disabled && !chosen && onChange(o.value)}
          >
            {o.label}
          </span>
        );
      })}
    </div>
  );
}

function sameOption(a: string, b: string): boolean {
  return a === b || (a !== "" && b !== "" && Number(a) === Number(b));
}

// ---- one of many ---------------------------------------------------------------------------------

export function Choice({ id, options, value, words, disabled, onChange, onEnter }: { id: string; options: ShownOption[]; value: string; words?: string; disabled?: boolean; onChange(v: string): void; onEnter?(): boolean }) {
  const at = options.findIndex((o) => sameOption(o.value, value));
  const go = (step: number) => {
    if (disabled || !options.length) return;
    const from = at < 0 ? (step > 0 ? -1 : 0) : at;
    const next = options[(from + step + options.length) % options.length];
    onChange(next.value);
  };
  const key = (e: ReactKeyboardEvent) => {
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      e.preventDefault();
      e.stopPropagation();
      go(e.key === "ArrowRight" ? 1 : -1);
    } else if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      e.stopPropagation();
      // a row may take Enter for itself (the layout: apply the one picked)
      if (!(e.key === "Enter" && onEnter?.())) go(1);
    }
  };
  const shown = words ?? (at >= 0 ? options[at].label : value || tr("settings.opt.custom"));
  return (
    <div
      className="st-choice"
      id={"st-" + id + "-ctl"}
      role="spinbutton"
      aria-labelledby={"st-" + id + "-key"}
      aria-valuetext={shown}
      aria-disabled={disabled || undefined}
      tabIndex={disabled ? -1 : 0}
      data-nav=""
      onKeyDown={key}
    >
      <button className="st-chev" type="button" tabIndex={-1} aria-hidden="true" disabled={disabled} onClick={() => go(-1)}>
        <Chevron left />
      </button>
      <span className="st-choice-val" onClick={() => go(1)}>
        {shown}
      </span>
      <button className="st-chev" type="button" tabIndex={-1} aria-hidden="true" disabled={disabled} onClick={() => go(1)}>
        <Chevron />
      </button>
    </div>
  );
}

function Chevron({ left }: { left?: boolean }) {
  return (
    <svg className="st-chev-mark" viewBox="0 0 24 24" aria-hidden="true">
      <path d={left ? "M15 5 L8 12 L15 19" : "M9 5 L16 12 L9 19"} />
    </svg>
  );
}

// ---- a number on a range ---------------------------------------------------------------------

export function roundTo(v: number, step: number): number {
  return Number((Math.round(v / step) * step).toFixed(4));
}

export function clampTo(row: Pick<SliderRow, "min" | "max" | "step">, v: number): number {
  return roundTo(Math.max(row.min, Math.min(row.max, v)), row.step);
}

export function sliderWords(row: Pick<SliderRow, "unit" | "words">, v: number): string {
  if (row.words) return row.words(v);
  const n = num(Math.round(v * 100) / 100 + 0);
  if (!row.unit) return n;
  return row.unit === "%" || row.unit === "°" ? n + row.unit : n + " " + row.unit;
}

interface SliderProps {
  id: string;
  row: Pick<SliderRow, "min" | "max" | "step" | "unit" | "words" | "def">;
  value: number;
  disabled?: boolean;
  onChange(v: number): void;
}

export function Slider({ id, row, value, disabled, onChange }: SliderProps) {
  const line = useRef<HTMLSpanElement>(null);
  const box = useRef<HTMLDivElement>(null);
  const [typing, setTyping] = useState<string | null>(null);
  const v = Number.isFinite(value) ? value : row.def;
  const span = row.max - row.min || 1;
  const at = (x: number) => Math.max(0, Math.min(1, (x - row.min) / span));

  const fromPointer = (clientX: number) => {
    const r = line.current?.getBoundingClientRect();
    if (!r || r.width <= 0) return;
    const next = clampTo(row, row.min + ((clientX - r.left) / r.width) * span);
    if (next !== v) onChange(next);
  };

  const down = (e: ReactPointerEvent<HTMLSpanElement>) => {
    if (disabled || e.button !== 0) return;
    e.preventDefault();
    box.current?.focus({ preventScroll: true });
    e.currentTarget.setPointerCapture(e.pointerId);
    fromPointer(e.clientX);
  };

  const key = (e: ReactKeyboardEvent) => {
    if (disabled || typing !== null) return;
    let next: number | null = null;
    const big = row.step * 10;
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") next = v + (e.key === "ArrowRight" ? 1 : -1) * (e.shiftKey ? big : row.step);
    else if (e.key === "PageUp" || e.key === "PageDown") next = v + (e.key === "PageUp" ? big : -big);
    else if (e.key === "Home") next = row.min;
    else if (e.key === "End") next = row.max;
    else if (e.key === "Enter") {
      e.preventDefault();
      e.stopPropagation();
      setTyping(String(v));
      return;
    } else if (/^[0-9.-]$/.test(e.key) && !e.ctrlKey && !e.metaKey && !e.altKey) {
      e.preventDefault();
      e.stopPropagation();
      setTyping(e.key);
      return;
    }
    if (next === null) return;
    e.preventDefault();
    e.stopPropagation();
    next = clampTo(row, next);
    if (next !== v) onChange(next);
  };

  const commit = (text: string | null) => {
    setTyping(null);
    window.requestAnimationFrame(() => box.current?.focus({ preventScroll: true }));
    if (text === null) return;
    const n = Number(text.replace(",", "."));
    if (!Number.isFinite(n)) return;
    const next = clampTo(row, n);
    if (next !== v) onChange(next);
  };

  const words = sliderWords(row, v);
  return (
    <div
      className={"st-slider" + (typing !== null ? " is-typing" : "")}
      id={"st-" + id + "-ctl"}
      ref={box}
      role="slider"
      aria-labelledby={"st-" + id + "-key"}
      aria-valuemin={row.min}
      aria-valuemax={row.max}
      aria-valuenow={v}
      aria-valuetext={words}
      aria-disabled={disabled || undefined}
      tabIndex={disabled ? -1 : 0}
      data-nav=""
      onKeyDown={key}
    >
      {typing !== null ? (
        <TypeIn initial={typing} unit={row.unit} onDone={commit} />
      ) : (
        <span className="st-slider-val" onDoubleClick={() => !disabled && setTyping(String(v))}>
          {words}
        </span>
      )}
      <span className="st-line" ref={line} onPointerDown={down} onPointerMove={(e) => e.currentTarget.hasPointerCapture(e.pointerId) && fromPointer(e.clientX)}>
        <i className="st-line-rule" />
        <i className="st-line-stock" style={{ left: at(row.def) * 100 + "%" }} onDoubleClick={() => !disabled && onChange(row.def)} title={tr("settings.stock")} />
        <i className="st-line-tick" style={{ left: at(v) * 100 + "%" }} />
      </span>
    </div>
  );
}

// the value as a well while it is typed: Enter keeps it, Esc and leaving drop it
function TypeIn({ initial, unit, onDone }: { initial: string; unit?: string; onDone(text: string | null): void }) {
  const input = useRef<HTMLInputElement>(null);
  const done = useRef(false);
  const [text, setText] = useState(initial);
  useEffect(() => {
    const el = input.current;
    if (!el) return;
    el.focus({ preventScroll: true });
    el.setSelectionRange(el.value.length, el.value.length);
  }, []);
  const finish = (t: string | null) => {
    if (done.current) return;
    done.current = true;
    onDone(t);
  };
  return (
    <span className="st-type">
      <input
        ref={input}
        className="st-type-input"
        value={text}
        inputMode="decimal"
        size={6}
        spellCheck={false}
        autoComplete="off"
        aria-label={tr("settings.value")}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => finish(null)}
        onKeyDown={(e) => {
          e.stopPropagation();
          if (e.key === "Enter") {
            e.preventDefault();
            finish(text);
          } else if (e.key === "Escape") {
            e.preventDefault();
            finish(null);
          }
        }}
      />
      {unit && <span className="st-type-unit">{unit}</span>}
    </span>
  );
}

// ---- text --------------------------------------------------------------------------------------

export function TextWell({ id, value, max, disabled, placeholder, colors, onCommit }: { id: string; value: string; max?: number; disabled?: boolean; placeholder?: string; colors?: boolean; onCommit(v: string): void }) {
  const [draft, setDraft] = useState(value);
  const focused = useRef(false);
  useEffect(() => {
    if (!focused.current) setDraft(value);
  }, [value]);
  const Field = colors ? ColorInput : "input";
  return (
    <Field
      className="st-text"
      id={"st-" + id + "-ctl"}
      value={draft}
      maxLength={max}
      disabled={disabled}
      placeholder={placeholder}
      spellCheck={false}
      autoComplete="off"
      aria-labelledby={"st-" + id + "-key"}
      data-nav=""
      onFocus={() => (focused.current = true)}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={() => {
        focused.current = false;
        if (draft !== value) onCommit(draft);
      }}
      onKeyDown={(e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          if (draft !== value) onCommit(draft);
        } else if (e.key === "Escape" && draft !== value) {
          e.preventDefault();
          e.stopPropagation();
          setDraft(value);
        }
      }}
    />
  );
}

// ---- the cycle colour --------------------------------------------------------------------------

// the desktop game's First Setup colours (src/tron/gArmagetron.cpp: tColor * 15)
// label: English (an id; the Profile's tints use it too), name: the key of its words, t(name)
export const COLOUR_PRESETS: Array<{ label: string; name: PlainKey; rgb: [number, number, number] }> = [
  { label: "red", name: "settings.colour.red", rgb: [15, 0, 0] },
  { label: "blue", name: "settings.colour.blue", rgb: [0, 0, 15] },
  { label: "green", name: "settings.colour.green", rgb: [0, 15, 0] },
  { label: "yellow", name: "settings.colour.yellow", rgb: [15, 15, 0] },
  { label: "orange", name: "settings.colour.orange", rgb: [15, 7, 0] },
  { label: "purple", name: "settings.colour.purple", rgb: [7, 0, 15] },
  { label: "magenta", name: "settings.colour.magenta", rgb: [15, 0, 15] },
  { label: "cyan", name: "settings.colour.cyan", rgb: [0, 15, 15] },
  { label: "white", name: "settings.colour.white", rgb: [15, 15, 15] },
  { label: "dark", name: "settings.colour.dark", rgb: [0, 0, 0] }
];

export function css(rgb: [number, number, number]): string {
  return "#" + rgb.map((c) => Math.round((Math.max(0, Math.min(15, c)) / 15) * 255).toString(16).padStart(2, "0")).join("");
}

export function Swatches({ id, rgb, onPick }: { id: string; rgb: [number, number, number]; onPick(rgb: [number, number, number]): void }) {
  const at = COLOUR_PRESETS.findIndex((p) => p.rgb.every((c, i) => c === rgb[i]));
  const key = (e: ReactKeyboardEvent) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    e.stopPropagation();
    const step = e.key === "ArrowRight" ? 1 : -1;
    const next = COLOUR_PRESETS[((at < 0 ? (step > 0 ? -1 : 0) : at) + step + COLOUR_PRESETS.length) % COLOUR_PRESETS.length];
    onPick(next.rgb);
  };
  return (
    <div className="st-swatches" id={"st-" + id + "-ctl"} role="radiogroup" aria-labelledby={"st-" + id + "-key"} tabIndex={0} data-nav="" onKeyDown={key}>
      {COLOUR_PRESETS.map((p, i) => (
        <span
          key={p.label}
          className={"st-swatch" + (i === at ? " is-on" : "")}
          role="radio"
          aria-checked={i === at}
          aria-label={tr(p.name)}
          title={tr(p.name)}
          style={{ background: css(p.rgb) }}
          onClick={() => onPick(p.rgb)}
        />
      ))}
      <span className="st-swatch-mine" aria-hidden="true" style={{ background: css(rgb) }} />
    </div>
  );
}

// ---- the camera rig ------------------------------------------------------------------------------

export interface RigValues {
  back: number;
  rise: number;
  tilt: number; // degrees down
}

// what a drag changes: the standing camera, its tilt, or where speed takes it (_FROMSPEED, metres
// per m/s)
export interface RigChange extends Partial<RigValues> {
  backSpeed?: number;
  riseSpeed?: number;
}

// the glance keys' names, for the plan of the glance (null: not bound)
export interface GlanceKeys {
  left: string | null;
  right: string | null;
  back: string | null;
}

type RigPart = "back" | "rise" | "tilt" | "speed";
type Handle = "cam" | "fast" | "tilt";

interface RigProps {
  id: string;
  values: RigValues;
  speed?: RigValues | null; // where the camera is at speed (its _FROMSPEED added), when that differs
  stock: RigValues;
  focus: RigPart | null; // the row with the focus lights its part
  fov: number; // START_FOV_1: the view's width in degrees (the wedge of what the camera sees)
  glance?: boolean; // the glance's rig: seen from behind, beside a plan of the glance from above
  keys?: GlanceKeys;
  disabled?: boolean;
  onChange(next: RigChange): void;
}

// the drawing's scale, in metres a screen pixel, and how far ahead of the cycle it reaches: room
// for the farthest camera behind and the highest with its label over it (the floor's metres
// take 30 px under it); the cycle stays at the right, so a scale set by the height leaves more
// floor behind it, never less
interface Frame {
  px: number;
  ahead: number;
}

function frameFor(cams: RigValues[], bw: number, bh: number): Frame {
  const far = Math.max(...cams.map((c) => c.back));
  const high = Math.max(...cams.map((c) => c.rise));
  const ahead = Math.max(4, far * 0.3 + 2.5);
  const wide = far * 1.06 + 3 + ahead;
  const px = Math.max(wide / bw, (high * 1.04 + 1) / Math.max(40, bh - 30 - 34), 14 / bw);
  // a scale set by the height leaves width over: some of it goes ahead, where the view meets the floor
  return { px, ahead: ahead + (bw * px - wide) * 0.35 };
}

// a tick every 2, 5, 10 or 20 m: four to eight of them behind the cycle
function tickStep(behind: number): number {
  return [2, 5, 10, 20].find((s) => behind / s <= 8) ?? 20;
}

// the game's vertical view from its horizontal FOV (rViewport::Perspective: a screen wider than
// 3:2 keeps the 3:2 height), so the wedge is what the screen shows
function halfHeightAngle(fov: number): number {
  const aspect = typeof window === "undefined" ? 16 / 9 : window.innerWidth / Math.max(1, window.innerHeight);
  return (Math.atan(Math.tan((Math.min(170, Math.max(10, fov)) * Math.PI) / 360) / Math.min(aspect, 1.5)) * 180) / Math.PI;
}

const near = (a: RigValues, b: RigValues) => Math.abs(a.back - b.back) < 0.05 && Math.abs(a.rise - b.rise) < 0.05 && Math.abs(a.tilt - b.tilt) < 0.5;

// the camera: a body and its lens, looking along +x, in pixels round its centre
function CamShape() {
  return (
    <>
      <rect x={-10} y={-6.5} width={14} height={13} />
      <polygon points="4,-2.5 10,-6.5 10,6.5 4,2.5" />
    </>
  );
}

// a camera in the drawing, scaled by `px` (the metres of one pixel) so it keeps its size on screen
function CamGlyph({ x, y, tilt, px, className }: { x: number; y: number; tilt: number; px: number; className: string }) {
  return (
    <g className={className} transform={`translate(${x} ${y}) rotate(${-tilt}) scale(${px})`}>
      <CamShape />
    </g>
  );
}

const KNOB = 34; // the tilt knob: at the end of the angle's arc, on the line of sight, px from the camera

export function Rig({ id, values, speed, stock, focus, fov, glance, keys, disabled, onChange }: RigProps) {
  const svg = useRef<SVGSVGElement>(null);
  const root = useRef<HTMLDivElement>(null);
  const dragRef = useRef<Handle | null>(null);
  const [drag, setDrag] = useState<Handle | null>(null);
  // the scale holds still while a drag is on (it would run away from the pointer otherwise)
  const held = useRef<Frame | null>(null);
  const [box, setBox] = useState<{ w: number; h: number } | null>(null);
  const titleId = useId();
  const uid = titleId.replace(/[^a-zA-Z0-9]/g, "");

  // the drawing fills its box whatever its shape
  useEffect(() => {
    const el = svg.current;
    if (!el) return;
    const read = () => {
      const r = el.getBoundingClientRect();
      if (r.width < 1 || r.height < 1) return;
      setBox((b) => (b && Math.abs(b.w - r.width) < 0.5 && Math.abs(b.h - r.height) < 0.5 ? b : { w: r.width, h: r.height }));
    };
    read();
    const ro = new ResizeObserver(read);
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const bw = box?.w ?? 600;
  const bh = box?.h ?? 300;
  const fast = speed && (Math.abs(speed.back - values.back) > 0.05 || Math.abs(speed.rise - values.rise) > 0.05) ? speed : null;
  const frame = held.current ?? frameFor([values, stock, ...(fast ? [fast] : [])], bw, bh);
  const px = frame.px; // metres a screen pixel: marks keep their size on screen whatever the scale
  const x1 = frame.ahead;
  const w = bw * px;
  const x0 = x1 - w;
  const h = bh * px;
  const y0 = -30 * px; // room under the floor for the metres
  const y1 = h + y0;
  const dir = glance ? -1 : 1; // the drawing is mirrored for the glance: the camera stands to the right
  const X = (x: number) => x * dir;

  // pointer to world
  const toWorld = (clientX: number, clientY: number) => {
    const el = svg.current;
    const m = el?.getScreenCTM();
    if (!el || !m) return null;
    const p = new DOMPoint(clientX, clientY).matrixTransform(m.inverse());
    return { x: p.x * dir, y: -p.y };
  };

  const apply = (clientX: number, clientY: number) => {
    const p = toWorld(clientX, clientY);
    if (!p) return;
    if (dragRef.current === "cam") {
      onChange({ back: clamp(-p.x, 0, 40, 0.25), rise: clamp(p.y, 0, 30, 0.25) });
    } else if (dragRef.current === "fast") {
      onChange({ backSpeed: clamp((-p.x - values.back) / REFERENCE_SPEED, 0, 1.5, 0.05), riseSpeed: clamp((p.y - values.rise) / REFERENCE_SPEED, 0, 1.5, 0.05) });
    } else if (dragRef.current === "tilt") {
      const dx = p.x + values.back;
      const dy = values.rise - p.y;
      if (Math.hypot(dx, dy) < px * 10 || dx <= 0) return;
      onChange({ tilt: clamp((Math.atan2(dy, dx) * 180) / Math.PI, -10, 80, 1) });
    }
  };

  const start = (which: Handle) => (e: ReactPointerEvent<SVGElement>) => {
    if (disabled || e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    dragRef.current = which;
    setDrag(which);
    held.current = frame;
    // the row takes the focus as hovering a row does (the press itself is kept from the browser), so
    // the sheet knows which rig is in hand: the glance's holds a glance on the preview
    root.current?.focus({ preventScroll: true });
    svg.current?.setPointerCapture(e.pointerId);
  };

  const stop = () => {
    dragRef.current = null;
    setDrag(null);
    held.current = null;
  };

  // where a camera's line of sight meets the floor, or the drawing's edge
  const sight = (v: RigValues) => {
    const cx = -v.back;
    const cy = v.rise;
    const t = (v.tilt * Math.PI) / 180;
    let x = x1;
    let y = cy - Math.tan(t) * (x1 - cx);
    if (t > 0.001) {
      const hit = cx + cy / Math.tan(t);
      if (hit < x1) {
        x = hit;
        y = 0;
      }
    }
    if (y > y1) {
      y = y1;
      x = cx + (cy - y1) / Math.tan(t);
    }
    return { cx, cy, x, y };
  };

  const lit = (part: RigPart) => focus === part || (drag === "cam" && (part === "back" || part === "rise")) || (drag === "tilt" && part === "tilt") || (drag === "fast" && part === "speed");
  const now = sight(values);
  const fastAt = fast ? sight(fast) : null;
  const showStock = !near(values, stock);
  const was = sight(stock);
  const step = tickStep(-x0);
  const ticks: number[] = [];
  for (let t = -step; t >= x0 + 18 * px; t -= step) ticks.push(t);

  // the cycle at its true size, or at least 46 px long so it reads as one
  const k = Math.max(1, (46 * px) / (CYCLE_SIDE.u[1] - CYCLE_SIDE.u[0]));
  const cycleTop = (glance ? CYCLE_REAR.v[1] : CYCLE_SIDE.v[1]) * k;
  const cycleRear = CYCLE_SIDE.u[0] * k;

  // the wedge: what the camera sees, its top and bottom edges round the line of sight
  const half = halfHeightAngle(fov);
  const ray = (deg: number) => {
    const a = (deg * Math.PI) / 180;
    return `${now.cx + Math.cos(a) * 400},${now.cy - Math.sin(a) * 400}`;
  };

  const fontPx = bw > 480 ? 12.5 : 11.5;

  // the tilt: its arc at the camera and the knob along the line of sight, in screen space
  const t = (values.tilt * Math.PI) / 180;
  // pixels from a point to the cycle: the knob and the angle's figure keep clear of it
  const cu = glance ? CYCLE_REAR.u : CYCLE_SIDE.u;
  const clear = (x: number, y: number) => Math.hypot(Math.max(cu[0] * k - x, 0, x - cu[1] * k), Math.max(-y, 0, y - cycleTop)) / px;
  let r = KNOB;
  while (r > 22 && clear(now.cx + Math.cos(t) * r * px, now.cy - Math.sin(t) * r * px) < 11) r -= 2;
  const knob = { x: now.cx + Math.cos(t) * r * px, y: now.cy - Math.sin(t) * r * px };
  const R = r * px;
  const sx = X(now.cx);
  const sy = -now.cy;
  const arcEnd = { x: sx + dir * R * Math.cos(t), y: sy + R * Math.sin(t) };
  const arc = `M${sx + dir * R} ${sy}A${R} ${R} 0 0 ${dir * t > 0 ? 1 : 0} ${arcEnd.x} ${arcEnd.y}`;
  // inside the angle past the arc, or over the camera when the cycle is there
  const inside = { x: now.cx + (R + 14 * px) * Math.cos(t / 2), y: now.cy - (R + 14 * px) * Math.sin(t / 2) };
  const tiltLabel: { x: number; y: number; anchor: "start" | "end" | "middle" } =
    clear(inside.x, inside.y) > 16
      ? { x: X(inside.x), y: -inside.y + (fontPx / 2.6) * px, anchor: glance ? "end" : "start" }
      : { x: sx, y: sy - 17 * px, anchor: "middle" };
  const metres = (v: number) => trimRig(v) + " m";
  const say = glance ? tr("settings.rig.fromBehind") : tr("settings.rig.fromSide");

  const hit = (which: Handle, x: number, y: number, size: number, label: string) => (
    <rect className="st-rig-hit" data-handle={which} x={x - (size / 2) * px} y={y - (size / 2) * px} width={size * px} height={size * px} onPointerDown={start(which)} aria-label={label} />
  );

  return (
    // a stop for the sheet's focus (hover, Up / Down) like any row's control; its rows below take the keys
    <div
      ref={root}
      className={"st-rig" + (glance ? " is-glance" : "") + (drag ? " is-dragging" : "") + (disabled ? " is-off" : "")}
      id={"st-" + id + "-ctl"}
      role="group"
      aria-labelledby={"st-" + id + "-key"}
      tabIndex={-1}
      data-nav=""
    >
      <div className="st-rig-stage">
        {glance && <GlancePlan keys={keys} />}
        <svg
          ref={svg}
          className="st-rig-svg"
          viewBox={`${glance ? -x1 : x0} ${-y1} ${w} ${h}`}
          preserveAspectRatio="xMidYMid meet"
          role="img"
          aria-labelledby={titleId}
          onPointerMove={(e) => dragRef.current && apply(e.clientX, e.clientY)}
          onPointerUp={stop}
          onPointerCancel={stop}
        >
          <title id={titleId}>{tr(glance ? "settings.rig.title.glance" : "settings.rig.title.custom", { back: values.back, rise: values.rise, tilt: values.tilt }) + (fast ? tr("settings.rig.title.fast", { back: trimRig(fast.back), rise: trimRig(fast.rise) }) : "")}</title>
          <defs>
            <clipPath id={uid + "-room"}>
              <rect x={x0} y={0} width={w} height={y1} />
            </clipPath>
            <linearGradient id={uid + "-trail"} gradientUnits="userSpaceOnUse" x1={x0} y1={0} x2={cycleRear} y2={0}>
              <stop offset="0" stopColor="var(--player)" stopOpacity="0" />
              <stop offset="1" stopColor="var(--player)" stopOpacity="0.9" />
            </linearGradient>
          </defs>
          <g transform={glance ? "scale(-1 -1)" : "scale(1 -1)"}>
            {/* what the camera sees, down to the floor */}
            <polygon className={"st-rig-view" + (lit("tilt") ? " is-lit" : "")} clipPath={`url(#${uid}-room)`} points={`${now.cx},${now.cy} ${ray(values.tilt - half)} ${ray(values.tilt + half)}`} />
            {/* the floor and its metres */}
            <line className="st-rig-floor" x1={x0} y1={0} x2={x1} y2={0} />
            {ticks.map((tk) => (
              <line key={tk} className="st-rig-tick" x1={tk} y1={0} x2={tk} y2={-5 * px} />
            ))}
            {/* the cycle's wall behind it: it drives away from it */}
            {!glance && <rect x={x0} y={0} width={Math.max(0, cycleRear - x0)} height={3 * px} fill={`url(#${uid}-trail)`} />}
            {/* stock: a dashed ghost */}
            {showStock && (
              <>
                <line className="st-rig-ghost" x1={was.cx} y1={was.cy} x2={was.x} y2={was.y} />
                <CamGlyph className="st-rig-ghost" x={was.cx} y={was.cy} tilt={stock.tilt} px={px} />
              </>
            )}
            {/* the measurements: up from the floor, back from the cycle at the camera's height */}
            {now.cy > 0.4 && <line className={"st-rig-dim" + (lit("rise") ? " is-lit" : "")} x1={now.cx} y1={0} x2={now.cx} y2={now.cy} />}
            {now.cx < -0.4 && (
              <>
                <line className={"st-rig-dim" + (lit("back") ? " is-lit" : "")} x1={now.cx} y1={now.cy} x2={0} y2={now.cy} />
                {now.cy > cycleTop && <line className="st-rig-guide" x1={0} y1={now.cy} x2={0} y2={cycleTop} />}
              </>
            )}
            {/* at speed: where the camera pulls back to, joined to the standing one */}
            {fastAt && fast && (
              <>
                <line className="st-rig-travel" x1={now.cx} y1={now.cy} x2={fastAt.cx} y2={fastAt.cy} />
                <line className={"st-rig-fast-sight" + (lit("speed") ? " is-lit" : "")} x1={fastAt.cx} y1={fastAt.cy} x2={fastAt.x} y2={fastAt.y} />
              </>
            )}
            {/* the line of sight */}
            <line className={"st-rig-sight" + (lit("tilt") ? " is-lit" : "")} x1={now.cx} y1={now.cy} x2={now.x} y2={now.y} />
            {/* the cycle, the game's own model */}
            <g transform={`scale(${k})`}>
              {glance ? (
                <>
                  <path className="st-rig-cycle-body" d={CYCLE_REAR.body} />
                  <rect className="st-rig-cycle-tyre" x={CYCLE_REAR.tyre.u[0]} y={CYCLE_REAR.tyre.v[0]} width={CYCLE_REAR.tyre.u[1] - CYCLE_REAR.tyre.u[0]} height={CYCLE_REAR.tyre.v[1] - CYCLE_REAR.tyre.v[0]} />
                </>
              ) : (
                <>
                  <path className="st-rig-cycle-body" d={CYCLE_SIDE.body} />
                  {CYCLE_SIDE.wheels.map((wh) => (
                    <g key={wh.cx}>
                      <circle className="st-rig-cycle-wheel" cx={wh.cx} cy={wh.cy} r={wh.r} />
                      <circle className="st-rig-cycle-hub" cx={wh.cx} cy={wh.cy} r={wh.r * 0.3} />
                    </g>
                  ))}
                </>
              )}
            </g>
            {/* the handles: the camera at speed, the tilt knob, the camera */}
            {fastAt && fast && (
              <>
                <CamGlyph className={"st-rig-fast" + (lit("speed") ? " is-lit" : "")} x={fastAt.cx} y={fastAt.cy} tilt={fast.tilt} px={px} />
                {hit("fast", fastAt.cx, fastAt.cy, 40, tr("settings.rig.drag.fast"))}
              </>
            )}
            <rect className={"st-rig-knob" + (lit("tilt") ? " is-lit" : "")} x={knob.x - 5 * px} y={knob.y - 5 * px} width={10 * px} height={10 * px} />
            {hit("tilt", knob.x, knob.y, 28, tr("settings.rig.drag.tilt"))}
            <CamGlyph className={"st-rig-cam" + (lit("back") || lit("rise") ? " is-lit" : "")} x={now.cx} y={now.cy} tilt={values.tilt} px={px} />
            {hit("cam", now.cx, now.cy, 40, tr("settings.rig.drag.cam"))}
          </g>
          <g className="st-rig-labels" style={{ fontSize: fontPx * px + "px" }}>
            <text className="st-rig-say" x={(glance ? -x1 : x0) + 2 * px} y={-y1 + (fontPx + 4) * px}>
              {say}
            </text>
            {now.cy > 0.4 && (
              <text className={"st-rig-num" + (lit("rise") ? " is-lit" : "")} x={sx - dir * 9 * px} y={sy / 2 + (fontPx / 3) * px} textAnchor={glance ? "start" : "end"}>
                {metres(values.rise)}
              </text>
            )}
            {now.cx < -1 && (
              <text className={"st-rig-num" + (lit("back") ? " is-lit" : "")} x={X(now.cx / 2)} y={sy - 7 * px} textAnchor="middle">
                {metres(values.back)}
              </text>
            )}
            {Math.abs(values.tilt) >= 1 && (
              <>
                <path className={"st-rig-arc" + (lit("tilt") ? " is-lit" : "")} d={arc} />
                <text className={"st-rig-num" + (lit("tilt") ? " is-lit" : "")} x={tiltLabel.x} y={tiltLabel.y} textAnchor={tiltLabel.anchor}>
                  {num(values.tilt) + "°"}
                </text>
              </>
            )}
            {fastAt && (
              <text className={"st-rig-fast-label" + (lit("speed") ? " is-lit" : "")} x={X(fastAt.cx)} y={-fastAt.cy - 15 * px} textAnchor="middle">
                {tr("settings.rig.atSpeed")}
              </text>
            )}
            {ticks.map((tk) => (
              <text key={tk} x={X(tk)} y={(fontPx + 9) * px} textAnchor="middle">
                {num(-tk) + " m"}
              </text>
            ))}
          </g>
        </svg>
      </div>
      <p className="st-rig-legend" aria-hidden="true">
        <span className="st-rig-key">
          <LegendGlyph kind="cam" />
          {tr("settings.rig.standing")}
        </span>
        {fast && (
          <span className="st-rig-key">
            <LegendGlyph kind="fast" />
            {tr("settings.rig.atSpeed")}
          </span>
        )}
        <span className="st-rig-key">
          <LegendGlyph kind="tilt" />
          {tr("settings.rig.tilt")}
        </span>
        {showStock && (
          <span className="st-rig-key">
            <LegendGlyph kind="stock" />
            {tr("settings.rig.stock")}
          </span>
        )}
        <span className="st-rig-do">{tr("settings.rig.drag")}</span>
      </p>
    </div>
  );
}

function LegendGlyph({ kind }: { kind: "cam" | "fast" | "tilt" | "stock" }) {
  return (
    <svg className="st-rig-glyph" viewBox="-12 -12 24 24" aria-hidden="true">
      {kind === "tilt" ? (
        <>
          <line className="st-rig-sight" x1={-11} y1={-6} x2={11} y2={6} />
          <rect className="st-rig-knob" x={-4} y={-4} width={8} height={8} />
        </>
      ) : (
        <g className={kind === "cam" ? "st-rig-cam" : kind === "fast" ? "st-rig-fast" : "st-rig-ghost"} transform="translate(1 0) scale(0.95)">
          <CamShape />
        </g>
      )}
    </svg>
  );
}

// the glance from above, beside the view from behind: the cycle heading up the plan with its wall
// behind it; the camera's place while driving (dashed) swings round to the side and looks across
// the cycle. The left glance, as the view beside it draws it; the keys of all three under it
function GlancePlan({ keys }: { keys?: GlanceKeys }) {
  const s = 26; // pixels a metre: a plan, not to scale
  const r = 42;
  const mid = (CYCLE_TOP.u[0] + CYCLE_TOP.u[1]) / 2;
  const named = [
    { key: keys?.left, say: tr("settings.rig.left") },
    { key: keys?.right, say: tr("settings.rig.right") },
    { key: keys?.back, say: tr("settings.rig.back") }
  ].filter((x) => x.key);
  return (
    <div className="st-rig-plan" aria-hidden="true">
      <span className="st-rig-plan-say">{tr("settings.rig.fromAbove")}</span>
      <svg viewBox="-62 -26 124 92">
        <defs>
          <linearGradient id="st-rig-plan-trail" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="var(--player)" stopOpacity="0.9" />
            <stop offset="1" stopColor="var(--player)" stopOpacity="0" />
          </linearGradient>
          <marker id="st-rig-plan-head" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path className="st-rig-head" d="M0 0L8 4L0 8Z" />
          </marker>
        </defs>
        {/* the wall, the look across, the cycle */}
        <rect x={-1.5} y={s * (mid - CYCLE_TOP.u[0])} width={3} height={42} fill="url(#st-rig-plan-trail)" />
        <line className="st-rig-look" x1={r - 14} y1={0} x2={-r - 6} y2={0} markerEnd="url(#st-rig-plan-head)" />
        <path className="st-rig-cycle-body" d={CYCLE_TOP.body} transform={`matrix(0 ${-s} ${-s} 0 0 ${mid * s})`} />
        {/* driving: behind it; the swing to the side */}
        <g className="st-rig-ghost" transform={`translate(0 ${r}) rotate(-90)`}>
          <CamShape />
        </g>
        <path className="st-rig-swing" d={`M${r * Math.cos(1.35)} ${r * Math.sin(1.35)}A${r} ${r} 0 0 0 ${r * Math.cos(0.32)} ${r * Math.sin(0.32)}`} markerEnd="url(#st-rig-plan-head)" />
        {/* the glance: at the side, looking across */}
        <g className="st-rig-cam" transform={`translate(${r} 0) rotate(180)`}>
          <CamShape />
        </g>
        {keys?.left && (
          <g className="st-rig-cap" transform={`translate(${-r - 4} -14)`}>
            <rect x={-Math.max(8, keys.left.length * 3.5 + 4)} y={-8} width={Math.max(16, keys.left.length * 7 + 8)} height={15} />
            <text x={0} y={3.5} textAnchor="middle">
              {keys.left}
            </text>
          </g>
        )}
      </svg>
      {named.length > 0 && (
        <span className="st-rig-plan-keys">
          {named.map((x) => (
            <span key={x.say}>
              <kbd>{x.key}</kbd> {x.say}
            </span>
          ))}
        </span>
      )}
    </div>
  );
}

function trimRig(n: number): string {
  return num(Math.round(n * 10) / 10 + 0);
}

function clamp(v: number, min: number, max: number, step: number): number {
  return roundTo(Math.max(min, Math.min(max, v)), step);
}
