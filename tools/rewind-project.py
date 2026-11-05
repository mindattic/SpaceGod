"""Rewind projection (US-T99, docs/REWIND.md): turns the probe's measurements into budget reach and seek latency.

Reads Bench/results/rewind-<scenario>.json (tools/bench.ps1 -Rewind; Bench.Rewind.exe --rewind-probe) and prints:
  1. the delta table: median Kraken-Normal delta size per interval, quiet vs battle;
  2. how far back a RAM and a disk budget reach with a tiered thinning schedule (dense recent, sparse old);
  3. typical seek latency for 1 minute, 1 hour and 1 day back at each warp the Bench measured.

    python tools/rewind-project.py [--results Bench/results] [--ram-mb 512] [--disk-mb 4096]

Model (stated so the numbers can be checked):
- Every retained keyframe is stored as a delta against the retained keyframe before it, so its size is the measured
  delta for that gap (log-log interpolation between measured intervals; beyond the largest one it is capped at a full
  keyframe). One keyframe in FULL_EVERY is stored whole instead, bounding the decode chain; the session start is
  always kept whole; the newest keyframe is also held raw (uncompressed) as the next delta's dictionary.
- Battle-heavy uses the battle scenarios' deltas where they exist (<= 10 min) and the quiet ones beyond.
- Seek latency = restore + chain decode + replay of the worst gap (the tier spacing) at that warp's replay speed.
"""
import argparse
import glob
import json
import math
import os
import statistics

FULL_EVERY = 16
CODEC = "kraken-normal"

# (age the tier runs to, spacing) in game seconds: dense recent, sparse old. The last tier runs forever.
MIN, HOUR, DAY, WEEK = 60, 3600, 86400, 7 * 86400
TIERS_QUIET = [(HOUR, MIN), (DAY, 10 * MIN), (WEEK, HOUR), (90 * DAY, DAY), (math.inf, WEEK)]
TIERS_BATTLE = [(15 * MIN, 10), (HOUR, MIN), (DAY, 10 * MIN), (WEEK, HOUR), (90 * DAY, DAY), (math.inf, WEEK)]


def load(results):
    runs = {}
    for f in glob.glob(os.path.join(results, "rewind-*.json")):
        with open(f, encoding="utf-8") as fh:
            j = json.load(fh)
        runs[j["scenario"]] = j
    return runs


def delta_table(runs):
    quiet, battle = {}, {}
    for name, j in runs.items():
        table = battle if name.startswith("battle") else quiet
        for d in j["deltas"]:
            if CODEC in d:
                table.setdefault(d["interval"], []).append(d[CODEC]["bytes"])
    med = lambda t: {k: statistics.median(v) for k, v in sorted(t.items())}
    return med(quiet), med(battle)


def interp(table, gap, cap):
    pts = sorted(table.items())
    if not pts:
        return cap
    if gap <= pts[0][0]:
        return pts[0][1]
    for (g0, s0), (g1, s1) in zip(pts, pts[1:]):
        if g0 <= gap <= g1:
            t = math.log(gap / g0) / math.log(g1 / g0)
            return min(cap, math.exp(math.log(s0) + t * (math.log(s1) - math.log(s0))))
    return cap


def reach(tiers, size_of, budget, full, raw):
    """Walks back from now through the tiers; returns (age reached, keyframes) when the budget runs out."""
    used = full + raw  # the session start, whole, and the newest keyframe raw
    age, n, start = 0.0, 0, 0.0
    for end, spacing in tiers:
        while age + spacing <= end:
            cost = size_of(spacing) * (FULL_EVERY - 1) / FULL_EVERY + full / FULL_EVERY
            if used + cost > budget:
                return age, n
            used += cost
            age += spacing
            n += 1
            if age > 200 * 365 * DAY:
                return math.inf, n
    return age, n


def fmt_age(s):
    if s == math.inf:
        return "whole session"
    for unit, n in (("years", 365.25 * DAY), ("days", DAY), ("h", HOUR), ("min", MIN)):
        if s >= n:
            return f"{s / n:.1f} {unit}"
    return f"{s:.0f} s"


def main():
    ap = argparse.ArgumentParser()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--results", default=os.path.join(root, "Bench", "results"))
    ap.add_argument("--ram-mb", type=float, default=512)
    ap.add_argument("--disk-mb", type=float, default=4096)
    a = ap.parse_args()
    runs = load(a.results)
    if not runs:
        raise SystemExit("no rewind-*.json in " + a.results + " (run tools/bench.ps1 -Rewind)")
    quiet, battle = delta_table(runs)

    print("Delta (KB, %s, median) by interval:" % CODEC)
    for iv in sorted(set(quiet) | set(battle)):
        q, b = quiet.get(iv), battle.get(iv)
        print(f"  {fmt_age(iv):>10}: quiet {q / 1024 if q else float('nan'):8.0f}   battle {b / 1024 if b else float('nan'):8.0f}")

    fulls = [k[CODEC]["bytes"] for j in runs.values() for k in j["keyframes"] if CODEC in k]
    raws = [k["raw"] for j in runs.values() for k in j["keyframes"]]
    early = statistics.median([k[CODEC]["bytes"] for n, j in runs.items() if n in ("sol-idle", "battle", "galaxy-ftl") for k in j["keyframes"]])
    states = [("early session (%.1f MB keyframe)" % (early / 2**20), early, 8.5 * 2**20),
              ("a game-year in (%.1f MB keyframe)" % (max(fulls) / 2**20), max(fulls), max(raws))]

    print("\nReach (how far back the thinned schedule fits; session start always kept):")
    for label, full, raw in states:
        for prof, tiers, size_of in (
                ("quiet", TIERS_QUIET, lambda g: interp(quiet, g, full)),
                ("battle-heavy", TIERS_BATTLE, lambda g: max(interp(battle, g, full) if g <= max(battle or {0: 0}) else 0, interp(quiet, g, full)))):
            for bname, mb in (("RAM", a.ram_mb), ("disk", a.disk_mb)):
                age, n = reach(tiers, size_of, mb * 2**20, full, raw)
                print(f"  {label:34} {prof:13} {bname:4} {mb:6.0f} MB: {fmt_age(age):>14} ({n} keyframes)")

    print("\nSeek latency (restore + chain decode + worst replay gap = the tier spacing at that age):")
    restore = {n: statistics.mean(r["restoreMs"] + r["decompressMs"] for r in j["replays"]) for n, j in runs.items() if j["replays"]}
    speed = {n: statistics.mean(r["gameSecPerWallSec"] for r in j["replays"]) for n, j in runs.items() if j["replays"]}
    decode = statistics.mean(d["deltaDecMs"] for j in runs.values() for d in j["deltas"])
    chain = decode * FULL_EVERY / 2
    for n in sorted(speed, key=lambda k: speed[k]):
        tiers = TIERS_BATTLE if n.startswith("battle") else TIERS_QUIET
        cells = []
        for back in (MIN, HOUR, DAY):
            spacing = next(sp for end, sp in tiers if back <= end)
            ms = restore[n] + chain + spacing / speed[n] * 1000
            cells.append(f"{fmt_age(back)} back {ms / 1000:7.2f} s")
        print(f"  {n:16} ({speed[n]:9.0f} game-s/wall-s, restore {restore[n]:4.0f} ms): " + ", ".join(cells))


if __name__ == "__main__":
    main()
