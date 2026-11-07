#!/usr/bin/env python3
"""US-T236: SpaceGod's own check of the universe file the mapper (tools/universe-export.ps1) writes.

    python tools/prose_manifest.py [Data/Prose/spacegod.universe.json]

The Prose consumer kit's `validate` checks the interchange contract (stable ids, no duplicates, no dangling relations,
the schema). This adds the one SpaceGod-specific promise it cannot know: the `spacegod-export-manifest` entity's counts
(entities, relations, entities by type) match the file, and the file's `source` block names the same build and seed.
Exit 0 when it holds, 1 when it does not. Standard library only.
"""

import collections
import json
import sys

MANIFEST = "spacegod-export-manifest"


def check(doc) -> list:
    errors = []
    ents = doc.get("entities") or []
    m = next((e for e in ents if isinstance(e, dict) and e.get("id") == MANIFEST), None)
    if m is None:
        return [f"no {MANIFEST} entity (build hash and counts)"]
    d = m.get("details") or {}
    if d.get("entityCount") != len(ents):
        errors.append(f"manifest entityCount {d.get('entityCount')} != {len(ents)} entities in the file")
    edges = sum(len(e.get("relations") or []) for e in ents)
    if d.get("edgeCount") != edges:
        errors.append(f"manifest edgeCount {d.get('edgeCount')} != {edges} relations in the file")
    types = dict(collections.Counter(e.get("type") for e in ents))
    if d.get("entitiesByType") is not None and d["entitiesByType"] != types:
        diff = {t: (d["entitiesByType"].get(t), types.get(t)) for t in set(d["entitiesByType"]) | set(types)
                if d["entitiesByType"].get(t) != types.get(t)}
        errors.append(f"manifest entitiesByType differs from the file: {diff}")
    src = doc.get("source")
    if isinstance(src, dict):
        if src.get("build") != d.get("buildHash"):
            errors.append(f"source.build {src.get('build')} != manifest buildHash {d.get('buildHash')}")
        if src.get("seed") != d.get("seed"):
            errors.append(f"source.seed {src.get('seed')} != manifest seed {d.get('seed')}")
    return errors


def main(argv) -> int:
    path = argv[1] if len(argv) > 1 else "Data/Prose/spacegod.universe.json"
    with open(path, encoding="utf-8-sig") as f:
        doc = json.load(f)
    errors = check(doc)
    if errors:
        print(f"INVALID {path}")
        for e in errors:
            print("  " + e)
        return 1
    ents = doc.get("entities") or []
    print(f"manifest ok {path}: {len(ents)} entities, {sum(len(e.get('relations') or []) for e in ents)} relations")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
