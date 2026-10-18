// The home menu's trail (Strip.tsx): a lightcycle wall that stands round three sides of the
// focused row, open on the left. When the focus moves it runs to the new row the way a cycle
// would have driven it: from its head at the old row's left edge, down or up the side line
// all the rows share, round the new row's three sides, its tail pulling off the old row
// behind it, and it stops there. A row below is entered at its bottom-left corner (bottom,
// far side, top), a row above at its top-left (top, far side, bottom), so the wall always
// arrives the way it was going and rests with its head on the side line, ready for the next
// move. Only 90-degree turns, never across a row, no glow and no cycle drawn: the wall is the
// whole device. When the menu shows, it comes in from the screen edge along the row's top.
// Distances are px of the menu; sizes scale with --u.

export interface Loop {
  l: number;
  t: number;
  r: number;
  b: number;
}

interface Pt {
  x: number;
  y: number;
}

// the wall's distance outside the row
export const LOOP_GAP = 8;
// how much of the wall's tail fades out (u), so it reads as a wall's cut end, not a frame
const FADE = 70;
const EPS = 0.5;

// a row's three sides, entered from above (at the bottom-left) or from below (top-left)
const fromAbove = (L: Loop): Pt[] => [
  { x: L.l, y: L.b },
  { x: L.r, y: L.b },
  { x: L.r, y: L.t },
  { x: L.l, y: L.t }
];
const fromBelow = (L: Loop): Pt[] => [
  { x: L.l, y: L.t },
  { x: L.r, y: L.t },
  { x: L.r, y: L.b },
  { x: L.l, y: L.b }
];
const sidesLength = (L: Loop) => 2 * (L.r - L.l) + (L.b - L.t);
const dist = (a: Pt, b: Pt) => Math.abs(a.x - b.x) + Math.abs(a.y - b.y);
// fast out, settling into the corner
const ease = (k: number) => 1 - (1 - k) ** 4;

export class MenuWall {
  private pts: Pt[] = [];
  private cum: number[] = [];
  private s = 0; // the head's distance along the path
  private w = 0; // the wall's length
  private from = 0;
  private to = 0;
  private wFrom = 0;
  private wTo = 0;
  private t0 = 0;
  private dur = 0;
  private target: Loop | null = null;
  moving = false;

  constructor(private u: number) {}

  setUnit(u: number) {
    this.u = u;
  }

  // standing round the row, no motion
  rest(L: Loop) {
    this.target = L;
    this.setPath(fromBelow(L));
    this.s = this.total();
    this.w = sidesLength(L);
    this.moving = false;
  }

  // in from the screen edge along the row's top, round it, and stop
  enter(L: Loop, now: number) {
    this.target = L;
    this.setPath([{ x: 0, y: L.t }, ...fromBelow(L).slice(1)]);
    this.s = 0;
    this.w = this.wFrom = this.wTo = sidesLength(L);
    this.run(now, 420);
  }

  // a new focus: on from where the head is to the new row
  retarget(L: Loop, now: number) {
    const was = this.target;
    if (!was || !this.pts.length) {
      this.rest(L);
      return;
    }
    if (was.t === L.t && was.b === L.b) return;
    this.target = L;
    this.compact();
    const head = this.at(this.s);
    const i = this.segmentAt(this.s);
    const a = this.pts[i];
    const b = this.pts[i + 1] ?? a;
    const vertical = Math.abs(a.x - b.x) < EPS && Math.abs(a.y - b.y) > EPS;
    const down = b.y > a.y;
    // running down or up the side line toward the new row: straight on from where it is;
    // anywhere else it finishes the way it was going, then sets off from that row's corner
    const straightOn = vertical && Math.abs(head.x - L.l) < EPS && (down ? L.b > head.y + EPS : L.t < head.y - EPS);
    const lead = straightOn ? [...this.pts.slice(0, i + 1), head] : this.pts;
    const end = lead[lead.length - 1];
    const below = straightOn ? down : (L.t + L.b) / 2 > end.y;
    this.setPath([...lead, ...(below ? fromAbove(L) : fromBelow(L))]);
    this.wFrom = this.w;
    this.wTo = sidesLength(L);
    // the time grows with the way left to go, so a far row is not a faster run
    const way = (this.total() - this.s) / this.u;
    this.run(now, Math.min(460, Math.max(240, 150 + way * 0.3)));
  }

  // the frame at time now; false once the wall stands still
  update(now: number): boolean {
    if (!this.moving) return false;
    const k = Math.min(1, (now - this.t0) / this.dur);
    const e = ease(k);
    this.s = this.from + (this.to - this.from) * e;
    this.w = this.wFrom + (this.wTo - this.wFrom) * e;
    if (k >= 1) {
      this.moving = false;
      this.compact();
    }
    return this.moving;
  }

  draw(ctx: CanvasRenderingContext2D) {
    if (this.pts.length < 2) return;
    const u = this.u;
    const tail = Math.max(0, this.s - this.w);
    const fade = Math.min(FADE * u, this.w * 0.5);
    const solidFrom = Math.min(this.s, tail + fade);
    // one flat wall in the trail colour: no lit core, no glow
    const layers: Array<[number, string, number]> = [[4 * u, "47, 211, 255", 1]];
    for (const [width, rgb, peak] of layers) {
      ctx.lineWidth = width;
      // the cut end: the tail's first stretch, each piece fading toward the end
      ctx.lineCap = "butt";
      for (const [p, q, dp, dq] of this.pieces(tail, solidFrom)) {
        const g = ctx.createLinearGradient(p.x, p.y, q.x, q.y);
        g.addColorStop(0, `rgba(${rgb}, ${(peak * (dp - tail)) / fade})`);
        g.addColorStop(1, `rgba(${rgb}, ${(peak * (dq - tail)) / fade})`);
        ctx.strokeStyle = g;
        ctx.beginPath();
        ctx.moveTo(p.x, p.y);
        ctx.lineTo(q.x, q.y);
        ctx.stroke();
      }
      // the rest as one line with mitred corners
      if (this.s - solidFrom > 0.01) {
        ctx.strokeStyle = `rgba(${rgb}, ${peak})`;
        ctx.lineJoin = "miter";
        ctx.beginPath();
        const start = this.at(solidFrom);
        ctx.moveTo(start.x, start.y);
        for (let i = this.segmentAt(solidFrom) + 1; i < this.pts.length && this.cum[i] < this.s; i++) {
          ctx.lineTo(this.pts[i].x, this.pts[i].y);
        }
        const end = this.at(this.s);
        ctx.lineTo(end.x, end.y);
        ctx.stroke();
      }
    }
  }

  private run(now: number, ms: number) {
    this.from = this.s;
    this.to = this.total();
    this.t0 = now;
    this.dur = ms;
    this.moving = true;
  }

  private setPath(pts: Pt[]) {
    // drop repeated points, so every piece has a length and a heading
    const clean: Pt[] = [];
    for (const p of pts) if (!clean.length || dist(clean[clean.length - 1], p) > EPS) clean.push(p);
    this.pts = clean;
    this.cum = [0];
    for (let i = 1; i < clean.length; i++) this.cum.push(this.cum[i - 1] + dist(clean[i - 1], clean[i]));
  }

  private total() {
    return this.cum[this.cum.length - 1] ?? 0;
  }

  private segmentAt(d: number): number {
    for (let i = 0; i < this.pts.length - 1; i++) if (d <= this.cum[i + 1] + 1e-6) return i;
    return Math.max(0, this.pts.length - 2);
  }

  private at(d: number): Pt {
    const i = this.segmentAt(d);
    const a = this.pts[i];
    const b = this.pts[i + 1] ?? a;
    const len = this.cum[i + 1] - this.cum[i] || 1;
    const k = Math.max(0, Math.min(1, (d - this.cum[i]) / len));
    return { x: a.x + (b.x - a.x) * k, y: a.y + (b.y - a.y) * k };
  }

  // the path's pieces between two distances, with their distances
  private pieces(d0: number, d1: number): Array<[Pt, Pt, number, number]> {
    const out: Array<[Pt, Pt, number, number]> = [];
    if (d1 - d0 < 0.01) return out;
    for (let i = this.segmentAt(d0); i < this.pts.length - 1 && this.cum[i] < d1; i++) {
      const a = Math.max(d0, this.cum[i]);
      const b = Math.min(d1, this.cum[i + 1]);
      if (b - a > 0.01) out.push([this.at(a), this.at(b), a, b]);
    }
    return out;
  }

  // forget the path behind the tail
  private compact() {
    const tail = Math.max(0, this.s - this.w);
    if (tail < 1) return;
    const i = this.segmentAt(tail);
    const head = this.s - tail;
    this.setPath([this.at(tail), ...this.pts.slice(i + 1)]);
    this.s = head;
  }
}
