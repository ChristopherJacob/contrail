// Simulator page (6b spec §6): the screen on a canvas, the place, theme and screen controls, and the worker that runs
// the firmware (sim-worker.js). Every request is for a file of this site.
const $ = (id) => document.getElementById(id);
const canvas = $("screen"), ctx = canvas.getContext("2d"), img = ctx.createImageData(240, 320);
const place = $("place"), status = $("status"), recorded = $("recorded"), failed = $("failed"), led = $("led");
const retry = $("retry");
const LOADING = "Loading the simulator (about 1 MB)…";
const screenButtons = [...document.querySelectorAll("[data-screen]")];
let worker = null, places = [], logos = false, current = null, waiting = true, ledColour = "";

if (!("Worker" in window) || typeof WebAssembly !== "object") {
  $("sim").hidden = true;
  $("unsupported").hidden = false;
}

// gfx::unpack565
function draw(px) {
  const out = img.data;
  for (let i = 0, j = 0; i < px.length; ++i, j += 4) {
    const v = px[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[j] = (r << 3) | (r >> 2);
    out[j + 1] = (g << 2) | (g >> 4);
    out[j + 2] = (b << 3) | (b >> 2);
    out[j + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

// canRetry: a load that may work next time ("Try again"); else a crash, which is worth reporting ("Restart").
function showError(text, canRetry) {
  failed.replaceChildren(text);
  if (!canRetry) {
    failed.append(" ", Object.assign(document.createElement("a"), {
      href: "https://github.com/ChristopherJacob/contrail/issues", textContent: "Report it on GitHub" }), ".");
  }
  failed.hidden = false;
  retry.hidden = false;
  retry.textContent = canRetry ? "Try again" : "Restart";
  status.textContent = "";
  ctx.clearRect(0, 0, 240, 320);  // not the previous place's last frame under the new place's name
}

// A crashed runtime is aborted: the worker goes, and the controls do nothing until Restart boots a new one.
function endWorker() {
  worker?.terminate();
  worker = null;
  for (const b of screenButtons) b.setAttribute("aria-pressed", "false");
}

// In the place's own time, as its board would show it.
function recordedLine(p) {
  const at = new Intl.DateTimeFormat(undefined, { timeZone: p.zone, hour: "numeric", minute: "2-digit" })
    .format(new Date(p.start * 1000));
  const mins = Math.max(0, Math.round((Date.now() / 1000 - p.start) / 60));
  const ago = mins < 2 ? "just now" : mins < 120 ? `${mins} minutes ago` : `${Math.round(mins / 60)} hours ago`;
  const loop = Math.max(1, Math.round((p.replies * p.interval) / 60));
  return `Recorded at ${at} local time, ${ago}, and replayed in a ${loop}-minute loop. ` +
    "A board shows live flights over your home.";
}

function theme() {
  return document.querySelector('input[name="theme"]:checked').value;
}

function startPlace(id) {
  current = places.find((p) => p.id === id) || places[0];
  failed.hidden = retry.hidden = true;
  recorded.textContent = recordedLine(current);
  waiting = true;
  worker.postMessage({ start: current, logos, theme: theme() });
}

function boot() {
  failed.hidden = retry.hidden = true;
  status.textContent = LOADING;
  waiting = true;
  endWorker();
  worker = new Worker("sim-worker.js");
  if (document.hidden) worker.postMessage({ pause: true });
  // importScripts failing (a missing contrail.js) and anything the worker didn't catch.
  worker.onerror = (e) => {
    e.preventDefault();
    endWorker();
    showError("Couldn't load the simulator. Check the connection and try again.", true);
  };
  worker.onmessage = ({ data: d }) => {
    if (d.frame) {
      draw(d.frame);
      if (waiting) waiting = false, status.textContent = "";
    } else if (d.led) {
      // Every frame of an alert's pulse: only touch the style when the colour changes.
      const c = d.led.some(Boolean) ? `rgb(${d.led.join(",")})` : "";
      if (c !== ledColour) {
        ledColour = led.style.color = led.style.background = c;
        led.setAttribute("aria-label", c ? "The board's LED: lit" : "The board's LED: off");
      }
    } else if (d.screen !== undefined) {
      for (const b of screenButtons) b.setAttribute("aria-pressed", String(Number(b.dataset.screen) === d.screen));
    } else if (d.theme) {
      // The firmware's theme, which the quick-settings panel may have changed.
      const r = document.querySelector(`input[name="theme"][value="${d.theme}"]`);
      if (r) r.checked = true;
    } else if (d.ready) startPlace(place.value);
    else if (d.error === "load") showError(`Couldn't load ${d.what}. Check the connection and try again.`, true);
    else if (d.error) {
      endWorker();
      showError(`The simulator stopped: ${d.what}.`, false);
    }
  };
}

async function init() {
  failed.hidden = retry.hidden = true;
  status.textContent = LOADING;
  let list;
  try {
    const r = await fetch("sim/places.json");
    // Missing: no recorder run has published one yet, the same as an empty list.
    if (r.status === 404) list = { places: [] };
    else if (!r.ok) throw new Error(r.status);
    else list = await r.json();
  } catch {
    return showError("Couldn't load the list of places. Check the connection and try again.", true);
  }
  places = Array.isArray(list.places) ? list.places : [];
  logos = !!list.logos;
  if (!places.length) {
    status.textContent = "";
    failed.textContent = "No recording is available right now; try again in an hour.";
    failed.hidden = false;
    return;
  }
  place.replaceChildren(...places.map((p) => new Option(`${p.name} (${p.airport})`, p.id)));
  boot();
}

place.addEventListener("change", () => worker && startPlace(place.value));
for (const r of document.querySelectorAll('input[name="theme"]'))
  r.addEventListener("change", () => worker?.postMessage({ theme: theme() }));
for (const b of screenButtons)
  b.addEventListener("click", () => worker?.postMessage({ show: Number(b.dataset.screen) }));
$("alert").addEventListener("click", () => worker?.postMessage({ alert: true }));
retry.addEventListener("click", () => (places.length ? boot() : init()));
// A hidden tab's frames would go unseen: the worker stops stepping until it shows again.
document.addEventListener("visibilitychange",
                          () => worker?.postMessage(document.hidden ? { pause: true } : { resume: true }));
// "N minutes ago" stays true while the page is open.
setInterval(() => current && (recorded.textContent = recordedLine(current)), 60000);

// The canvas is the touch screen: pointer positions in screen pixels.
function at(e) {
  const r = canvas.getBoundingClientRect();
  return [Math.min(239, Math.max(0, Math.floor(((e.clientX - r.left) * 240) / r.width))),
          Math.min(319, Math.max(0, Math.floor(((e.clientY - r.top) * 320) / r.height)))];
}
// One pointer at a time, as the board's resistive panel reads one touch: a second finger is ignored.
let pointer = null, lastAt = [0, 0];
canvas.addEventListener("pointerdown", (e) => {
  if (pointer !== null) return;
  // A swipe that leaves the canvas still ends on it. Without the capture its release could be missed and the touch
  // never end, so such a press is ignored.
  try {
    canvas.setPointerCapture(e.pointerId);
  } catch {
    return;
  }
  pointer = e.pointerId;
  worker?.postMessage({ touch: [1, ...(lastAt = at(e))] });
});
canvas.addEventListener("pointermove", (e) => {
  if (e.pointerId === pointer) worker?.postMessage({ touch: [1, ...(lastAt = at(e))] });
});
canvas.addEventListener("pointerup", (e) => {
  if (e.pointerId !== pointer) return;
  pointer = null;
  worker?.postMessage({ touch: [0, ...at(e)] });
});
// A cancel's position can be anywhere (the browser took the gesture), and a lost capture has no release of its own:
// either releases where the touch last was. After a pointerup the capture's loss is ignored (pointer is null).
for (const type of ["pointercancel", "lostpointercapture"]) {
  canvas.addEventListener(type, (e) => {
    if (e.pointerId !== pointer) return;
    pointer = null;
    worker?.postMessage({ touch: [0, ...lastAt] });
  });
}

if (!$("sim").hidden) init();
