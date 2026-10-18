// frames/<ms>.png captured at a tenth of the speed -> out/f%03d.png, one lap at 30 fps
// usage: node assemble.js <dir> <lapRealMs> <startMs>
const fs = require("fs");
const path = require("path");
const [dir, lapMs, startMs] = [process.argv[2], Number(process.argv[3]), Number(process.argv[4] || 0)];
const frames = fs.readdirSync(path.join(dir, "frames")).map((f) => Number(f.replace(".png", ""))).sort((a, b) => a - b);
const t0 = frames[0] + startMs;
const out = path.join(dir, "out");
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out);
const N = 80; // 2.67 s at 30 fps
for (let k = 0; k < N; k++) {
  const want = t0 + (k * lapMs) / N;
  let best = frames[0];
  for (const f of frames) if (Math.abs(f - want) < Math.abs(best - want)) best = f;
  fs.copyFileSync(path.join(dir, "frames", String(best).padStart(6, "0") + ".png"), path.join(out, "f" + String(k).padStart(3, "0") + ".png"));
}
console.log("frames", frames.length, "span", frames[frames.length - 1] - frames[0], "ms");
