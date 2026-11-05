"""Blender step of the Ultimate Spaceships Creator import (US-T142): cuts the pack's FBX ships into the pieces the game draws.

Run by tools/import-usc.ps1 (any Blender 3.3+, headless, FBX in and out only):
    blender -b --factory-startup --python tools/usc_prepare.py -- <USC_FBX dir> <out dir>

The pack is third-party art that must not be redistributed: <USC_FBX dir> and <out dir> are both under the git-ignored
Cpp/SourceArt/ThirdParty/USC/. Nothing from the pack is executed; its FBX files are read as data.

For every ship the game uses (MODELS, kept in step with Rendering/UscKit.cpp) it writes one FBX per piece, all in one
frame: the whole ship centred on its bounding box, nose -Y, up +Z, in the pack's metres. A ship of two meshes (Galactic
Leopard and Void Whale: Main and Parts, each on its own texture atlas) gives a piece per mesh. A warship is cut across
its length where ShipMeshFactory::SplitZ puts the break (6 % of its length aft of the middle) and the cut is capped, so
its bow and stern are separate pieces that meet exactly and a destroyed ship breaks in two:
    SM_USC_<Model>_<Main|Parts|Hull>_<Whole|Bow|Stern>.fbx
Each module the game bolts on (MODULES: turret bases, barrels, thruster bells, cargo) is written centred on its own box:
    SM_USC_Mod_<Module>.fbx
"""

import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

SPLIT_AFT = 0.06  # ShipMeshFactory::SplitZ = -0.06 ship lengths (aft of the middle)

# Model -> (family folder, warship: cut into bow and stern). Kept in step with Rendering/UscKit.cpp.
MODELS = {
    # Navy capitals (Galactic Leopard) and their pirate counterparts (Void Whale).
    "GalacticLeopard31": ("GalacticLeopard", True),   # battleship
    "GalacticLeopard44": ("GalacticLeopard", True),   # carrier
    "GalacticLeopard39": ("GalacticLeopard", True),   # gate breaker
    "GalacticLeopard41": ("GalacticLeopard", True),   # cruiser
    "GalacticLeopard27": ("GalacticLeopard", True),   # command cruiser
    "GalacticLeopard43": ("GalacticLeopard", True),   # destroyer
    "GalacticLeopard40": ("GalacticLeopard", True),   # frigate
    "GalacticLeopard32": ("GalacticLeopard", True),   # corvette
    "VoidWhale30": ("VoidWhale", True),
    "VoidWhale10": ("VoidWhale", True),
    "VoidWhale27": ("VoidWhale", True),
    "VoidWhale8": ("VoidWhale", True),
    "VoidWhale25": ("VoidWhale", True),
    "VoidWhale16": ("VoidWhale", True),
    "VoidWhale20": ("VoidWhale", True),
    "VoidWhale14": ("VoidWhale", True),
    # Civil and auxiliary hulls.
    "GalacticLeopard11": ("GalacticLeopard", False),  # truss freighter
    "GalacticLeopard16": ("GalacticLeopard", False),  # lane tender
    "GalacticLeopard7": ("GalacticLeopard", False),  # gate picket
    "GalacticLeopard13": ("GalacticLeopard", False),  # explorer
    "VoidWhale40": ("VoidWhale", False),              # miner
    "VoidWhale24": ("VoidWhale", False),              # ore tug
    "VoidWhale31": ("VoidWhale", False),              # capture tug
    # Small craft.
    "StriderOx14": ("StriderOx", False),    # gunship
    "StriderOx10": ("StriderOx", False),    # pirate gunship
    "StriderOx18": ("StriderOx", False),    # assault shuttle
    "StriderOx01": ("StriderOx", False),    # pirate assault shuttle
    "NightAye20": ("NightAye", False),      # bomber
    "NightAye09": ("NightAye", False),      # pirate bomber
    "NightAye12": ("NightAye", False),      # pirate recon
    "HyperFalcon17": ("HyperFalcon", False),  # interceptor
    "HyperFalcon08": ("HyperFalcon", False),  # pirate interceptor
    "HyperFalcon07": ("HyperFalcon", False),  # fighter
    "HyperFalcon10": ("HyperFalcon", False),  # pirate fighter
    "HyperFalcon19": ("HyperFalcon", False),  # recon
    "HyperFalcon14": ("HyperFalcon", False),  # courier
    "HyperFalcon24": ("HyperFalcon", False),  # courier
    "HyperFalcon20": ("HyperFalcon", False),  # skiff
    "ForceBadger17": ("ForceBadger", False),  # taxi
    "ForceBadger04": ("ForceBadger", False),  # runabout
    "ForceBadger14": ("ForceBadger", False),  # tourer
    "ForceBadger11": ("ForceBadger", False),  # bus
}

# Module FBX -> the parts taken from it.
MODULES = {
    ("GalacticLeopard", "GalacticLeopard_Parts_Modules"): ["Turret", "Gun", "Thruster", "Cargo", "Silo", "Cylinder"],
    ("VoidWhale", "VoidWhale_Parts_Modules"): ["TurretBase", "Cannon", "Lazer", "Thruster", "Dish", "Engine"],
}

# US-T143: Ebal Studios' Space Station Creator, the same way (tools/import-ssc.ps1 passes "ssc" as the third argument).
# Its stations come whole (none is cut), each mesh painted from one of four atlases (a Combined station has one mesh per
# atlas: Bridge, Cylinderical, Hangars, Storage). Kept in step with Rendering/SscKit.cpp.
# US-T149: only complete prebuilt stations, each used whole for one role (no parts to assemble our own from).
SSC_MODELS = {
    "Combined20": ("Combined", False),  # Void Stations (the largest)
    "Combined28": ("Combined", False),  # megastructures
    "Combined30": ("Combined", False),  # free-flying stations (Rigil Gate, Toliman Forge, Blackhollow ...)
    "Combined08": ("Combined", False),
    "Combined01": ("Combined", False),
    "Combined16": ("Combined", False),  # derelicts
    "Combined24": ("Combined", False),
    "Combined10": ("Combined", False),
    "Combined21": ("Combined", False),  # factories
    "Storage01": ("Storage", False),
    "Storage04": ("Storage", False),
    "Storage11": ("Storage", False),
    "Storage18": ("Storage", False),    # depots (the tank farm)
    "Cylinderical12": ("Cylinderical", False),  # habitats (the wheel)
    "Cylinderical18": ("Cylinderical", False),  # gateways (the gyroscope ring)
}
SSC_MODULES = {}
SSC_ATLASES = ("Bridge", "Cylinderical", "Hangars", "Storage")


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    reset()
    bpy.ops.import_scene.fbx(filepath=path)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in bpy.data.objects:
        o.select_set(o.type == "MESH")
    if meshes:
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return meshes


def bounds(objs):
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return mn, mx


def shift(obj, by):
    obj.data.transform(Matrix.Translation(by))
    obj.data.update()


def cut(obj, y, keep_bow):
    """Keeps the part of obj ahead of (keep_bow: y < cut) or behind the plane y = cut, the opening capped."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    res = bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0, y, 0), plane_no=(0, 1, 0),
                                 clear_outer=keep_bow, clear_inner=not keep_bow)
    edges = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
    if edges:
        bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    return len(obj.data.polygons) > 0


def export(objs, path):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_ALL", bake_space_transform=False, mesh_smooth_type="FACE",
                             use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False, path_mode="STRIP")


def atlas_of(name):
    if "_Main" in name:
        return "Main"
    if "_Parts" in name:
        return "Parts"
    return "Hull"


def find_fbx(folder, stem):
    for n in os.listdir(folder):
        if os.path.splitext(n)[0].lower() == stem.lower() and n.lower().endswith(".fbx"):
            return os.path.join(folder, n)
    # The pack names some ships without the leading zero (GalacticLeopard1) and some with it (HyperFalcon01).
    digits = "".join(c for c in stem if c.isdigit())
    base = stem[: len(stem) - len(digits)]
    for alt in (base + str(int(digits)), base + digits.zfill(2)):
        for n in os.listdir(folder):
            if os.path.splitext(n)[0].lower() == alt.lower() and n.lower().endswith(".fbx"):
                return os.path.join(folder, n)
    return None


def ssc_atlas_of(name):
    """Combined08_Bridge -> Bridge; Storage01 -> Hull (a single-set station is painted from its set's atlas)."""
    tail = name.split(".")[0].split("_")[-1]
    return tail if tail in SSC_ATLASES else "Hull"


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    src, out = args[0], args[1]
    ssc = len(args) > 2 and args[2].lower() == "ssc"
    tag = "SSC" if ssc else "USC"
    models, modules = (SSC_MODELS, SSC_MODULES) if ssc else (MODELS, MODULES)
    os.makedirs(out, exist_ok=True)
    written, failed = 0, []
    for model, (family, warship) in models.items():
        path = find_fbx(os.path.join(src, family), model)
        if not path:
            failed.append(f"{model}: no FBX in {family}")
            continue
        meshes = import_fbx(path)
        if not meshes:
            failed.append(f"{model}: no meshes")
            continue
        mn, mx = bounds(meshes)
        centre = (mn + mx) * 0.5
        for o in meshes:
            shift(o, -centre)
        length = mx.y - mn.y
        split_y = SPLIT_AFT * length  # the nose is -Y, so aft of the middle is +Y
        for o in meshes:
            atlas = ssc_atlas_of(o.name) if ssc else atlas_of(o.name)
            if not warship:
                export([o], os.path.join(out, f"SM_{tag}_{model}_{atlas}_Whole.fbx"))
                written += 1
                continue
            data = o.data
            for section, keep_bow in (("Bow", True), ("Stern", False)):
                piece = o.copy()
                piece.data = data.copy()
                bpy.context.scene.collection.objects.link(piece)
                if cut(piece, split_y, keep_bow):
                    export([piece], os.path.join(out, f"SM_USC_{model}_{atlas}_{section}.fbx"))
                    written += 1
                bpy.data.objects.remove(piece)
        print(f"{tag}: {model} {length:.1f} m, {len(meshes)} mesh(es)")

    for (family, stem), parts in modules.items():
        path = find_fbx(os.path.join(src, family), stem)
        if not path:
            failed.append(f"{stem}: no FBX")
            continue
        meshes = import_fbx(path)
        for part in parts:
            name = f"{family}_{part}" if ssc else f"{family}_Parts_{part}"
            obj = next((o for o in meshes if o.name == name or o.name.startswith(name + ".")), None)
            if obj is None:
                failed.append(f"module {name} not found")
                continue
            mn, mx = bounds([obj])
            shift(obj, -(mn + mx) * 0.5)
            export([obj], os.path.join(out, f"SM_{tag}_Mod_{family}{part}.fbx"))
            written += 1
    print(f"{tag}: SUMMARY pieces={written} failures={len(failed)}")
    for f in failed:
        print(f"{tag}: FAILURE {f}")


main()
