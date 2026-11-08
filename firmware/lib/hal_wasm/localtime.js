// localtime_r for the browser build. Emscripten's takes the zone from JS Date (the visitor's) and ignores TZ; the
// simulator shows a place's own time, so this one uses the IANA zone the worker sets (Module.timeZone,
// "America/New_York"), "UTC" until then (the parity test's demo frames, whose clocks are UTC). tm_isdst stays 0:
// the firmware reads only the fields and the offset. tm_zone and mktime still follow the visitor's zone; the firmware
// uses neither (no mktime, %Z or tm_zone in lib/core).
addToLibrary({
  _localtime_js__i53abi: true,
  _localtime_js: (time, tmPtr) => {
    const ms = time * 1000;
    if (!Number.isFinite(ms)) return 1;
    const zone = Module["timeZone"] || "UTC";
    const cache = (globalThis.contrailZones ??= {});
    const options = { hourCycle: "h23", year: "numeric", month: "numeric", day: "numeric",
                      hour: "numeric", minute: "numeric", second: "numeric" };
    let f = cache[zone];
    if (!f) {
      try {
        f = new Intl.DateTimeFormat("en-US", { ...options, timeZone: zone });
      } catch (e) {  // RangeError: a zone this browser doesn't know. UTC rather than an abort
        f = new Intl.DateTimeFormat("en-US", { ...options, timeZone: "UTC" });
      }
      cache[zone] = f;
    }
    const p = {};
    for (const x of f.formatToParts(new Date(ms))) p[x.type] = x.value;
    const y = +p.year, mo = +p.month - 1, d = +p.day, h = +p.hour, mi = +p.minute, s = +p.second;
    const local = Date.UTC(y, mo, d, h, mi, s);
    const off = Math.round((local - Math.floor(time) * 1000) / 1000);
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_sec, 's', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_min, 'mi', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_hour, 'h', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_mday, 'd', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_mon, 'mo', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_year, 'y - 1900', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_wday, 'new Date(local).getUTCDay()', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_yday, '((local - Date.UTC(y, 0, 1)) / 86400000) | 0', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_gmtoff, 'off', LONG_TYPE) }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_isdst, '0', 'i32') }}};
    return 0;
  },
});
