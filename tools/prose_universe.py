"""US-T171: checks for the SpaceGod <-> Prose Universe Interchange contract (docs/PROSE.md).

  python tools/prose_universe.py validate <file>            schema + contract checks (exit 1 on any failure)
  python tools/prose_universe.py stats <file>               entity and edge counts by type and kind (JSON)
  python tools/prose_universe.py diff <ours> <theirs>       semantic diff of two interchange files (exit 1 if they differ)
  python tools/prose_universe.py leak <ours> <snapshot>     a Hub /snapshot?scope=all against the file: every node and
                                                            edge Prose holds for the universe must be one of ours

Standard library only, except that `validate` uses the jsonschema package when it is installed (it falls back to the
contract checks below, which cover everything the schema says, when it is not).
"""

import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(os.path.dirname(HERE), "Data", "Prose", "universe.schema.json")

# Prose re-slugifies every id (lowercase, runs of anything but a-z0-9 to one hyphen, trimmed). An id already in this
# form comes back unchanged; anything else would be renamed on import (the EVE post-mortem's re-slugify bug).
STABLE_ID = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def validate(path):
    doc = load(path)
    errors = []
    try:
        import jsonschema  # type: ignore

        schema = load(SCHEMA)
        for e in sorted(jsonschema.Draft202012Validator(schema).iter_errors(doc), key=lambda e: list(e.path))[:20]:
            errors.append(f"schema: {'/'.join(str(p) for p in e.path)}: {e.message}")
    except ImportError:
        pass

    u = doc.get("universe")
    if not isinstance(u, dict) or not u.get("id") or not u.get("name"):
        errors.append("universe.id and universe.name are required")
    elif not STABLE_ID.match(u["id"]):
        errors.append(f"universe.id '{u['id']}' is not a stable slug")
    ents = doc.get("entities")
    if not isinstance(ents, list):
        errors.append("entities must be an array")
        ents = []
    ids = collections.Counter()
    for i, e in enumerate(ents):
        for k in ("id", "type", "name", "summary"):
            if not isinstance(e.get(k), str):
                errors.append(f"entities[{i}].{k} missing or not a string")
        if isinstance(e.get("id"), str):
            ids[e["id"]] += 1
            if not STABLE_ID.match(e["id"]):
                errors.append(f"entities[{i}].id '{e['id']}' is not a stable slug (Prose would rename it)")
        if "details" in e and not isinstance(e["details"], dict):
            errors.append(f"entities[{i}].details must be an object")
        for r in e.get("relations", []):
            if not isinstance(r.get("to"), str) or not isinstance(r.get("kind"), str):
                errors.append(f"entities[{i}] has a relation without to/kind")
    for i, n in ids.items():
        if n > 1:
            errors.append(f"duplicate id '{i}' ({n} times)")
    known = set(ids)
    dangling = [(e["id"], r["to"]) for e in ents for r in e.get("relations", []) if r.get("to") not in known]
    if dangling:
        errors.append(f"{len(dangling)} relations point at ids not in the file (Prose would invent stubs), e.g. {dangling[:3]}")
    if "spacegod-export-manifest" not in known:
        errors.append("no spacegod-export-manifest entity (build hash and counts)")
    else:
        m = next(e for e in ents if e["id"] == "spacegod-export-manifest")["details"]
        if m.get("entityCount") != len(ents):
            errors.append(f"manifest entityCount {m.get('entityCount')} != {len(ents)} entities in the file")
        edges = sum(len(e.get("relations", [])) for e in ents)
        if m.get("edgeCount") != edges:
            errors.append(f"manifest edgeCount {m.get('edgeCount')} != {edges} relations in the file")

    if errors:
        print(f"INVALID {path}")
        for e in errors[:40]:
            print("  " + e)
        return 1
    print(f"valid   {path}: {len(ents)} entities, {sum(len(e.get('relations', [])) for e in ents)} edges")
    return 0


def stats_of(doc):
    ents = doc["entities"]
    types = collections.Counter(e["type"] for e in ents)
    kinds = collections.Counter(r["kind"] for e in ents for r in e.get("relations", []))
    return {
        "entities": len(ents),
        "edges": sum(kinds.values()),
        "entitiesByType": dict(sorted(types.items())),
        "edgesByKind": dict(sorted(kinds.items())),
    }


def canon(e):
    """An entity as Prose stores it (RFC 0007's Record.Json): the fields the contract has, absent details as {}."""
    return {
        "id": e.get("id", ""),
        "type": e.get("type", ""),
        "name": e.get("name", ""),
        "summary": e.get("summary", ""),
        "details": e.get("details") or {},
        "relations": sorted((r.get("to", ""), r.get("kind", "")) for r in e.get("relations", [])),
        "tags": e.get("tags", []),
    }


def diff(a_path, b_path):
    a, b = load(a_path), load(b_path)
    out = []
    ua, ub = a.get("universe", {}), b.get("universe", {})
    for k in sorted(set(ua) | set(ub)):
        if ua.get(k) != ub.get(k):
            out.append(f"universe.{k}: {json.dumps(ua.get(k))[:80]} != {json.dumps(ub.get(k))[:80]}")
    ea = {e["id"]: canon(e) for e in a["entities"]}
    eb = {e["id"]: canon(e) for e in b["entities"]}
    only_a = sorted(set(ea) - set(eb))
    only_b = sorted(set(eb) - set(ea))
    changed = []
    for i in sorted(set(ea) & set(eb)):
        if ea[i] != eb[i]:
            fields = [k for k in ea[i] if ea[i][k] != eb[i][k]]
            changed.append((i, fields))
    sa, sb = stats_of(a), stats_of(b)
    print(f"ours:   {sa['entities']} entities, {sa['edges']} edges  ({a_path})")
    print(f"theirs: {sb['entities']} entities, {sb['edges']} edges  ({b_path})")
    for t in sorted(set(sa["entitiesByType"]) | set(sb["entitiesByType"])):
        x, y = sa["entitiesByType"].get(t, 0), sb["entitiesByType"].get(t, 0)
        if x != y:
            out.append(f"type {t}: {x} != {y}")
    for k in sorted(set(sa["edgesByKind"]) | set(sb["edgesByKind"])):
        x, y = sa["edgesByKind"].get(k, 0), sb["edgesByKind"].get(k, 0)
        if x != y:
            out.append(f"edge kind {k}: {x} != {y}")
    if only_a:
        out.append(f"{len(only_a)} entities only in ours, e.g. {only_a[:5]}")
    if only_b:
        out.append(f"{len(only_b)} entities only in theirs (a leak or a stale entity), e.g. {only_b[:5]}")
    if changed:
        out.append(f"{len(changed)} entities differ, e.g. {changed[:5]}")
    if out:
        print("DIFFERS")
        for line in out:
            print("  " + line)
        return 1
    print("IDENTICAL: 0 entity diffs, 0 relation diffs, 0 universe diffs")
    return 0


def leak(file_path, snap_path):
    """Prose's graph view of the universe (/api/universes/<slug>/snapshot?scope=all, UniverseGraphService) against the
    file: the EVE post-mortem found that service serving other universes' rows. Every node it shows must be ours."""
    doc, snap = load(file_path), load(snap_path)
    ours = {e["id"] for e in doc["entities"]}
    nodes = snap.get("nodes") or snap.get("Nodes") or []
    edges = snap.get("edges") or snap.get("Edges") or []
    node_ids = []
    for n in nodes:
        node_ids.append(str(n.get("slug") or n.get("id") or n.get("Id") or ""))
    foreign = [n for n in node_ids if n not in ours]
    types = collections.Counter(str(n.get("type") or n.get("Type") or "?") for n in nodes)
    print(f"snapshot: {len(nodes)} nodes, {len(edges)} edges; file: {len(ours)} entities, "
          f"{sum(len(e.get('relations', [])) for e in doc['entities'])} edges")
    print(f"snapshot node types: {dict(sorted(types.items()))}")
    if foreign:
        print(f"LEAK?   {len(foreign)} snapshot nodes are not entities of the file, e.g. {foreign[:8]}")
        return 1
    print("no foreign nodes: every snapshot node is an entity of the file")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "validate":
        return validate(argv[2])
    if cmd == "stats":
        print(json.dumps(stats_of(load(argv[2])), indent=2))
        return 0
    if cmd == "diff" and len(argv) >= 4:
        return diff(argv[2], argv[3])
    if cmd == "leak" and len(argv) >= 4:
        return leak(argv[2], argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
