// Builds KFDK's SD card in Pyodide under Node, the way site/sd-worker.js does, and checks its map.pak against
// the native build and that logos.pak is carried unchanged (6c spec §5).
//   node tools/sd_node_test.mjs <path to pyodide/pyodide.mjs> <regions folder> <native map.pak>
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const [pyodideModule, regions, nativePak] = process.argv.slice(2);
if (!nativePak) {
    console.error("usage: node tools/sd_node_test.mjs <pyodide.mjs> <regions folder> <native map.pak>");
    process.exit(2);
}
const { loadPyodide } = await import(pathToFileURL(resolve(pyodideModule)).href);
const py = await loadPyodide();
const tools = dirname(fileURLToPath(import.meta.url));

// The page loads Pyodide from the CDN URL in site/sd-worker.js; this test means something only for that version.
const workerSource = readFileSync(`${tools}/../site/sd-worker.js`, "utf8");
const pageVersion = workerSource.match(/const PYODIDE = "[^"]*\/pyodide\/v(\d+\.\d+\.\d+)\//)?.[1];
if (!pageVersion) {
    console.error("couldn't find the Pyodide version in site/sd-worker.js's PYODIDE URL");
    process.exit(1);
}
if (pageVersion !== py.version) {
    console.error(`Pyodide ${py.version} here, but site/sd-worker.js loads ${pageVersion}: test with the page's version ` +
        `(npm install pyodide@${pageVersion}) or update PYODIDE`);
    process.exit(1);
}
for (const f of ["build_map.py", "gen_map.py", "sd_card.py"]) {
    py.FS.writeFile(`/home/pyodide/${f}`, readFileSync(`${tools}/${f}`));
}
py.FS.mkdir("/regions");
py.FS.mount(py.FS.filesystems.NODEFS, { root: resolve(regions) }, "/regions");

const dummyLogos = Buffer.alloc(1000, 0x55);
py.FS.writeFile("/home/pyodide/logos.pak", dummyLogos);

const sd = py.pyimport("sd_card");
const levels = [];
const started = Date.now();
const summary = sd.make("/regions", 39.4176, -77.3743, "/home/pyodide/logos.pak", "/home/pyodide/sd.zip", (level) => levels.push(level))
    .toJs({ dict_converter: Object.fromEntries });
const seconds = (Date.now() - started) / 1000;
const names = py.runPython(`import zipfile; zipfile.ZipFile("/home/pyodide/sd.zip").namelist()`).toJs();
const built = py.runPython(`zipfile.ZipFile("/home/pyodide/sd.zip").read("contrail/map.pak")`).toJs();
const builtLogos = py.runPython(`zipfile.ZipFile("/home/pyodide/sd.zip").read("contrail/logos.pak")`).toJs();
const native = readFileSync(nativePak);

let ok = true;
if (Buffer.compare(Buffer.from(built), native) !== 0) {
    console.error(`map.pak differs: ${built.length} bytes from Pyodide, ${native.length} native`);
    ok = false;
}
if (names.join() !== "contrail/map.pak,contrail/logos.pak") {
    console.error(`zip holds ${names.join()}, expected contrail/map.pak,contrail/logos.pak`);
    ok = false;
}
if (Buffer.compare(Buffer.from(builtLogos), dummyLogos) !== 0) {
    console.error("logos.pak differs from the file given");
    ok = false;
}
if (levels.join() !== "0,1,2,3") {
    console.error(`progress was ${levels.join()}, expected 0,1,2,3`);
    ok = false;
}
console.log(`Pyodide ${py.version}: ${summary.towns} towns, ${summary.airports} airports in ${seconds.toFixed(1)} s`);
if (!ok) process.exit(1);
console.log("pyodide build matches");
