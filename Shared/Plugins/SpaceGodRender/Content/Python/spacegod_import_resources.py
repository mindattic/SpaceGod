"""Imports what Unity.SpaceGod's views load from Resources into the shared plugin's content (US-T27).

Unity.SpaceGod/Assets/UnitySpace/Resources (read-only source) -> /SpaceGodRender/Resources/..., where the C# track's
Unity facade (CSharp/Script/.../UnityCompat/UnityResources.cs, Resources.Roots) finds "Textures/earth_color" as
/SpaceGodRender/Resources/Textures/earth_color:
  - Textures/*.jpg|png   planet/moon colour and relief maps, the hull plating; Unity's import settings kept per file
                         (from its .meta: maxTextureSize, sRGB; relief maps and the plating raw, not BC5 normal maps,
                         as SpaceGodRender's README asks).
  - Skyboxes/*.jpg       the panoramas (8k), wrap U / clamp V, as Unity's.
  - Ships/*.glb          the five Kenney Space Kit craft (CC0) as static meshes; their glTF material names and base
                         colours, and the rotation/scale that brings Unreal's import back to the mesh Unity's glTF
                         importer made (one unit long on +Z, nose +Z), go to CSharp/Content/SpaceGod/Data/ShipModels.json.
  - Data/asteroids.bytes copied as is to CSharp/Content/SpaceGod/Data (read as a file; staged by DefaultGame.ini).
Commit the .uasset files and the Data folder. Re-running skips what exists (pass --force to re-import).

Run headless (one UE process, no UI):
  UnrealEditor-Cmd <repo>/CSharp/UnrealSpaceGodCSharp.uproject -run=pythonscript
      -script="<repo>/Shared/Plugins/SpaceGodRender/Content/Python/spacegod_import_resources.py" -unattended -nosplash
Set UNITY_SPACEGOD_DIR to the Unity checkout if it is not next to this repo.
"""
import json
import os
import re
import shutil
import sys

import unreal

ROOT = "/SpaceGodRender/Resources"
eal = unreal.EditorAssetLibrary
FORCE = "--force" in sys.argv or os.environ.get("SPACEGOD_FORCE_IMPORT") == "1"


def _repo():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))


REPO = _repo()
UNITY = os.environ.get("UNITY_SPACEGOD_DIR") or os.path.normpath(os.path.join(REPO, "..", "Unity.SpaceGod"))
SOURCE = os.path.join(UNITY, "Assets", "UnitySpace", "Resources")
DATA_OUT = os.path.join(REPO, "CSharp", "Content", "SpaceGod", "Data")


def _meta(path):
    """Unity's import settings for one file: (maxTextureSize, sRGB)."""
    size, srgb = 2048, True
    try:
        with open(path + ".meta", "r", encoding="utf-8") as f:
            text = f.read()
        m = re.search(r"maxTextureSize:\s*(\d+)", text)
        if m:
            size = int(m.group(1))
        m = re.search(r"sRGBTexture:\s*(\d)", text)
        if m:
            srgb = m.group(1) == "1"
    except OSError:
        pass
    return size, srgb


def _import(files, destination):
    tasks = []
    for file in files:
        name = os.path.splitext(os.path.basename(file))[0]
        if not FORCE and eal.does_asset_exist(f"{destination}/{name}"):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", file)
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        task.set_editor_property("replace_existing", True)
        tasks.append((name, file, task))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, _, t in tasks])
    return [(name, file) for name, file, _ in tasks]


def import_textures(folder, wrap_v_clamp):
    source = os.path.join(SOURCE, folder)
    files = [os.path.join(source, f) for f in sorted(os.listdir(source)) if f.lower().endswith((".jpg", ".jpeg", ".png"))]
    destination = f"{ROOT}/{folder}"
    done = _import(files, destination)
    for name, file in done:
        path = f"{destination}/{name}"
        tex = unreal.load_asset(path)
        if tex is None:
            unreal.log_error(f"SpaceGod import: {path} did not import")
            continue
        size, srgb = _meta(file)
        tex.set_editor_property("srgb", srgb)
        # Relief maps and the plating are read as raw data by the shaders (README: not BC5 normal maps).
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("max_texture_size", size)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP if wrap_v_clamp else unreal.TextureAddress.TA_WRAP)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        eal.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"SpaceGod import: {len(done)} of {len(files)} {folder} texture(s) imported")


def _glb(file):
    with open(file, "rb") as f:
        data = f.read()
    length = int.from_bytes(data[12:16], "little")
    return json.loads(data[20:20 + length].decode("utf-8"))


def import_ships():
    """Each Kenney glb is four nodes (identity transforms), one mesh and one material each. Unity's glTF import makes a
    GameObject with one child renderer per node, in node order; Interchange makes one static mesh per node (the first
    named after the file, the rest <file>_node_<i> under <file>/StaticMeshes). ShipModels.json lists those parts in node
    order with their glTF material (name, baseColorFactor), so the C# Resources facade rebuilds Unity's hierarchy."""
    source = os.path.join(SOURCE, "Ships")
    files = [os.path.join(source, f) for f in sorted(os.listdir(source)) if f.lower().endswith(".glb")]
    destination = f"{ROOT}/Ships"
    _import(files, destination)
    models = {}
    for file in files:
        name = os.path.splitext(os.path.basename(file))[0]
        doc = _glb(file)
        materials = doc.get("materials", [])
        nodes = [n for n in doc["scenes"][doc.get("scene", 0)]["nodes"]]
        parts = []
        k = None
        for order, node_index in enumerate(nodes):
            node = doc["nodes"][node_index]
            path = f"{destination}/{name}" if order == 0 else f"{destination}/{name}/StaticMeshes/{name}_node_{order}"
            mesh = unreal.load_asset(path)
            if not isinstance(mesh, unreal.StaticMesh):
                unreal.log_error(f"SpaceGod import: no static mesh {path} for node {node_index} of {name}")
                continue
            prim = doc["meshes"][node["mesh"]]["primitives"][0]
            mat = materials[prim["material"]] if "material" in prim else {"name": "default"}
            color = mat.get("pbrMetallicRoughness", {}).get("baseColorFactor", [1, 1, 1, 1])
            if k is None:
                acc = doc["accessors"][prim["attributes"]["POSITION"]]
                gltf_z = acc["max"][2] - acc["min"][2]
                box = mesh.get_bounding_box()
                ue_y = box.max.y - box.min.y
                k = ue_y / gltf_z if gltf_z > 0 else 1.0
            parts.append({"mesh": f"{path}.{path.rsplit('/', 1)[1]}", "material": mat.get("name", "default"),
                          "baseColor": [float(c) for c in color]})
            eal.save_asset(path, only_if_is_dirty=False)
        # Unity's import (glTFast) is glTF with x negated: (-x, y, z), nose +Z (AssetForge). Interchange's is glTF as
        # (x, z, y) (GLTFCore ConversionUtilities ConvertVec3) times its unit scale k. With UnrealSpace
        # (Unreal = Unity (x, -z, y)), the rotation C with P*R*C*P^-1 * M = P*R (M: Unity -> this import) is
        # C = M * P = diag(-1, 1, -1): half a turn about Unity's Y; the scale undoes k.
        models[name] = {"parts": parts, "correctionEuler": [0.0, 180.0, 0.0], "correctionScale": 1.0 / (k or 1.0)}
    os.makedirs(DATA_OUT, exist_ok=True)
    with open(os.path.join(DATA_OUT, "ShipModels.json"), "w", encoding="utf-8") as f:
        json.dump(models, f, indent=2)
    unreal.log(f"SpaceGod import: {len(models)} ship model(s), ShipModels.json written")

def copy_data():
    os.makedirs(DATA_OUT, exist_ok=True)
    src = os.path.join(SOURCE, "Data", "asteroids.bytes")
    dst = os.path.join(DATA_OUT, "asteroids.bytes")
    if FORCE or not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
        shutil.copyfile(src, dst)
    unreal.log("SpaceGod import: Data/asteroids.bytes in place")


if not os.path.isdir(SOURCE):
    unreal.log_error(f"SpaceGod import: no Unity.SpaceGod Resources at {SOURCE} (set UNITY_SPACEGOD_DIR)")
else:
    import_textures("Textures", wrap_v_clamp=True)
    import_textures("Skyboxes", wrap_v_clamp=True)
    import_ships()
    copy_data()
    eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    unreal.log("SpaceGod import: done")
