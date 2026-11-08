// The browser simulator's WebAssembly build draws every site screenshot exactly as the desktop simulator does
// (6b spec §8): the same app::renderDemo, compiled twice.
//   node tools/sim_node_test.mjs <dir with contrail.js> <desktop sim binary>
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";

const [dir, sim] = process.argv.slice(2);
if (!dir || !sim) {
  console.error("usage: node tools/sim_node_test.mjs <dir with contrail.js> <desktop sim binary>");
  process.exit(2);
}
const require = createRequire(import.meta.url);
const createContrail = require(path.resolve(dir, "contrail.js"));
const m = await createContrail();
m.timeZone = "UTC";  // the desktop's --render sets TZ=UTC0

// tools/screenshots.sh's screen:scene pairs
const pairs = "closest:busy radar:busy board:busy sky:night map:busy stats:stats alert:busy".split(" ");
const header = Buffer.from("P6\n240 320\n255\n");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "contrail-sim-"));
const shot = path.join(tmp, "desktop.ppm");
let bad = 0;
for (const theme of ["cinematic", "editorial"]) {
  for (const pair of pairs) {
    const [screen, scene] = pair.split(":");
    const ptr = m.ccall("sim_render", "number", ["string", "string", "string"], [scene, screen, theme]);
    if (!ptr) {
      console.log(`${screen} ${theme}: unknown name`);
      ++bad;
      continue;
    }
    const px = m.HEAPU16.subarray(ptr >> 1, (ptr >> 1) + 240 * 320);
    execFileSync(sim, ["--render", scene, screen, theme, shot]);
    const ppm = fs.readFileSync(shot);
    let diff = 0;
    if (!ppm.subarray(0, header.length).equals(header)) diff = -1;
    else {
      for (let i = 0; i < px.length; ++i) {
        const v = px[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;  // gfx::unpack565
        const o = header.length + 3 * i;
        if (ppm[o] !== ((r << 3) | (r >> 2)) || ppm[o + 1] !== ((g << 2) | (g >> 4)) || ppm[o + 2] !== ((b << 3) | (b >> 2))) ++diff;
      }
    }
    console.log(`${screen} ${theme}: ${diff === 0 ? "same" : diff < 0 ? "bad desktop PPM" : `${diff} pixels differ`}`);
    if (diff) ++bad;
  }
}
fs.rmSync(tmp, { recursive: true, force: true });
console.log(bad ? `${bad} of ${pairs.length * 2} differ` : `all ${pairs.length * 2} the same`);
process.exit(bad ? 1 : 0);
