// SD card page (6c spec §3): picks home (search, the browser's location, typed coordinates, or #lat=&lon= / ?lat=&lon=),
// checks the map can cover it, and runs sd-worker.js, which builds contrail-sd-card.zip with tools/build_map.py in
// Pyodide. The exact location stays in the browser: every request is for a fixed file of this site or the CDN, and
// which map region files are fetched reveals only the rough area.
const COVER_NM = 250;  // build_map.COVER
const LEVELS = 3;      // build_map.LEVELS

const $ = (id) => document.getElementById(id);
const q = $("q"), results = $("results"), lat = $("lat"), lon = $("lon"), near = $("near"), homeNote = $("home-note");
const locate = $("locate"), buildButton = $("build"), working = $("working"), bar = $("bar"), step = $("step"), failed = $("failed");
const result = $("result"), download = $("download"), summary = $("summary");

const mb = (n) => `${(n / 1e6).toFixed(1)} MB`;
let worker = null, busy = false, zipUrl = null;

if (!("Worker" in window) || typeof WebAssembly !== "object") {
    $("builder").hidden = true;
    $("unsupported").hidden = false;
}

// ---------------------------------------------------------------- home

let search = null;  // the promise of mapdata/search.json, once asked for
function loadSearch() {
    search ??= fetch("mapdata/search.json")
        .then((r) => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .catch((e) => { search = null; throw e; });
    return search;
}

function home() {
    const a = Number(lat.value), b = Number(lon.value);
    if (!lat.value.trim() || !lon.value.trim() || !Number.isFinite(a) || !Number.isFinite(b)) return null;
    return Math.abs(a) <= 90 && Math.abs(b) <= 180 ? [a, b] : null;
}

// build_map.Box and check_box: the square COVER_NM from home to each side must stay inside +-180 and +-85.
function coverable([a, b]) {
    const dlat = COVER_NM / 60, dlon = dlat / Math.cos(a * Math.PI / 180);
    return a - dlat >= -85 && a + dlat <= 85 && b - dlon >= -180 && b + dlon <= 180;
}

function canBuild() {
    const h = home();
    return !busy && h !== null && coverable(h);
}

const townLabel = (t) => [t[0], /^[A-Z]+$/.test(t[1]) ? t[1] : "", t[2]].filter(Boolean).join(", ");

// The nearest town names a suburb as readily as a city, so among the towns not much farther than the nearest
// (within 2 * d0 + 1 nm) the most populous one wins. Towns are [name, admin, country, lat, lon, population].
async function nearest([a, b]) {
    const { towns } = await loadSearch();
    const k = Math.cos(a * Math.PI / 180);
    const nm = (t) => 60 * Math.hypot(t[3] - a, (t[4] - b) * k);
    let d0 = Infinity;
    for (const t of towns) d0 = Math.min(d0, nm(t));
    let best = null, bestD = Infinity;
    for (const t of towns) {
        const d = nm(t);
        if (d <= 2 * d0 + 1 && (!best || t[5] > best[5])) best = t, bestD = d;
    }
    return best && { label: townLabel(best), nm: bestD };
}

let checked = 0;  // ignores a slow "near" answer for a home that has changed since
async function update() {
    const ticket = ++checked;
    result.hidden = true;
    if (zipUrl) URL.revokeObjectURL(zipUrl), zipUrl = null;
    failed.hidden = true;
    const h = home();
    homeNote.hidden = true;
    near.textContent = "";
    if (!h) {
        buildButton.disabled = true;
        if (lat.value.trim() || lon.value.trim()) {
            homeNote.textContent = "Latitude is -90 to 90 and longitude -180 to 180, in decimal degrees.";
            homeNote.hidden = false;
        }
        return;
    }
    if (!coverable(h)) {
        buildButton.disabled = true;
        homeNote.textContent = "The map can't be built this close to the poles or the 180° meridian: its 250 nm square " +
            "would cross latitude ±85° or longitude ±180°.";
        homeNote.hidden = false;
        return;
    }
    buildButton.disabled = !canBuild();
    try {
        const n = await nearest(h);
        if (ticket === checked && n) near.textContent = n.nm < 1 ? `In ${n.label}` : `Near ${n.label} (${Math.round(n.nm)} nm)`;
    } catch { /* the search index is a nicety; building works without it */ }
}

function setHome(a, b) {
    if (busy) return;  // a location answer that arrives mid-build waits for the next pick
    lat.value = a;
    lon.value = b;
    update();
}

for (const input of [lat, lon]) input.addEventListener("input", update);

// ---------------------------------------------------------------- search

function matches(text, { airports, towns }) {
    const s = text.trim().toLowerCase();
    if (s.length < 2) return [];
    const hits = [], seen = new Set();
    const add = (key, label, detail, a, b) => {
        if (hits.length < 8 && !seen.has(key)) seen.add(key), hits.push({ label, detail, a, b });
    };
    const airport = (x) => add(`a${x[0]}`, `${x[0]} · ${x[1]}`, [x[2], x[3]].filter(Boolean).join(", "), x[4], x[5]);
    const town = (t) => add(`t${t[0]}${t[3]}${t[4]}`, townLabel(t), "town", t[3], t[4]);
    for (const x of airports) if (x[0].toLowerCase() === s) airport(x);
    for (const x of airports) if (x[0].toLowerCase().startsWith(s)) airport(x);
    for (const t of towns) if (t[0].toLowerCase().startsWith(s)) town(t);
    for (const x of airports) if (x[1].toLowerCase().includes(s)) airport(x);
    return hits;
}

function showResults(hits) {
    results.replaceChildren(...hits.map((h) => {
        const b = Object.assign(document.createElement("button"), { type: "button" });
        b.append(h.label, Object.assign(document.createElement("small"), { textContent: h.detail }));
        b.addEventListener("click", () => {
            q.value = h.label;
            results.hidden = true;
            setHome(h.a, h.b);
            q.focus();  // the picked button is hidden now
        });
        const li = document.createElement("li");
        li.append(b);
        return li;
    }));
    results.hidden = hits.length === 0;
}

const resultButtons = () => [...results.querySelectorAll("button")];

q.addEventListener("focus", () => loadSearch().catch(() => {}));
q.addEventListener("input", async () => {
    const text = q.value;
    try {
        const index = await loadSearch();
        if (q.value === text) showResults(matches(text, index));
    } catch {
        results.replaceChildren(Object.assign(document.createElement("li"), {
            className: "muted", textContent: "Search isn't available; type the coordinates instead." }));
        results.hidden = false;
    }
});
q.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !results.hidden) resultButtons()[0]?.click();
    else if (e.key === "ArrowDown" && !results.hidden && resultButtons().length) {
        e.preventDefault();
        resultButtons()[0].focus();
    } else if (e.key === "Escape") results.hidden = true;
});
results.addEventListener("keydown", (e) => {
    const buttons = resultButtons(), i = buttons.indexOf(document.activeElement);
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
        e.preventDefault();
        if (e.key === "ArrowUp" && i <= 0) q.focus();
        else buttons[Math.min(i + (e.key === "ArrowDown" ? 1 : -1), buttons.length - 1)]?.focus();
    } else if (e.key === "Escape") {
        results.hidden = true;
        q.focus();
    }
});
for (const el of [q, results]) {
    el.addEventListener("focusout", (e) => {
        if (!q.contains(e.relatedTarget) && !results.contains(e.relatedTarget)) results.hidden = true;
    });
}

// ---------------------------------------------------------------- location

locate.addEventListener("click", () => {
    if (!navigator.geolocation) return locationUnavailable();
    navigator.geolocation.getCurrentPosition(
        (p) => setHome(p.coords.latitude.toFixed(3), p.coords.longitude.toFixed(3)),
        locationUnavailable, { timeout: 15000 });
});

function locationUnavailable() {
    homeNote.textContent = "Location isn't available; search or type coordinates instead.";
    homeNote.hidden = false;
}

// ---------------------------------------------------------------- build

function setBusy(on) {
    busy = on;
    for (const el of [q, lat, lon, locate]) el.disabled = on;
    working.hidden = !on;
    buildButton.disabled = !canBuild();
}

function show(fraction, text) {
    bar.value = fraction;
    step.textContent = text;
}

function onMessage({ data: m }) {
    if (m.step === "python") show(0.03, "Loading Python (about 8 MB the first time)…");
    else if (m.step === "data") show(0.05 + 0.25 * (m.total ? m.got / m.total : 1), `Downloading map data (${mb(m.got)} of ${mb(m.total)})…`);
    else if (m.step === "build" && m.level < LEVELS) show(0.3 + 0.65 * m.level / LEVELS, `Building level ${m.level + 1} of ${LEVELS}…`);
    else if (m.step === "build") show(0.97, "Making the zip…");
    else if (m.done) finished(m);
    else if (m.error) stopped(m);
}

function finished({ zip, summary: s }) {
    setBusy(false);
    step.textContent = "";
    buildButton.textContent = "Build the SD card";
    if (zipUrl) URL.revokeObjectURL(zipUrl);
    zipUrl = URL.createObjectURL(new Blob([zip], { type: "application/zip" }));
    download.href = zipUrl;
    download.textContent = `Download contrail-sd-card.zip (${mb(zip.byteLength)})`;
    summary.textContent = `map.pak: ${s.towns.toLocaleString()} towns and ${s.airports.toLocaleString()} airports within 250 nm. ` +
        (s.logos ? "logos.pak: included." : "logos.pak: not available yet (no release); the card works without it.");
    result.hidden = false;
    download.focus();
}

function stopped({ error, what }) {
    setBusy(false);
    step.textContent = "";
    buildButton.textContent = "Try again";
    failed.replaceChildren();
    if (error === "range") {  // the builder's own check disagreed with coverable(): nothing to retry for this home
        buildButton.disabled = true;
        buildButton.textContent = "Build the SD card";
        homeNote.textContent = `The map can't be built for this home: ${what}.`;
        homeNote.hidden = false;
        return;
    }
    if (error === "load") {
        failed.textContent = `Couldn't download ${what}. Check the connection and try again.`;
    } else {
        failed.append(`The build failed: ${what}. `, Object.assign(document.createElement("a"), {
            href: "https://github.com/ChristopherJacob/contrail/issues", textContent: "Report it on GitHub" }), ".");
    }
    failed.hidden = false;
}

function workerFailed() {
    worker?.terminate();
    worker = null;
    if (busy) stopped({ error: "load", what: "the builder" });
}

buildButton.addEventListener("click", () => {
    const h = home();
    if (!canBuild()) return;
    setBusy(true);
    result.hidden = failed.hidden = true;
    results.hidden = true;
    show(0, "Starting…");
    if (!worker) {
        worker = new Worker("sd-worker.js");
        worker.onmessage = onMessage;
        worker.onerror = workerFailed;
        worker.onmessageerror = workerFailed;
    }
    worker.postMessage({ lat: h[0], lon: h[1] });
});

// ---------------------------------------------------------------- #lat=&lon= (the board's web page links here)
// The fragment never reaches the server; ?lat=&lon= is accepted too. Either way the address is cleaned afterwards,
// so the coordinates don't linger in the address bar, history or bookmarks.

function readLink() {
    for (const params of [new URLSearchParams(location.hash.slice(1)), new URLSearchParams(location.search)]) {
        if (params.has("lat") && params.has("lon")) {
            setHome(params.get("lat"), params.get("lon"));
            history.replaceState(null, "", location.pathname);
            return;
        }
    }
}
readLink();
addEventListener("hashchange", readLink);  // a link followed while the page is already open
