// The summary's lap (Rewards.tsx): a closed square round its one action, a short wall
// running round it. The cyan trail round a focused lane is the home menu's alone
// (Strip.tsx); the in-game menu and the Profile mark the focus with a solid lane and a
// short ink bar instead (first-minute.css, profile.css).
import { useLayoutEffect, useState, type CSSProperties } from "react";

function unit(): number {
  // --u (faces.css): 1px at 1536 wide, clamped to 0.8-1.25px
  return Math.min(1.25, Math.max(0.8, window.innerWidth * 0.000651));
}

// a closed lap round an element, a short wall running round it for as long as it shows;
// with reduced motion the wall stands still along the top and the far side
export function LapTrail({ around }: { around: HTMLElement | null }) {
  const [box, setBox] = useState<{ w: number; h: number } | null>(null);
  useLayoutEffect(() => {
    if (!around) return;
    const measure = () => setBox({ w: around.offsetWidth, h: around.offsetHeight });
    measure();
    const watch = new ResizeObserver(measure);
    watch.observe(around);
    return () => watch.disconnect();
  }, [around]);
  if (!box) return null;
  const gap = Math.round(6 * unit());
  const w = box.w + 2 * gap;
  const h = box.h + 2 * gap;
  const len = 2 * (w + h);
  const d = `M0 0 H${w} V${h} H0 Z`;
  const style = { "--len": len, "--wall": Math.round(len * 0.42) } as CSSProperties;
  return (
    <svg className="lap" aria-hidden="true" style={{ left: -gap, top: -gap, width: w, height: h }} viewBox={`0 0 ${w} ${h}`}>
      <path className="lap-path" d={d} style={style} />
      <path className="lap-path lap-core" d={d} style={style} />
    </svg>
  );
}
