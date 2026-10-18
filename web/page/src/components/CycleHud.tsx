// The page's HUD (the HUD style MODERN, settings/hudStyle.ts; compact.ts useGameHud turns the game's
// gauges off): the rail and the danger edge. Rubber first, as what is LEFT: the
// server's CYCLE_RUBBER in cells (rubberCells: one a unit up to ten, then bigger cells with ticks
// inside, like a health bar) and the units left in figures, full and green at the start, draining as it is used, amber from
// half, red near the end (a blend between the state colours where the browser mixes colours, the
// tiers otherwise); the brakes under it (bright while braking, amber and EMPTY when nearly out), the
// speed last and quietest; from half the rubber used on, the screen's edges warm, amber and then red,
// so a player grinding a wall sees it without looking down. The brake bar only
// where there is a brake to press: not with touch buttons that have no brake key, nor in Discord on a
// phone; the speed stays. Read from
// aa.getCycle() (the cycle the camera follows) on animation frames, drawn at most every 50 ms;
// nothing while that cycle is dead. Takes no pointer, no focus.
import { useEffect, useState, type CSSProperties } from "react";
import type { Aa, AaCycle } from "../aa/types";
import { num, t } from "../i18n";

const DRAW_MS = 50;

function clamp01(v: number): number {
  return Number.isFinite(v) ? Math.max(0, Math.min(1, v)) : 0;
}

// the rubber's colour for the share left: green at full, amber at half, red at none
function meterMix(left: number): string {
  if (left >= 0.5) return `color-mix(in oklab, var(--meter-ok) ${Math.round(((left - 0.5) / 0.5) * 100)}%, var(--meter-warn))`;
  return `color-mix(in oklab, var(--meter-warn) ${Math.round((left / 0.5) * 100)}%, var(--meter-danger))`;
}

// the rail's cells for a server's CYCLE_RUBBER: one cell a unit up to ten (5
// rubber, 5 cells); above ten, cells of 5, 10, 25, 50... units, the smallest step that keeps them at
// ten or fewer, with a tick inside every unit (every fifth of the cell above 10-unit cells). A
// fractional CYCLE_RUBBER ends on a narrower cell. Each cell: its start and size in units
export interface RubberCells {
  cells: { start: number; size: number }[];
  minor: number; // units between the ticks inside a cell, 0 for none
}

const STEPS = [1, 5, 10, 25, 50, 100, 250, 500, 1000];

export function rubberCells(max: number): RubberCells {
  if (!Number.isFinite(max) || max <= 0) return { cells: Array.from({ length: 10 }, (_, i) => ({ start: i / 10, size: 0.1 })), minor: 0 };
  const step = STEPS.find((s) => Math.ceil(max / s - 1e-6) <= 10) ?? Math.ceil(max / 10);
  const cells: { start: number; size: number }[] = [];
  for (let start = 0; start < max - 1e-6; start += step) cells.push({ start, size: Math.min(step, max - start) });
  return { cells, minor: step === 1 ? 0 : step <= 10 ? 1 : step / 5 };
}

// a unit count as the rail says it: whole, else one decimal
function units(v: number): string {
  return Math.abs(v - Math.round(v)) < 0.05 ? num(Math.round(v)) : num(v, { minimumFractionDigits: 1, maximumFractionDigits: 1 });
}

export function CycleHud({ aa, brakes = true }: { aa: Aa; brakes?: boolean }) {
  const [cycle, setCycle] = useState<AaCycle | null>(null);

  useEffect(() => {
    let frame = 0;
    let last = -DRAW_MS;
    const tick = (t: number) => {
      frame = requestAnimationFrame(tick);
      if (t - last < DRAW_MS) return;
      last = t;
      try {
        setCycle(aa.getCycle ? aa.getCycle() : null);
      } catch {
        setCycle(null);
      }
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [aa]);

  if (!cycle || !cycle.alive) return null;

  const used = cycle.rubberMax > 0 ? clamp01(cycle.rubber / cycle.rubberMax) : 0;
  const left = 1 - used;
  const tier = used >= 0.8 ? "danger" : used >= 0.5 ? "warn" : "ok";
  const max = cycle.rubberMax > 0 ? cycle.rubberMax : 0;
  const leftUnits = left * max;
  const { cells, minor } = rubberCells(max);
  const brake = clamp01(cycle.brake);
  const brakeState = cycle.braking ? "on" : brake < 0.15 ? "low" : "";
  // the edges: nothing under half used, then warming to the full glow at the end
  const heat = Math.max(0, (used - 0.5) / 0.5);
  const glow = tier === "danger" ? "255, 90, 95" : "255, 181, 71";

  return (
    <div className="cyclehud" id="cycle-hud" data-tier={tier} data-brake={brakeState} data-brakes={brakes ? undefined : "none"} aria-hidden="true"
      style={{ "--meter-mix": meterMix(left) } as CSSProperties}>
      {heat > 0 && (
        <div
          className="hud-edge"
          style={{ boxShadow: `inset 0 0 ${Math.round(40 + 60 * heat)}px ${Math.round(4 + 20 * heat)}px rgba(${glow}, ${(0.15 + 0.4 * heat).toFixed(2)})` }}
        />
      )}
      <div className="hud-rail">
        <div className="hud-row">
          <span className="hud-key">{t("game.hud.rubber")}</span>
          <span className="hud-pct" id="hud-rubber">
            {max > 0 ? units(leftUnits) : Math.round(left * 100)}
            <small>{max > 0 ? "/ " + units(max) : "%"}</small>
          </span>
        </div>
        <div className="hud-cells" data-cells={cells.length}>
          {cells.map((c, i) => {
            const ticks = minor > 0 ? Math.ceil(c.size / minor - 1e-6) - 1 : 0;
            return (
              <i key={i} style={{ flexGrow: c.size }}>
                <b style={{ width: (clamp01(max > 0 ? (leftUnits - c.start) / c.size : left * 10 - i) * 100).toFixed(1) + "%" }} />
                {Array.from({ length: ticks }, (_, t) => (
                  <u key={t} style={{ left: ((((t + 1) * minor) / c.size) * 100).toFixed(2) + "%" }} />
                ))}
              </i>
            );
          })}
        </div>
        <div className="hud-row hud-brake">
          <span className="hud-key">{brakeState === "on" ? t("game.hud.braking") : brakeState === "low" ? t("game.hud.empty") : t("game.hud.brake")}</span>
          <span className="hud-bar">
            <b style={{ width: (brake * 100).toFixed(1) + "%" }} />
          </span>
          <span className="hud-speed" id="hud-speed">
            {Math.round(cycle.speed)}
            <small>{t("game.hud.speedUnit")}</small>
          </span>
        </div>
      </div>
    </div>
  );
}
