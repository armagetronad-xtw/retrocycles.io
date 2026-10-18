// The game's colour codes in what players type (a name, an instant chat: "0xabab12Holing!!"):
// the text after a 0xRRGGBB code takes that colour, 0xRESETT goes back to the default, as the
// game draws it. A text input cannot colour part of its text, so the input's own text is
// transparent (its caret and selection stay) and a mirror behind it, in the same face, size
// and padding, draws the runs: the codes themselves quiet, so they can still be seen and
// edited, the text after them in their colour. A near-black colour is lifted toward ink just
// enough to be read on the page's dark wells, as the chat overlay does.
import { forwardRef, useEffect, useImperativeHandle, useRef, type InputHTMLAttributes } from "react";

// a fresh expression per use: a shared global one carries lastIndex from one call into the next
// (test() leaves it after the first match, and matchAll starts from it), which skipped the first code
const CODE_SOURCE = "0x(?:[0-9a-fA-F]{6}|RESETT)";

export interface ColorRun {
  text: string;
  color: string | null; // null: the default ink
  code: boolean; // the code itself
}

// the text in runs, the codes kept (for a mirror the caret lines up with)
export function colorRuns(text: string): ColorRun[] {
  const runs: ColorRun[] = [];
  let color: string | null = null;
  let at = 0;
  for (const m of text.matchAll(new RegExp(CODE_SOURCE, "g"))) {
    const i = m.index ?? 0;
    if (i > at) runs.push({ text: text.slice(at, i), color, code: false });
    runs.push({ text: m[0], color: null, code: true });
    color = /RESETT$/i.test(m[0]) ? null : "#" + m[0].slice(2).toLowerCase();
    at = i + m[0].length;
  }
  if (at < text.length) runs.push({ text: text.slice(at), color, code: false });
  return runs;
}

export function hasColorCodes(text: string): boolean {
  return new RegExp(CODE_SOURCE).test(text);
}

// a near-black colour lifted toward ink just enough to read on a dark ground
const READ_LUMA = 96;
export function readable(hex: string): string {
  const n = parseInt(hex.slice(1), 16);
  const c = [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  const luma = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  if (luma >= READ_LUMA) return hex;
  const ink = [242, 244, 247];
  const t = (READ_LUMA - luma) / (0.2126 * ink[0] + 0.7152 * ink[1] + 0.0722 * ink[2] - luma);
  const mixed = c.map((v, i) => Math.round(v + (ink[i] - v) * t));
  return `rgb(${mixed[0]}, ${mixed[1]}, ${mixed[2]})`;
}

// the text in its colours, the codes left out (a label, the chip)
export function ColorText({ text }: { text: string }) {
  return (
    <>
      {colorRuns(text)
        .filter((r) => !r.code)
        .map((r, i) => (
          <span key={i} style={r.color ? { color: readable(r.color) } : undefined}>
            {r.text}
          </span>
        ))}
    </>
  );
}

interface ColorInputProps extends InputHTMLAttributes<HTMLInputElement> {
  value: string;
  wrapClassName?: string;
}

// an input whose text shows its colour codes; without a code it is a plain input
export const ColorInput = forwardRef<HTMLInputElement, ColorInputProps>(function ColorInput({ value, className, wrapClassName, ...rest }, ref) {
  const input = useRef<HTMLInputElement>(null);
  const mirror = useRef<HTMLSpanElement>(null);
  useImperativeHandle(ref, () => input.current as HTMLInputElement);
  const coloured = hasColorCodes(value);

  // the mirror scrolls with the input (a long text in a narrow well)
  useEffect(() => {
    const el = input.current;
    if (!el || !coloured) return;
    const sync = () => {
      if (mirror.current) mirror.current.scrollLeft = el.scrollLeft;
    };
    sync();
    const events = ["scroll", "input", "keyup", "select", "focus", "pointerup"];
    for (const ev of events) el.addEventListener(ev, sync);
    return () => {
      for (const ev of events) el.removeEventListener(ev, sync);
    };
  }, [coloured, value]);

  return (
    <span className={"cin" + (coloured ? " is-coloured" : "") + (wrapClassName ? " " + wrapClassName : "")}>
      {coloured && (
        <span className={(className ? className + " " : "") + "cin-mirror"} ref={mirror} aria-hidden="true">
          {colorRuns(value).map((r, i) => (
            <span key={i} className={r.code ? "cin-code" : undefined} style={!r.code && r.color ? { color: readable(r.color) } : undefined}>
              {r.text}
            </span>
          ))}
        </span>
      )}
      <input ref={input} className={(className ? className + " " : "") + "cin-input"} value={value} {...rest} />
    </span>
  );
});
