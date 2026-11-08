// The browser simulator's worker (6b spec §2): runs the firmware (sim/contrail.js) on a place's recording and hands
// its frames to sim.js. In: {start: place, logos, theme}, {touch: [down, x, y]}, {show: screen}, {theme: name},
// {alert: true}, {pause: true}, {resume: true}. Out: {ready: true}, {started: id}, {frame: Uint16Array},
// {led: [r, g, b]}, {theme: name} and {screen: id} (what is on screen, also when the device's own gestures and panel
// change it), {error: "load" | "crash", what}. After a crash the runtime is aborted: the page ends the worker.
importScripts("sim/contrail.js");

const FRAME_MS = 1000 / 30;
let m = null, timer = 0, last = 0, theme = "", screen = -1;
let running = false, paused = false;  // a place started and not failed; the page's tab hidden
// The touch samples for the coming steps, one per step, as the board's touch panel is read once a frame. Pointer
// events come faster than steps and in bursts (a quick flick can be down, one move, up between two steps): the
// press's first sample and its release are kept, moves between them coalesce, so the firmware sees where a swipe
// began and the queue stays a few samples long.
const queue = [];
let touch = { down: 0, x: 0, y: 0 };

function pushTouch(down, x, y) {
  const tail = queue[queue.length - 1];
  if (down && touch.down && tail && tail.down && tail.move) Object.assign(tail, { x, y });
  else if (down) queue.push({ down: 1, x, y, move: touch.down === 1 });
  // A tap needs two pressed samples (Gesture.h's kTapMinSamples): one more at the release point, however short the
  // press was.
  else if (touch.down) queue.push({ down: 1, x, y }, { down: 0, x, y });
  touch = { down, x, y };
}

function stop() {
  clearTimeout(timer);
  timer = 0;
}

function fail(error, what) {
  stop();
  running = false;
  if (error === "crash") m = null;  // an aborted runtime takes no more calls
  postMessage({ error, what });
}

// Before sim_start: the firmware's own fetch of a missing recording would just look like an empty sky.
function exists(url) {
  const xhr = new XMLHttpRequest();
  xhr.open("HEAD", url, false);
  try {
    xhr.send();
  } catch {
    return false;
  }
  return xhr.status === 200;
}

function step() {
  const now = performance.now();
  try {
    const s = queue.shift() || touch;
    m._sim_touch(s.down, s.x, s.y);
    // Capped: a background tab's throttled timers would otherwise hand the firmware one huge step.
    m._sim_step(Math.min(0.1, (now - last) / 1000));
    const t = m.ccall("sim_theme_name", "string", [], []);
    if (t !== theme) postMessage({ theme: (theme = t) });
    const id = m._sim_screen();
    if (id !== screen) postMessage({ screen: (screen = id) });
  } catch (e) {
    return fail("crash", String((e && e.message) || e));
  }
  last = now;
  // Hidden: no frames to show, and the replay waits rather than running on unseen.
  timer = paused ? 0 : setTimeout(step, Math.max(0, FRAME_MS - (performance.now() - now)));
}

function start({ start: p, logos, theme: chosen }) {
  stop();
  running = false;
  theme = "", screen = -1;  // the new place's are posted after its first step
  if (!exists(`sim/${p.id}/000.json`)) return fail("load", `the ${p.name} recording`);
  // localtime.js reads it on every clock: set before the place starts, so its clocks show the place's own time.
  m.timeZone = p.zone;
  try {
    m.ccall("sim_start", "number",
            ["string", "number", "number", "number", "number", "number", "string", "string", "string"],
            [`sim/${p.id}`, p.lat, p.lon, p.start, p.replies, p.interval, chosen, `sim/map-${p.id}.pak`,
             logos ? "firmware/logos.pak" : ""]);
  } catch (e) {
    return fail("crash", String((e && e.message) || e));
  }
  postMessage({ started: p.id });
  running = true;
  last = performance.now();
  step();
}

onmessage = ({ data: d }) => {
  if (d.pause) {
    paused = true;
    stop();
    return;
  }
  if (d.resume) {
    paused = false;
    // One step for the time hidden (step caps it), then the usual pace.
    if (running && m && !timer) step();
    return;
  }
  if (!m) return;
  try {
    if (d.start) start(d);
    else if (d.touch) pushTouch(...d.touch);
    else if (d.show !== undefined) m._sim_show(d.show);
    else if (d.theme) m.ccall("sim_theme", null, ["string"], [d.theme]);
    else if (d.alert) m._sim_alert();
  } catch (e) {
    fail("crash", String((e && e.message) || e));
  }
};

createContrail({
  locateFile: (f) => `sim/${f}`,
  onFrame: (px) => {
    const f = px.slice();  // px is a view into the WASM memory, valid only during this call (WasmGlue.h)
    postMessage({ frame: f }, [f.buffer]);
  },
  onLed: (r, g, b) => postMessage({ led: [r, g, b] }),
  onLog: (line) => console.debug(line),
}).then((mod) => {
  m = mod;
  postMessage({ ready: true });
}, () => postMessage({ error: "load", what: "the simulator" }));
