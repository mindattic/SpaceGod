"""US-T171: checks for the SpaceGod <-> Prose Universe Interchange contract (docs/PROSE.md).

  python tools/prose_universe.py validate <file>            schema + contract checks (exit 1 on any failure)
  python tools/prose_universe.py stats <file>               entity and edge counts by type and kind (JSON)
  python tools/prose_universe.py diff <ours> <theirs> [--allow-stale]   semantic diff of two interchange files (exit 1 if they differ)
  python tools/prose_universe.py leak <ours> <snapshot> [<prose-export>]   a Hub /snapshot?scope=all against the file: every node and
                                                            edge Prose holds for the universe must be one of ours
  python tools/prose_universe.py chunk <file> <dir> [size]  split into import batches (entities first, then relations)

Standard library only, except that `validate` uses the jsonschema package when it is installed (it falls back to the
contract checks below, which cover everything the schema says, when it is not).
"""

import collections
import json
import os
import re
import sys
import unicodedata

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


def diff(a_path, b_path, allow_stale=False):
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
    if allow_stale and only_b:
        # Counted without the stale entities, so the totals compare what this export put there.
        b = dict(b, entities=[e for e in b["entities"] if e["id"] in ea])
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
        # Prose never deletes on import (docs/PROSE.md, proposed change 1): what an older export held and this one does not
        # stays in Prose. --allow-stale lists those without failing.
        msg = f"{len(only_b)} entities only in theirs (stale from an older export, or a leak), e.g. {only_b[:5]}"
        if allow_stale:
            print("  stale: " + msg)
        else:
            out.append(msg)
    if changed:
        out.append(f"{len(changed)} entities differ, e.g. {changed[:5]}")
    if out:
        print("DIFFERS")
        for line in out:
            print("  " + line)
        return 1
    print("IDENTICAL: 0 entity diffs, 0 relation diffs, 0 universe diffs")
    return 0


def prose_slug(name):
    """Prose's UniverseGraphService node id: the entity's NAME slugified (it never reads the stored id, the RFC 0007
    post-mortem's re-slugify bug), so two entities with the same name share one graph node."""
    folded = "".join(c for c in unicodedata.normalize("NFD", name or "") if unicodedata.category(c) != "Mn")
    for a, b in (("ß", "ss"), ("æ", "ae"), ("Æ", "ae"), ("ø", "o"), ("Ø", "o"), ("œ", "oe"), ("Œ", "oe"), ("ł", "l"), ("Ł", "l")):
        folded = folded.replace(a, b)
    return re.sub(r"[^a-z0-9]+", "-", folded.lower().strip()).strip("-")


def leak(file_path, snap_path, stale_path=None):
    """Prose's graph view of the universe (/api/universes/<slug>/snapshot?scope=all, UniverseGraphService) against the
    file. The EVE post-mortem found that service serving other universes' rows. Its nodes are keyed by name slug, so the
    check is in those terms: every node must be the name of one of our entities, with one of our types, and its edges
    must be exactly the file's relations (source name, target name, kind): one more is a leak, one fewer a lost edge."""
    doc, snap = load(file_path), load(snap_path)
    ents = doc["entities"]
    by_id = {e["id"]: e for e in ents}
    stale = []
    if stale_path:
        # --allow-stale: what Prose's own export of the universe holds beyond the file (left by older exports, since Prose
        # never deletes on import) is the universe's own, not a leak; anything outside both still is.
        stale = [e for e in load(stale_path)["entities"] if e["id"] not in by_id]
        for e in stale:
            by_id[e["id"]] = e
        ents = ents + stale
    names = {prose_slug(e["name"]) for e in ents} | set(by_id)
    types = {e["type"] for e in ents} | {"place", "stub"}
    nodes = snap.get("nodes") or []
    edges = snap.get("edges") or []
    foreign = [n.get("id") for n in nodes if str(n.get("id")) not in names]
    foreign_types = collections.Counter(n.get("nodeType") for n in nodes if n.get("nodeType") not in types)
    ours = {(prose_slug(e["name"]), prose_slug(by_id[r["to"]]["name"]), r["kind"]) for e in ents for r in e.get("relations", []) if r["to"] in by_id}
    theirs = {(x.get("source"), x.get("target"), x.get("relationType")) for x in edges}
    extra, missing = theirs - ours, ours - theirs
    print(f"snapshot: {len(nodes)} nodes, {len(edges)} edges; file: {len(ents)} entities ({len({prose_slug(e['name']) for e in ents})} distinct names), "
          f"{sum(len(e.get('relations', [])) for e in ents)} relations ({len(ours)} distinct by name)")
    bad = False
    if foreign or foreign_types:
        print(f"LEAK    {len(foreign)} nodes are not entities of the file, e.g. {foreign[:8]}; foreign types {dict(foreign_types)}")
        bad = True
    if extra:
        print(f"LEAK    {len(extra)} edges Prose holds that the file does not, e.g. {sorted(extra)[:5]}")
        bad = True
    if missing:
        print(f"MISSING {len(missing)} of the file's relations are not edges in Prose (an import cut short?), e.g. {sorted(missing)[:5]}")
        bad = True
    if not bad:
        print("no leak: every graph node is one of the file's entities and the graph's edges are exactly the file's relations")
    return 1 if bad else 0

def chunk(path, out_dir, size):
    """Split an interchange file into import batches the Hub can take one short request at a time: first every entity
    with its relations left out (a-*.json), then every entity whole (b-*.json). By the time any relation is imported its
    target already exists, so Prose never invents a stub, and a Hub restart part way costs one batch, not the whole push.
    Prints the batch files in the order to post them."""
    doc = load(path)
    ents = doc["entities"]
    os.makedirs(out_dir, exist_ok=True)
    for f in os.listdir(out_dir):
        if re.match(r"^[ab]-\d+\.json$", f):
            os.remove(os.path.join(out_dir, f))
    files = []
    for phase in ("a", "b"):
        for n, i in enumerate(range(0, len(ents), size)):
            batch = ents[i:i + size]
            if phase == "a":
                batch = [dict(e, relations=[]) for e in batch]
            name = os.path.join(out_dir, f"{phase}-{n + 1:04d}.json")
            with open(name, "w", encoding="utf-8") as f:
                json.dump({"universe": doc["universe"], "entities": batch}, f, ensure_ascii=False)
            files.append(name)
    for name in files:
        print(name)
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
        return diff(argv[2], argv[3], "--allow-stale" in argv[4:])
    if cmd == "leak" and len(argv) >= 4:
        return leak(argv[2], argv[3], argv[4] if len(argv) >= 5 else None)
    if cmd == "chunk" and len(argv) >= 4:
        return chunk(argv[2], argv[3], int(argv[4]) if len(argv) >= 5 else 400)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
