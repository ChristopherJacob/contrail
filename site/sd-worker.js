// SD card page worker (6c spec §3): runs tools/build_map.py (copied to py/) in Pyodide, fetches only the map
// regions near home, and hands back contrail-sd-card.zip.
// In: {lat, lon}. Out: {step: "python"} | {step: "data", got, total} | {step: "build", level} (level 3: zipping)
//     | {done: true, zip, summary} | {error: "load" | "range" | "build", what}
//     ("range": build_map.RangeError, a home the map can't be built for; what is its message).
// tools/sd_node_test.mjs reads the Pyodide version from PYODIDE below and checks it matches the one it tests.
const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v0.27.7/full/";
const HOME = "/home/pyodide";

class LoadError extends Error {}

async function download(url, what) {
    let r;
    try {
        r = await fetch(url);
    } catch {
        throw new LoadError(what);
    }
    if (!r.ok) throw new LoadError(what);
    return new Uint8Array(await r.arrayBuffer());
}

let ready = null;  // {py, sd}: Pyodide with the builder imported, once per page

async function start() {
    try {
        importScripts(PYODIDE + "pyodide.js");
    } catch {
        throw new LoadError("Python (Pyodide)");
    }
    const py = await loadPyodide({ indexURL: PYODIDE }).catch(() => { throw new LoadError("Python (Pyodide)"); });
    for (const f of ["build_map.py", "gen_map.py", "sd_card.py"]) {
        py.FS.writeFile(`${HOME}/${f}`, await download(`py/${f}`, "the map builder"));
    }
    for (const d of ["/regions", "/regions/cells", "/regions/shared"]) py.FS.mkdir(d);
    py.FS.writeFile("/regions/index.json", await download("mapdata/index.json", "the map data"));
    return { py, sd: py.pyimport("sd_card") };
}

const have = (py, path) => py.FS.analyzePath(path).exists;
const lastLine = (e) => String(e?.message || e || "unknown error").trim().split("\n").pop() || "unknown error";

onmessage = async ({ data: { lat, lon } }) => {
    try {
        if (!ready) postMessage({ step: "python" });
        ready ??= start();
        const { py, sd } = await ready.catch((e) => { ready = null; throw e; });

        // Region files stay in /regions after a build: a cache for the next build near the same home.
        const filesProxy = sd.region_files("/regions", lat, lon);
        const files = filesProxy.toJs();
        filesProxy.destroy();
        const total = files.reduce((n, [, bytes]) => n + bytes, 0);
        let got = 0;
        postMessage({ step: "data", got, total });
        for (const [path, bytes] of files) {
            if (!have(py, `/regions/${path}`)) {
                py.FS.writeFile(`/regions/${path}`, await download(`mapdata/${path}`, "the map data"));
            }
            got += bytes;
            postMessage({ step: "data", got, total });
        }

        if (!have(py, `${HOME}/logos.pak`)) {  // no release yet: the map alone
            try {
                const r = await fetch("firmware/logos.pak");
                if (r.ok) py.FS.writeFile(`${HOME}/logos.pak`, new Uint8Array(await r.arrayBuffer()));
            } catch { /* logos stay out */ }
        }
        const logos = have(py, `${HOME}/logos.pak`) ? `${HOME}/logos.pak` : null;

        postMessage({ step: "build", level: 0 });
        const made = sd.make("/regions", lat, lon, logos, `${HOME}/sd.zip`, (level) => postMessage({ step: "build", level }));
        const summary = made.toJs({ dict_converter: Object.fromEntries });
        made.destroy();
        const zip = py.FS.readFile(`${HOME}/sd.zip`);
        py.FS.unlink(`${HOME}/sd.zip`);
        postMessage({ done: true, zip, summary }, [zip.buffer]);
    } catch (e) {  // every failure posts one error, so the page never waits forever
        if (e instanceof LoadError) postMessage({ error: "load", what: e.message });
        else if (e?.type === "RangeError") postMessage({ error: "range", what: lastLine(e).replace(/^[\w.]*RangeError: /, "") });
        else postMessage({ error: "build", what: lastLine(e) });
    }
};
