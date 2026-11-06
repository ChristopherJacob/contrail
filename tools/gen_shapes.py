#!/usr/bin/env python3
"""Build firmware/lib/core/data/ShapeData.cpp: the aircraft shape families and the type table.

The 12 families' outlines come from tools/shapes_outlines.json (drawn on the 5a canvas) and are
stored as uint8 half-units. Every ICAO Doc 8643 type in tar1090-db's icao_aircraft_types2.js
(gzipped JSON: {"B789": ["BOEING 787-9 Dreamliner", "L2J", "H"], ...}) is classified into a family
by rule (description class, wake category, name keywords) with tools/shapes_overrides.csv on top,
and stored as a sorted uint32: the code in base 37 (21 bits) << 4 | family (1..12).

  curl -L -o types.js.gz https://raw.githubusercontent.com/wiedehopf/tar1090-db/2ab834a65f2fbde9457823e88682cd46954b8bc1/db/icao_aircraft_types2.js
  python3 -I tools/gen_shapes.py --types types.js.gz --out firmware/lib/core/data/ShapeData.cpp
  python3 -I tools/gen_shapes.py --selftest

Exit codes: 0 written, 2 jets left unclassified (listed; add them to the overrides), 3 a known type
came out wrong. The output is committed, so firmware builds never need this script or the data.
"""
import argparse
import csv
import gzip
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FAMILIES = ["narrowbody", "widebody", "jumbo", "superjumbo", "regional", "bizjet", "turboprop", "single",
            "twin", "helicopter", "fastjet", "airlifter"]  # index + 1 = data::Family
ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"  # digit 0 of base 37 is "no character"

# Types the output must classify this way; checked before anything is written.
KNOWN = {"B789": "widebody", "A320": "narrowbody", "B738": "narrowbody", "A388": "superjumbo",
         "B744": "jumbo", "A346": "jumbo", "CRJ9": "regional", "E145": "regional", "C56X": "bizjet",
         "GLF5": "bizjet", "AT76": "turboprop", "DH8D": "turboprop", "C172": "single", "P28A": "single",
         "BE58": "twin", "R44": "helicopter", "EC35": "helicopter", "F16": "fastjet", "F35": "fastjet",
         "C17": "airlifter", "C130": "airlifter", "B77W": "widebody", "E75L": "narrowbody",
         "BCS3": "narrowbody", "MD11": "widebody", "B752": "narrowbody", "B763": "widebody",
         "A21N": "narrowbody", "B38M": "narrowbody", "E190": "narrowbody", "MD82": "narrowbody",
         "AT72": "turboprop", "B350": "turboprop", "C208": "single", "SR22": "single", "PC12": "single",
         "B06": "helicopter", "H60": "helicopter", "V22": "helicopter", "T38": "fastjet", "F18S": "fastjet",
         "EUFI": "fastjet", "A225": "superjumbo", "DC10": "widebody", "J328": "regional", "LJ45": "bizjet",
         "C25A": "bizjet", "F2TH": "bizjet", "A343": "jumbo", "B52": "jumbo", "C5M": "airlifter"}

BIZJET = ["CITATION", "GULFSTREAM", "LEARJET", "FALCON", "CHALLENGER", "GLOBAL", "PHENOM", "HAWKER",
          "PREMIER", "HONDAJET", "PRAETOR", "LEGACY", "LINEAGE", "HORIZON", "WESTWIND", "ASTRA",
          "SABRELINER", "JETSTAR", "MU-300", "BEECHJET", "ECLIPSE", "VISION", "PILATUS PC-24", "CJ"]
REGIONAL = ["CRJ", "REGIONAL JET", "ERJ-135", "ERJ-140", "ERJ-145", "EMB-135", "EMB-145", "FOKKER 70",
            "FOKKER 100", "FOKKER F-28", "BAE-146", "AVRO RJ", "DORNIER 328JET", "SUPERJET",
            "SPACEJET", "ARJ21", "YAK-40", "MRJ", "328 JET"]
FASTJET_MAKERS = ["LOCKHEED MARTIN F-", "GENERAL DYNAMICS F-", "MCDONNELL DOUGLAS F-", "BOEING F-", "NORTHROP",
                  "MIKOYAN", "SUKHOI SU-", "DASSAULT MIRAGE", "DASSAULT RAFALE", "EUROFIGHTER", "SAAB JAS",
                  "SAAB 35", "SAAB 37", "PANAVIA", "SEPECAT", "AERMACCHI", "AERO L-", "BAE HAWK",
                  "HAWKER SIDDELEY HAWK", "T-38", "T-45", "T-7", "ALPHA JET", "CHENGDU", "SHENYANG",
                  "KAI T-50", "HAL TEJAS", "FAIRCHILD A-10", "GRUMMAN F-", "VOUGHT", "MITSUBISHI F-",
                  "MCDONNELL F-", "LOCKHEED F-", "CONVAIR F-", "REPUBLIC F-", "NORTH AMERICAN F-",
                  "YAKOVLEV YAK-130", "HONGDU", "PZL TS-11",
                  "MCDONNELL-DOUGLAS F-", "LOCKHEED-MARTIN F-", "CONVAIR QF-", "GRUMMAN A-", "GRUMMAN X-",
                  "LOCKHEED T-", "LOCKHEED U-", "KAWASAKI T-", "MITSUBISHI T-", "AIDC", "KOREA AEROSPACE",
                  "HINDUSTAN", "SAAB 29", "SAAB 32", "SAAB 39", "XIAN JH", "GUIZHOU", "IAI K", "SUPERMARINE",
                  "NORTH AMERICAN SABRE", "BRITISH AEROSPACE HARRIER", "DASSAULT SUPER MYSTERE",
                  "DASSAULT ETENDARD", "DOUGLAS A-", "BOEING FA-", "BOEING MQ-", "BOEING X-32", "BAC CANBERRA",
                  "BAC LIGHTNING", "SUD VAUTOUR", "TUPOLEV TU-22", "DE HAVILLAND DH-112", "GLOSTER", "PZL-MIELEC I-22", "CASA C-101",
                  "CANADAIR TUTOR", "GRUMMAN F9F", "AEROSPATIALE CM-170", "SOKO G", "SOKO JASTREB", "FIAT G-91",
                  "HAWKER SIDDELEY GNAT", "HAWKER HUNTER", "HAWKER SEA HAWK", "HAWKER-SIDDELEY BUCCANEER",
                  "FMA IA-63", "AVIOANE", "LET L-39", "EMBRAER EMB-326", "MORANE-SAULNIER", "SIAI-MARCHETTI S-211",
                  "CASA HA-220", "SAAB 105", "BAC STRIKEMASTER", "BAC P-84", "FUJI T1", "NORTH AMERICAN T-2",
                  "CESSNA 318", "PZL-MIELEC TS-11", "IRIAF", "DE HAVILLAND DH-100", "MESSERSCHMITT", "BOOM XB",
                  "KRATOS", "BOEING AUSTRALIA MQ-28", "YAKOVLEV YAK-30", "YAKOVLEV YAK-32"]
AIRLIFTER = ["C-17", "C-5", "AN-124", "AN-225", "A400M", "C-130", "HERCULES", "IL-76", "C-141", "KAWASAKI C-2",
             "EMBRAER KC-390", "C-390", "Y-20"]
SUPERJUMBO = ["A380", "A-380"]
JUMBO = ["747", "BOEING 720", "A340", "707", "KC-135", "IL-96", "IL-86", "DC-8", "VC10", "IL-62", "E-3"]
WIDE_TWIN_OR_TRI = ["A300", "A310", "A330", "A350", "767", "777", "787", "DC-10", "MD-11", "L-1011", "TRISTAR",
                    "IL-96", "C919"]


def has(name, words):
    """True when any keyword occurs in the name as a whole token: not glued to a letter or digit before it,
    and, when the keyword ends in a digit, not followed by another ("C-5" must not match "C-500")."""
    for w in words:
        tail = "(?![0-9])" if w[-1].isdigit() else ""
        if re.search(rf"(?<![A-Z0-9]){re.escape(w)}{tail}", name):
            return True
    return False


def classify(code, name, desc, wake):
    """The family for one type, or None for a jet the rules cannot place."""
    kind = desc[:1]
    engines = desc[1:2]
    power = desc[2:3]
    if kind in ("H", "R", "T"):  # helicopters and tilt-rotors
        return "helicopter"
    if power == "J":
        if has(name, SUPERJUMBO):
            return "superjumbo"
        if has(name, AIRLIFTER):
            return "airlifter"
        if has(name, FASTJET_MAKERS):
            return "fastjet"
        if has(name, BIZJET):
            return "bizjet"
        if has(name, REGIONAL):
            return "regional"
        if engines == "4":
            return "jumbo" if wake in ("H", "J") or has(name, JUMBO) else "regional"  # 4-jet medium: BAe 146 / Avro RJ
        if has(name, WIDE_TWIN_OR_TRI) or (wake == "H" and engines in ("2", "3")):
            return "widebody"
        if wake == "L":
            return "bizjet"
        if wake == "M" and (name.split() or [""])[0] in ("AIRBUS", "BOEING", "EMBRAER", "BOMBARDIER", "MCDONNELL",
                                                "DOUGLAS", "TUPOLEV", "YAKOVLEV", "COMAC", "IRKUT", "FOKKER",
                                                "BAC", "SUD", "CANADAIR", "DE HAVILLAND", "ANTONOV", "MCDONNELL-DOUGLAS"):
            return "narrowbody"
        return None
    if power in ("T", "P"):
        if engines == "1":
            return "single"
        if wake == "H" or has(name, AIRLIFTER):
            return "airlifter"
        if power == "P":
            return "twin" if engines == "2" else "turboprop"
        return "turboprop"
    # Gliders, balloons, gyrocopters, electric and the rest: a light single is the closest outline.
    return "single"


def base37(code):
    assert len(code) <= 4, code
    v = 0
    for ch in code.ljust(4, "\0")[:4]:
        v = v * 37 + (0 if ch == "\0" else ALPHABET.index(ch) + 1)
    return v


def load_types(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return json.loads(raw)


def load_overrides(path):
    out = {}
    with open(path, newline="") as f:
        for row in csv.reader(line for line in f if not line.startswith("#")):
            if not row or row[0] == "type":
                continue
            if len(row) < 2:
                sys.exit(f"{path}: row {row!r} needs a type and a family")
            code, fam = row[0].strip().upper(), row[1].strip()
            if not 2 <= len(code) <= 4 or not all(ch in ALPHABET for ch in code):
                sys.exit(f"{path}: bad type code {code!r} (2-4 characters of 0-9 A-Z)")
            if fam not in FAMILIES:
                sys.exit(f"{path}: unknown family {fam!r} for {code}")
            out[code] = fam
    return out


def table(types, overrides):
    out, unclassified = {}, []
    for code, (name, desc, wake) in sorted(types.items()):
        code = code.upper()
        if code == "ZZZZ":  # Doc 8643's "type unknown" placeholder: must stay a miss so the category decides
            continue
        if not all(c in ALPHABET for c in code) or not 2 <= len(code) <= 4:
            continue
        fam = overrides.get(code) or classify(code, name.upper(), desc, wake)
        if fam is None:
            unclassified.append(f"{code},{name},{desc},{wake}")
            continue
        out[code] = fam
    for code, fam in overrides.items():  # overrides may name types the list lacks
        out[code] = fam
    return out, unclassified


def half_units(v):
    h = round(v * 2)  # a few engine radii (2.6) are not half-units: up to 0.1 unit of rounding
    assert 0 <= h <= 255, v
    return h


def emit_family(f):
    def flat(rows, n):
        assert all(len(r) == n for r in rows)
        return [half_units(x) for r in rows for x in r]

    t, te, tl = f["t"], f.get("te", []), f.get("tl", [])
    s, se, sl = f["s"], f.get("se", []), f.get("sl", [])
    assert t[0][0] == 0 and t[-1][0] == 0, f["key"] + ": the half outline must start and end on the centreline"
    data = flat(t, 2) + flat(te, 4) + flat(tl, 4) + flat(s, 2) + flat(se, 4) + flat(sl, 4)
    counts = [len(t), len(te), len(tl), len(s), len(se), len(sl)]
    # kMaxOutline in Shapes.h sizes the firmware's point buffers (the top half is mirrored into 2 * 16).
    assert len(t) <= 16 and len(s) <= 16, f["key"] + ": outline longer than kMaxOutline"
    assert all(c <= 255 for c in counts), f["key"] + ": a count does not fit a uint8"
    return data, counts


def write(out_path, fams, tab):
    lines = ["// Generated by tools/gen_shapes.py from tools/shapes_outlines.json, tools/shapes_overrides.csv",
             "// and tar1090-db's ICAO Doc 8643 type list. Do not edit.",
             '#include "Shapes.h"', "", "namespace data {", "", "// clang-format off"]
    emitted = {f["key"]: emit_family(f) for f in fams}
    for f in fams:
        data, _ = emitted[f["key"]]
        rows = [", ".join(str(x) for x in data[i:i + 24]) for i in range(0, len(data), 24)]
        lines.append(f"static const uint8_t k_{f['key']}[] = {{")
        lines += [f"    {r}," for r in rows]
        lines.append("};")
    lines.append("const ShapeDef kShapes[kFamilyCount] = {")
    wake_px = {"L": 13, "M": 16, "H": 20, "J": 22}
    for f in fams:
        _, c = emitted[f["key"]]
        lines.append(f"    {{k_{f['key']}, {c[0]}, {c[1]}, {c[2]}, {c[3]}, {c[4]}, {c[5]}, "
                     f"{'true' if f.get('rotor') else 'false'}, {wake_px[f['wake']]}}},")
    lines.append("};")
    packed = sorted(base37(code) << 4 | (FAMILIES.index(fam) + 1) for code, fam in tab.items())
    lines.append(f"const uint32_t kTypeFamilies[] = {{")
    for i in range(0, len(packed), 8):
        lines.append("    " + ", ".join(f"0x{v:07X}u" for v in packed[i:i + 8]) + ",")
    lines += ["};", "const int kTypeFamilyCount = sizeof(kTypeFamilies) / sizeof(kTypeFamilies[0]);",
              "// clang-format on", "", "}  // namespace data", ""]
    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    return len(packed)


def selftest():
    assert base37("") == 0 and base37("A") == 11 * 37 ** 3 and base37("ZZZZ") < 1 << 21
    assert classify("X", "BOEING 787-9", "L2J", "H") == "widebody"
    assert classify("X", "CESSNA 172", "L1P", "L") == "single"
    assert classify("X", "BEECH 58 BARON", "L2P", "L") == "twin"
    assert classify("X", "ROBINSON R-44", "H1P", "L") == "helicopter"
    assert classify("X", "MYSTERY JET", "L2J", "M") is None
    assert classify("X", "THING", "", "") == "single"  # an empty description is not a helicopter or a jet
    assert classify("X", "FOUR JET", "L4J", "") == "regional"  # empty wake is not "H" or "J"
    assert classify("X", "", "L2J", "M") is None  # an empty name must not crash
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--types")
    ap.add_argument("--outlines", default=os.path.join(HERE, "shapes_outlines.json"))
    ap.add_argument("--overrides", default=os.path.join(HERE, "shapes_overrides.csv"))
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.types or not a.out:
        ap.error("--types and --out are required unless --selftest")
    with open(a.outlines) as f:
        fams = json.load(f)["families"]
    assert [f["key"] for f in fams] == FAMILIES, f"{a.outlines}: families must be {FAMILIES} in this order"
    tab, unclassified = table(load_types(a.types), load_overrides(a.overrides))
    if unclassified:
        print("Unclassified jets (add to tools/shapes_overrides.csv):", *unclassified, sep="\n  ")
        return 2
    wrong = [f"{c}: {tab.get(c)} (want {w})" for c, w in KNOWN.items() if tab.get(c) != w]
    if wrong:
        print("Known types came out wrong:", *wrong, sep="\n  ")
        return 3
    n = write(a.out, fams, tab)
    print(f"wrote {a.out}: {len(fams)} families, {n} types")
    return 0


if __name__ == "__main__":
    sys.exit(main())
