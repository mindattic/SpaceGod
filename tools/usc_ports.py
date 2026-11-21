"""Blender step that finds the engine ports each Ultimate Spaceships Creator ship actually has (US-T432).

Run by tools/import-usc.ps1 after tools/usc_prepare.py (any Blender 3.3+, headless):
    blender -b --factory-startup --python tools/usc_ports.py -- <USC_FBX dir> <pieces dir> <out .inl>

The pack draws its engines as glowing nozzles: the faces of a ship whose texture is lit in the pack's emission map. A
port is a patch of such faces that faces aft (its exhaust leaves backwards), with nothing of the hull behind it (its
exhaust is clear), among the rearmost lit patches of the ship. Each is written in the game's model frame (one unit
long, centred on the ship's box, nose +Z, up +Y, the frame UscKit fits every hull to) as its middle, the way its
exhaust leaves and its radius, into a generated C++ table (Rendering/UscPorts.inl) that UscKit puts an EnginePort on
for every one. The table holds numbers only, nothing of the pack itself.

The modules the game bolts on (MODULES below) are probed the same way in their own frame, so a thruster bell's mouth
is known as well.
"""

import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def _models():
    """usc_prepare.py's MODELS (the ships the game uses, kept in step with UscKit.cpp), read without running it."""
    import ast
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "usc_prepare.py"), encoding="utf-8") as fh:
        tree = ast.parse(fh.read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "MODELS" for t in node.targets):
            return ast.literal_eval(node.value)
    raise SystemExit("usc_prepare.py has no MODELS")


MODELS = _models()

MODULES = ["GalacticLeopardThruster", "VoidWhaleThruster"]
LIT = 0.35          # an emission texel brighter than this (0..1, its brightest channel) is lit
AFT = 0.6           # a port's faces point at least this much aft
MERGE = 0.002       # faces whose corners are closer than this (ship lengths) touch: one patch
MIN_RADIUS = 0.006  # a patch smaller than this (ship lengths) is a light, not an engine
REAR_BAND = 0.2     # ports lie within this (ship lengths) of the rearmost one
SPLIT_Z = -0.06     # ShipMeshFactory::SplitZ: the cut a warship's pieces were capped at
DEBUG = bool(os.environ.get("USC_PORTS_DEBUG"))

_images = {}


def emission(src, family, atlas):
    key = (family, atlas)
    if key in _images:
        return _images[key]
    folder = os.path.join(src, family, "Textures")
    if atlas not in ("Hull", "Mod"):
        folder = os.path.join(folder, atlas)
    prefix = (family if atlas in ("Hull", "Mod") else f"{family}_{atlas}").lower()
    best = None
    for name in sorted(os.listdir(folder)):
        stem = os.path.splitext(name)[0].lower()
        for tag in ("_emission1", "_emission", "_emissive1", "_emissive"):
            if stem == prefix + tag and (best is None or best[0] > tag):
                best = (tag, os.path.join(folder, name))
    if best is None:
        _images[key] = None
        return None
    img = bpy.data.images.load(best[1])
    img.scale(1024, 1024)
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    lit = px.reshape(h, w, 4)[:, :, :3].max(axis=2)
    bpy.data.images.remove(img)
    _images[key] = lit
    return lit


def texel(lit, uv):
    h, w = lit.shape
    return lit[int((uv[1] % 1.0) * (h - 1)), int((uv[0] % 1.0) * (w - 1))]


def to_model(v):
    """The pieces' frame (nose -Y, up +Z) to the game's model frame (nose +Z, up +Y): see UscKit's PackToModel."""
    return Vector((-v.x, v.z, -v.y))


def faces(objs, lits):
    """Every triangle as (centre, normal, area, lit) in the model frame (unscaled), and the BVH of them all."""
    tris, verts, polys = [], [], []
    for o, lit in zip(objs, lits):
        me = o.data
        me.calc_loop_triangles()
        uvl = me.uv_layers.active.data if me.uv_layers.active else None
        mw = o.matrix_world
        for t in me.loop_triangles:
            vs = [to_model(mw @ me.vertices[i].co) for i in t.vertices]
            base = len(verts)
            verts.extend(vs)
            polys.append((base, base + 1, base + 2))
            n = (vs[1] - vs[0]).cross(vs[2] - vs[0])
            area = n.length * 0.5
            if area <= 0:
                continue
            # The frame change mirrors (x is negated), which reverses every triangle's winding.
            n = -n.normalized()
            c = (vs[0] + vs[1] + vs[2]) / 3
            b = 0.0
            if lit is not None and uvl is not None:
                uvs = [uvl[l].uv for l in t.loops]
                cu = (uvs[0] + uvs[1] + uvs[2]) / 3
                b = max(texel(lit, cu), min(texel(lit, u * 0.7 + cu * 0.3) for u in uvs))
            tris.append((c, n, area, b, vs))
    return tris, BVHTree.FromPolygons(verts, polys, epsilon=0.0)


def patches(hot, merge):
    """The faces that touch (share a corner, to within <merge>) as patches."""
    parent = list(range(len(hot)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    cells = {}
    for i, t in enumerate(hot):
        for v in t[4]:
            key = (round(v.x / merge), round(v.y / merge), round(v.z / merge))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        j = cells.get((key[0] + dx, key[1] + dy, key[2] + dz))
                        if j is not None:
                            parent[find(i)] = find(j)
            cells.setdefault(key, i)
    by_root = {}
    for i, t in enumerate(hot):
        by_root.setdefault(find(i), []).append(t)
    groups = list(by_root.values())
    out = []
    for g in groups:
        area = sum(t[2] for t in g)
        c = sum((t[0] * t[2] for t in g), Vector()) / area
        n = sum((t[1] * t[2] for t in g), Vector()).normalized()
        # Its extent across its axis, both ways: a nozzle is about as wide as it is tall, a lit stripe is not.
        u = n.cross(Vector((0, 1, 0)) if abs(n.y) < 0.9 else Vector((1, 0, 0))).normalized()
        w = n.cross(u)
        lo_u = lo_w = math.inf
        hi_u = hi_w = -math.inf
        for t in g:
            for v in t[4]:
                d = v - c
                lo_u, hi_u = min(lo_u, d.dot(u)), max(hi_u, d.dot(u))
                lo_w, hi_w = min(lo_w, d.dot(w)), max(hi_w, d.dot(w))
        a, b = hi_u - lo_u, hi_w - lo_w
        out.append((area, c, n, 0.5 * min(a, b), min(a, b) / max(a, b, 1e-9)))
    return out


def ports_of(objs, lits, warship):
    tris, bvh = faces(objs, lits)
    pts = [v for t in tris for v in t[4]]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    length = mx.z - mn.z
    centre = (mn + mx) / 2
    aft = Vector((0, 0, -1))

    def candidates(lit_only, facing):
        chosen = []
        for t in tris:
            if (lit_only and t[3] < LIT) or t[1].dot(aft) < facing:
                continue
            # The cap a warship's pieces were cut and closed with is not part of the ship.
            if warship and all(abs((v.z - centre.z) / length - SPLIT_Z) < 0.002 for v in t[4]):
                continue
            chosen.append(t)
        found = []
        for area, c, n, r, round_ in patches(chosen, MERGE * length):
            why = ""
            if r / length < MIN_RADIUS:
                why = "small"
            elif round_ < (0.3 if lit_only else 0.6):
                why = "a stripe" if lit_only else "not round"
            elif bvh.ray_cast(c + n * (0.004 * length), n, 2 * length)[0] is not None:
                why = "blocked"  # its exhaust would hit the hull
            if DEBUG:
                p = (c - centre) / length
                print(f"   {'lit' if lit_only else 'geo'} ({p.x:+.3f},{p.y:+.3f},{p.z:+.3f}) n=({n.x:+.2f},{n.y:+.2f},{n.z:+.2f}) r={r / length:.4f} round={round_:.2f} {why}")
            if not why:
                found.append(((c - centre) / length, n, r / length, round_))
        if not found:
            return found
        # Engines sit at the stern: a lit patch far up the hull is a light. Of the stern's own faces, the rearmost few.
        rear = -0.5 if lit_only else min(p[0].z for p in found)
        found = [p for p in found if p[0].z <= rear + (REAR_BAND if lit_only else REAR_BAND * 0.5)]
        if lit_only:
            # A nozzle drawn in pieces (a glowing core inside a lit ring) is one port: the bigger one keeps it.
            found.sort(key=lambda p: -p[2])
            kept = []
            for p in found:
                if all((p[0] - k[0]).length > k[2] for k in kept):
                    kept.append(p)
            return kept
        # Unlit: the roundest, rearmost aft faces, none overlapping another, at most four, a nozzle fitted inside each.
        found.sort(key=lambda p: -(p[3] - 2 * (p[0].z - rear)))
        kept = []
        for p in found:
            if len(kept) < 4 and all(math.hypot(p[0].x - k[0].x, p[0].y - k[0].y) > p[2] + k[2] for k in kept):
                kept.append(p)
        return [(p, n, min(r * 0.7, 0.05), q) for p, n, r, q in kept]

    # Its glowing nozzles; a ship that has none lit (some small craft) fires from the aft faces of its stern instead.
    found = candidates(True, AFT) or candidates(False, 0.9)
    found = [(p, n, r) for p, n, r, _ in found]
    found.sort(key=lambda p: (round(p[0].z, 3), p[0].x, p[0].y))
    return found


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    src, pieces, out = args[0], args[1], args[2]
    rows = []
    only = os.environ.get("USC_PORTS_ONLY")
    for model, (family, warship) in MODELS.items():
        if only and model not in only.split(","):
            continue
        files = sorted(f for f in os.listdir(pieces) if f.startswith(f"SM_USC_{model}_") and f.lower().endswith(".fbx"))
        if not files:
            print(f"USC ports: {model}: no pieces")
            continue
        objs, lits = [], []
        for f in files:
            atlas = f[len(f"SM_USC_{model}_"):].split("_")[0]
            before = set(bpy.data.objects)
            if not objs:
                bpy.ops.wm.read_factory_settings(use_empty=True)
                _images.clear()
                before = set()
            bpy.ops.import_scene.fbx(filepath=os.path.join(pieces, f))
            for o in bpy.data.objects:
                if o.type == "MESH" and o not in before:
                    objs.append(o)
                    lits.append(emission(src, family, atlas))
        found = ports_of(objs, lits, warship)
        print(f"USC ports: {model}: {len(found)}")
        rows.append((model, found))
    for module in MODULES:
        family = module[: -len("Thruster")]
        path = os.path.join(pieces, f"SM_USC_Mod_{module}.fbx")
        if not os.path.exists(path):
            continue
        bpy.ops.wm.read_factory_settings(use_empty=True)
        _images.clear()
        bpy.ops.import_scene.fbx(filepath=path)
        objs = [o for o in bpy.data.objects if o.type == "MESH"]
        lit = emission(src, family, "Parts")
        tris, _ = faces(objs, [lit] * len(objs))
        pts = [v for t in tris for v in t[4]]
        size = tuple(round(max(getattr(p, a) for p in pts) - min(getattr(p, a) for p in pts), 3) for a in "xyz")
        for t in sorted(tris, key=lambda t: -t[2] * t[3])[:1]:
            print(f"USC module {module}: size {size}, brightest face n=({t[1].x:+.2f},{t[1].y:+.2f},{t[1].z:+.2f}) at {tuple(round(v, 3) for v in t[0])}")
    with open(out, "w", newline="\n") as fh:
        fh.write("// Generated by tools/usc_ports.py (US-T432) from the Ultimate Spaceships Creator pack: do not edit.\n")
        fh.write("// Each ship's engine ports in the model frame (one unit long, centred, nose +Z, up +Y): middle, exhaust, radius.\n")
        for model, found in rows:
            items = ", ".join(
                f"{{ {{ {p.x:.4f}f, {p.y:.4f}f, {p.z:.4f}f }}, {{ {n.x:.3f}f, {n.y:.3f}f, {n.z:.3f}f }}, {r:.4f}f }}" for p, n, r in found)
            fh.write(f'USC_PORTS("{model}", {{ {items} }})\n')
    print(f"USC ports: wrote {out}")


main()
