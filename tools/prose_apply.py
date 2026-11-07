#!/usr/bin/env python3
"""US-T236: SpaceGod's applier for the Prose conduit (Prose docs/CONDUIT.md section 6, docs/PROSE.md here).

    python tools/prose_apply.py <pending-dir> --lore Data/Lore --universe Data/Prose/spacegod.universe.json [--dry-run]

The consumer kit (`kit.ps1 apply`) runs it from the repo root with the kit's pending/ folder full of approved or
published `prose-text/1` documents. For each one it writes the game's copy, Data/Lore/<kind>/<id>.json in the
`spacegod-text/1` shape below, and prints one JSON line to stdout:

    {"id": "...", "version": 3, "hash": "sha256:...", "result": "applied" | "orphan" | "skipped", "note": "..."}

The kit records the `applied` lines in Data/Prose/applied.jsonl and clears them from pending/; an orphan or a skipped
text stays pending. This script never writes applied.jsonl itself.

- applied: written (or already identical); a retired text's file is deleted.
- orphan:  the text's key names no entity of the universe file, so nothing in the game would ever look it up.
- skipped: the text cannot be applied as it is (not prose-text/1, a kind SpaceGod does not use, a key another applied
           text already holds, or no universe file to check the key against).

Keys (the game's lookup handle, which the C++ loader Core/Lore/LoreLibrary finds texts by): `<entity-id>` or
`<entity-id>/<name>`, where <entity-id> is an id of the universe file (planet-sol-earth, pilot-tomas-brennan/hail), or
`spacegod` / `spacegod/<name>` for a text about the game's universe as a whole.

spacegod-text/1 (one text per file; written only by this script, never by hand: an edit belongs in Prose):

    {"schema": "spacegod-text/1", "id", "kind", "key", "version", "hash", "about": [...], "speakers": [...], "payload": {...}}

Prose's own bookkeeping (status, approvedBy, approvedAt, updatedAt) is left out, so a text that goes from approved to
published unchanged writes the same bytes. Files are UTF-8, two-space indented, LF, with keys in that fixed order.

Standard library only. Exit code 0 unless it could not run at all (2).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

SCHEMA_IN = "prose-text/1"
SCHEMA_OUT = "spacegod-text/1"
KINDS = ("description", "book", "bark", "conversation")
STABLE_ID = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
KEY_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:/-]*$")
UNIVERSE_KEY = "spacegod"


def load(path: str):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def render(doc: dict) -> str:
    """The spacegod-text/1 file for a prose-text/1 document: fixed field order, deterministic bytes."""
    out = {
        "schema": SCHEMA_OUT,
        "id": doc["id"],
        "kind": doc["kind"],
        "key": doc["key"],
        "version": doc["version"],
        "hash": doc.get("hash", ""),
        "about": list(doc.get("about") or []),
        "speakers": list(doc.get("speakers") or []),
        "payload": doc.get("payload") or {},
    }
    return json.dumps(out, ensure_ascii=False, indent=2) + "\n"


def key_entity(key: str) -> str:
    return key.split("/", 1)[0]


def lore_path(lore: str, doc: dict) -> str:
    return os.path.join(lore, doc["kind"], doc["id"] + ".json")


def existing_keys(lore: str) -> dict:
    """key -> id of every text already applied under the Lore folder."""
    keys = {}
    for kind in KINDS:
        folder = os.path.join(lore, kind)
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            if not name.endswith(".json"):
                continue
            try:
                d = load(os.path.join(folder, name))
            except (OSError, ValueError):
                continue
            if isinstance(d, dict) and isinstance(d.get("key"), str) and isinstance(d.get("id"), str):
                keys.setdefault(d["key"], d["id"])
    return keys


def report(doc, result: str, note: str = "") -> dict:
    row = {"id": doc.get("id") if isinstance(doc, dict) else None,
           "version": doc.get("version") if isinstance(doc, dict) else None,
           "hash": doc.get("hash") if isinstance(doc, dict) else None,
           "result": result}
    if note:
        row["note"] = note
    return row


def check(doc) -> str:
    """Why this document cannot be applied, or "" when it can."""
    if not isinstance(doc, dict):
        return "not a JSON object"
    if doc.get("schema") != SCHEMA_IN:
        return f"schema is not {SCHEMA_IN}"
    if not isinstance(doc.get("id"), str) or not STABLE_ID.match(doc["id"]):
        return f"id '{doc.get('id')}' is not a stable slug"
    if doc.get("kind") not in KINDS:
        return f"kind '{doc.get('kind')}' is not one SpaceGod uses ({', '.join(KINDS)})"
    if not isinstance(doc.get("key"), str) or not KEY_RE.match(doc["key"]):
        return f"key '{doc.get('key')}' is not a valid key"
    if not isinstance(doc.get("version"), int) or doc["version"] < 1:
        return "version must be a positive integer"
    if doc.get("status") not in ("approved", "published", "retired"):
        return f"status '{doc.get('status')}' is not approved, published or retired (only those leave Prose)"
    if doc.get("status") != "retired" and not isinstance(doc.get("payload"), dict):
        return "payload must be an object"
    return ""


def write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def apply_all(pending: str, lore: str, universe_file: str | None, dry_run: bool, err=sys.stderr) -> list:
    """Applies every pending text; returns the report rows in the order applied (file name order)."""
    files = sorted(f for f in os.listdir(pending) if f.endswith(".json")) if os.path.isdir(pending) else []
    entities = None
    if universe_file and os.path.isfile(universe_file):
        entities = {e.get("id") for e in load(universe_file).get("entities") or [] if isinstance(e, dict)}
    keys = existing_keys(lore)
    rows = []
    for name in files:
        try:
            doc = load(os.path.join(pending, name))
        except (OSError, ValueError) as e:
            rows.append({"id": None, "version": None, "hash": None, "result": "skipped", "note": f"{name}: unreadable ({e})"})
            continue
        why = check(doc)
        if why:
            rows.append(report(doc, "skipped", why))
            continue
        path = lore_path(lore, doc)
        if doc["status"] == "retired":
            if os.path.isfile(path):
                if not dry_run:
                    os.remove(path)
                if keys.get(doc["key"]) == doc["id"]:
                    del keys[doc["key"]]
                rows.append(report(doc, "applied", "retired: removed " + os.path.relpath(path, lore).replace(os.sep, "/")))
            else:
                rows.append(report(doc, "applied", "retired: nothing to remove"))
            continue
        if entities is None:
            rows.append(report(doc, "skipped", "no universe file to check the key against (run kit map)"))
            continue
        ent = key_entity(doc["key"])
        if ent != UNIVERSE_KEY and ent not in entities:
            rows.append(report(doc, "orphan", f"key '{doc['key']}': no entity '{ent}' in the universe file"))
            continue
        holder = keys.get(doc["key"])
        if holder is not None and holder != doc["id"]:
            rows.append(report(doc, "skipped", f"key '{doc['key']}' is already held by applied text '{holder}'"))
            continue
        text = render(doc)
        old = None
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                old = f.read()
        if old == text:
            note = "unchanged"
        elif old is None:
            note = "new"
        else:
            try:
                prev = json.loads(old)
            except ValueError:
                prev = {}
            if prev.get("version") == doc["version"] and prev.get("hash") == doc.get("hash"):
                note = "overwrote a hand edit (edit the text in Prose instead)"
            else:
                note = f"updated v{prev.get('version')} -> v{doc['version']}"
        if old != text and not dry_run:
            write_text(path, text)
        keys[doc["key"]] = doc["id"]
        rows.append(report(doc, "applied", note))
    if entities is not None:
        described = {key_entity(k) for k, i in keys.items() if i.startswith("description-")}
        missing = len(entities - described)
        err.write(f"prose_apply: {len(keys)} text(s) in {lore}; {missing} of {len(entities)} universe entities have no "
                  "description yet and keep their placeholder\n")
    return rows


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="SpaceGod's Prose applier (docs/PROSE.md).")
    ap.add_argument("pending", nargs="?", default=os.environ.get("PROSE_KIT_PENDING", os.path.join("Data", "Prose", "pending")))
    ap.add_argument("--lore", default=os.path.join("Data", "Lore"))
    ap.add_argument("--universe", default=os.path.join("Data", "Prose", "spacegod.universe.json"))
    ap.add_argument("--dry-run", action="store_true", default=os.environ.get("PROSE_KIT_DRY_RUN") == "1")
    args = ap.parse_args(argv)
    try:
        rows = apply_all(args.pending, args.lore, args.universe, args.dry_run)
    except OSError as e:
        print(f"prose_apply: {e}", file=sys.stderr)
        return 2
    for r in rows:
        print(json.dumps(r, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
