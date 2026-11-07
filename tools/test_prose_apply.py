"""US-T236: tests of tools/prose_apply.py, SpaceGod's Prose applier (docs/PROSE.md), end to end on a fixture pending/
folder, with no Hub and no approval needed.

    python -m unittest tools/test_prose_apply.py        (from the repo root)

The fixture lives beside the C++ loader's test (Cpp/Source/SpaceGodCore/Tests/Lore/fixture): pending/ holds
prose-text/1 documents as `kit pull` writes them, spacegod.universe.json a four-entity universe, and Lore/ the exact
files the applier must write, which Tests/Lore/LoreLibraryTests.cpp then loads. So the two tests pin the whole return
path: pulled text -> applier -> Data/Lore -> the game's loader.
"""

import io
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import prose_apply  # noqa: E402

FIXTURE = os.path.join(os.path.dirname(HERE), "Cpp", "Source", "SpaceGodCore", "Tests", "Lore", "fixture")
PENDING = os.path.join(FIXTURE, "pending")
UNIVERSE = os.path.join(FIXTURE, "spacegod.universe.json")
EXPECTED = os.path.join(FIXTURE, "Lore")


def tree(root):
    out = {}
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, root).replace(os.sep, "/")] = fh.read()
    return out


class ApplierTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="prose-apply-")
        self.lore = os.path.join(self.tmp, "Lore")
        # A text applied earlier that Prose has since retired (pending holds its retired v3).
        europa = os.path.join(self.lore, "description", "description-moon-sol-europa.json")
        os.makedirs(os.path.dirname(europa))
        with open(europa, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"schema": "spacegod-text/1", "id": "description-moon-sol-europa", "kind": "description",
                       "key": "moon-sol-europa", "version": 2, "hash": "", "about": [], "speakers": [],
                       "payload": {"short": "old"}}, f)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def apply(self, universe=UNIVERSE, dry_run=False):
        err = io.StringIO()
        rows = prose_apply.apply_all(PENDING, self.lore, universe, dry_run, err=err)
        return {r["id"]: r for r in rows}, err.getvalue()

    def test_writes_exactly_the_fixture_lore(self):
        rows, err = self.apply()
        self.assertEqual(tree(self.lore), tree(EXPECTED))
        self.assertIn("keep their placeholder", err)
        self.assertEqual(rows["description-planet-sol-earth"]["result"], "applied")
        self.assertEqual(rows["bark-pilot-tomas-brennan-hail"]["result"], "applied")
        self.assertEqual(rows["conversation-pilot-tomas-brennan-greeting"]["result"], "applied")

    def test_reports_follow_the_kit_contract(self):
        rows, _ = self.apply()
        for r in rows.values():
            self.assertIn(r["result"], ("applied", "orphan", "skipped"))
            self.assertIsInstance(r["version"], int)
            self.assertTrue(r["hash"].startswith("sha256:"))
        self.assertEqual(rows["description-planet-sol-vulcan"]["result"], "orphan")
        self.assertEqual(rows["crawl-sector-sol"]["result"], "skipped")

    def test_retired_text_is_removed(self):
        rows, _ = self.apply()
        self.assertEqual(rows["description-moon-sol-europa"]["result"], "applied")
        self.assertIn("retired: removed", rows["description-moon-sol-europa"]["note"])
        self.assertFalse(os.path.exists(os.path.join(self.lore, "description", "description-moon-sol-europa.json")))

    def test_second_apply_changes_nothing(self):
        self.apply()
        before = tree(self.lore)
        rows, _ = self.apply()
        self.assertEqual(tree(self.lore), before)
        self.assertEqual(rows["description-planet-sol-earth"]["note"], "unchanged")

    def test_hand_edit_is_overwritten_and_reported(self):
        self.apply()
        path = os.path.join(self.lore, "description", "description-planet-sol-earth.json")
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        doc["payload"]["short"] = "Edited by hand."
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f)
        rows, _ = self.apply()
        self.assertIn("hand edit", rows["description-planet-sol-earth"]["note"])
        self.assertEqual(tree(self.lore), tree(EXPECTED))

    def test_dry_run_writes_nothing(self):
        before = tree(self.lore)
        rows, _ = self.apply(dry_run=True)
        self.assertEqual(tree(self.lore), before)
        self.assertEqual(rows["description-planet-sol-earth"]["result"], "applied")

    def test_no_universe_file_skips_rather_than_guessing(self):
        rows, _ = self.apply(universe=os.path.join(self.tmp, "missing.json"))
        self.assertEqual(rows["description-planet-sol-earth"]["result"], "skipped")
        self.assertFalse(os.path.exists(os.path.join(self.lore, "description", "description-planet-sol-earth.json")))

    def test_key_held_by_another_text_is_skipped(self):
        other = os.path.join(self.lore, "description", "description-earth-old.json")
        with open(other, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"schema": "spacegod-text/1", "id": "description-earth-old", "kind": "description",
                       "key": "planet-sol-earth", "version": 1, "hash": "", "payload": {"short": "x"}}, f)
        rows, _ = self.apply()
        self.assertEqual(rows["description-planet-sol-earth"]["result"], "skipped")


if __name__ == "__main__":
    unittest.main()
