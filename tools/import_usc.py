"""Headless import of Ebal Studios' Ultimate Spaceships Creator into /Game/ThirdParty/USC (US-T142).

The pack is third-party art from the owner's library and must not be redistributed: neither its files
(Cpp/SourceArt/ThirdParty/USC/) nor the imported assets (Cpp/Content/ThirdParty/USC/) are committed. Run through
tools/import-usc.ps1, which first cuts the ships into pieces with Blender (tools/usc_prepare.py) and then runs
    UnrealEditor-Cmd Cpp/SpaceGod.uproject -run=pythonscript -script=<abs>/tools/import_usc.py
with USC_SOURCE (the pack's USC_FBX folder) and USC_PIECES (the prepared pieces) set. Re-running is safe: meshes,
textures and materials are re-imported or rebuilt in place, so references to them survive.

What it builds (see docs/ThirdPartyArt.md):
  Meshes/SM_USC_<Model>_<Main|Parts|Hull>_<Whole|Bow|Stern>   a ship's pieces in one frame (centred, nose -Y in the
                                                              pack, metres -> cm); warships cut at SplitZ
  Meshes/SM_USC_Mod_<Family><Part>                            modules (turret bases, barrels, bells, cargo), centred
  Textures/T_USC_<Atlas>_<Map>                                per atlas: the unified colours, normal, metallic,
                                                              roughness and emission (2048 at most)
  Materials/M_USCShip                                         the in-game material (lit from MPC_SpaceGod, unlit)
  Materials/MI_USC_<Atlas>_<Colour>                           one per atlas and unified colour (Grey, Red, Blue,
                                                              Green, Purple, White, Black when the set has it)
"""

import os

import unreal

ROOT = "/Game/ThirdParty/USC"
MESHES = ROOT + "/Meshes"
TEXTURES = ROOT + "/Textures"
MATERIALS = ROOT + "/Materials"
GAME_MATERIAL = "M_USCShip"
MPC = "/SpaceGodRender/Materials/MPC_SpaceGod"

# Atlas -> family folder. Galactic Leopard and Void Whale paint their hull (Main) and fittings (Parts) from two atlases.
ATLASES = {
    "GalacticLeopard_Main": "GalacticLeopard",
    "GalacticLeopard_Parts": "GalacticLeopard",
    "VoidWhale_Main": "VoidWhale",
    "VoidWhale_Parts": "VoidWhale",
    "StriderOx": "StriderOx",
    "NightAye": "NightAye",
    "HyperFalcon": "HyperFalcon",
    "ForceBadger": "ForceBadger",
}
COLOURS = ["Grey", "Red", "Blue", "Green", "Purple", "White", "Black"]
# Each map and the file stems that hold it (the sets name them differently).
MAPS = {
    "Normal": ["Normal"],
    "Metallic": ["Metallic"],
    "Roughness": ["Roughness"],
    "Glossiness": ["Glossiness"],
    "Emission": ["Emission1", "Emission", "Emissive1", "Emissive"],
}

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
failures = []


def _log(msg):
    unreal.log(f"USC: {msg}")


def _fail(msg):
    failures.append(msg)
    unreal.log_error(f"USC: {msg}")


# ---------------------------------------------------------------- textures

def _index(src, family):
    """{lower-case file stem: path} of a set's textures (Masks and Bonus folders left out)."""
    found = {}
    for dirpath, _, names in os.walk(os.path.join(src, family, "Textures")):
        if any(part in ("Masks", "Bonus") for part in dirpath.replace("\\", "/").split("/")):
            continue
        for n in names:
            if n.lower().endswith(".png"):
                found.setdefault(os.path.splitext(n)[0].lower(), os.path.join(dirpath, n))
    return found


def _pick(index, atlas, stems):
    for stem in stems:
        hit = index.get(f"{atlas}_{stem}".lower())
        if hit:
            return hit
    return None


def textures_of(src):
    """{atlas: {map or colour: file}} for every atlas."""
    out = {}
    for atlas, family in ATLASES.items():
        index = _index(src, family)
        maps = {}
        for colour in COLOURS:
            path = _pick(index, atlas, [colour] + (["Gray"] if colour == "Grey" else []))
            if path:
                maps[colour] = path
        for name, stems in MAPS.items():
            path = _pick(index, atlas, stems)
            if path:
                maps[name] = path
        if "Grey" not in maps or "Normal" not in maps:
            _fail(f"atlas {atlas}: no Grey or Normal texture in {family}/Textures")
        out[atlas] = maps
    return out


def import_textures(atlases):
    tasks, wanted = [], []
    for atlas, maps in atlases.items():
        for key, path in maps.items():
            name = f"T_USC_{atlas}_{key}"
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", path)
            task.set_editor_property("destination_path", TEXTURES)
            task.set_editor_property("destination_name", name)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", False)
            tasks.append(task)
            wanted.append((f"{TEXTURES}/{name}", key))
    tools.import_asset_tasks(tasks)
    count = 0
    for asset, key in wanted:
        tex = unreal.load_asset(asset)
        if tex is None:
            _fail(f"texture import failed: {asset}")
            continue
        # The pack's maps are 4096 and up: 2048 is plenty at the distances ships are seen from.
        tex.set_editor_property("max_texture_size", 2048)
        if key == "Normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif key in ("Metallic", "Roughness", "Glossiness"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        else:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property("srgb", True)
        eal.save_asset(asset, only_if_is_dirty=False)
        count += 1
    _log(f"imported {count}/{len(wanted)} texture(s)")
    return count


def _tex(atlas, key):
    return unreal.load_asset(f"{TEXTURES}/T_USC_{atlas}_{key}")


# ---------------------------------------------------------------- the in-game material

# The game draws with no Unreal lights: every material is unlit and lights itself from the star(s) in MPC_SpaceGod
# (as SpaceGodRender's M_Ship and the Star Sparrow kit's M_StarSparrowShip do). This is a USC atlas in those terms: the
# painted unified colour times the faction's Tint, the normal, metallic, roughness and emission maps, lit by the
# star(s) with a cool fill on the night side, then SG_Expose3. ShipView's property block sets _Tint (a wreck's char),
# _Highlight and _EngineGlow; Rendering/UscKit sets Tint per faction.
SHIP_CODE = r"""
float3 albedo = Texture2DSample(BaseColor, BaseColorSampler, UV).rgb * Tint * _Tint;
float metallic = Texture2DSample(Metallic, MetallicSampler, UV).r;
float rough = Texture2DSample(Roughness, RoughnessSampler, UV).r;
rough = lerp(rough, 1.0 - rough, InvertRoughness);
float gloss = saturate(1.0 - rough);
float2 nxy = Texture2DSample(Normal, NormalSampler, UV).rg * 2.0 - 1.0;
nxy.y *= FlipGreen; // the pack's maps are OpenGL-style (Unity/Blender); Unreal's tangent frame wants DirectX green
float3 tn = float3(nxy * NormalStrength, sqrt(saturate(1.0 - dot(nxy, nxy))));
float3 n = mul(normalize(tn), (float3x3)Parameters.TangentToWorld);
if (!(dot(n, n) > 1e-8) || any(isnan(n))) n = VertexNormal;
n = normalize(n);
if (any(isnan(n))) n = float3(0, 0, 1);
float3 l = normalize(SunRel - PositionCR);
float3 v = normalize(-PositionCR);
float ndl = saturate(dot(n, l));
float ndl2 = saturate(dot(n, normalize(Sun2Rel - PositionCR)));
float fill = saturate(dot(n, -l)) * 0.08 + 0.06;
float power = exp2(gloss * 8.0 + 3.0);
float spec = pow(saturate(dot(n, normalize(l + v))), power) * (0.1 + 0.9 * gloss) * step(0.0, dot(n, l));
float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
float3 col = albedo * (1.0 - 0.45 * metallic) * (ndl * SunColor + ndl2 * Sun2Color + fill) + f0 * spec * SunColor * 1.6;
float rim = pow(1.0 - saturate(dot(n, v)), 4.0);
col += float3(0.35, 0.7, 1.0) * rim * _Highlight * 0.6;
col += Texture2DSample(Emission, EmissionSampler, UV).rgb * EmissiveIntensity * (0.55 + 0.45 * saturate(_EngineGlow)) * _Tint;
col = any(isnan(col)) ? float3(1, 0, 1) : col;
return SG_Expose3(col, Exposure, SceneLuminance);
"""


def _rgb(mat, source, output, x, y):
    m = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    for c, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        m.set_editor_property(c, on)
    mel.connect_material_expressions(source, output, m, "")
    return m


def build_game_material(default_atlas):
    path = f"{MATERIALS}/{GAME_MATERIAL}"
    mat = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if mat is None:
        mat = tools.create_asset(GAME_MATERIAL, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)
    mpc = unreal.load_asset(MPC)
    if mpc is None:
        raise RuntimeError(f"missing {MPC} (the SpaceGodRender plugin)")
    S = unreal.MaterialSamplerType
    y = [0]

    def node(cls, x=-1600):
        e = mel.create_material_expression(mat, cls, x, y[0])
        y[0] += 110
        return e

    def tex(name, key, sampler):
        e = node(unreal.MaterialExpressionTextureObjectParameter)
        e.set_editor_property("parameter_name", name)
        t = _tex(default_atlas, key)
        if t is not None:
            e.set_editor_property("texture", t)
        e.set_editor_property("sampler_type", sampler)
        return e, ""

    def vec(name, rgb):
        e = node(unreal.MaterialExpressionVectorParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        return _rgb(mat, e, "", -1300, y[0]), ""

    def scalar(name, value):
        e = node(unreal.MaterialExpressionScalarParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", value)
        return e, ""

    def collection(name, rgb):
        e = node(unreal.MaterialExpressionCollectionParameter)
        e.set_editor_property("collection", mpc)
        e.set_editor_property("parameter_name", name)
        return (_rgb(mat, e, "", -1300, y[0]), "") if rgb else (e, "")

    camera = node(unreal.MaterialExpressionCameraPositionWS)

    def relative(name):
        s = node(unreal.MaterialExpressionSubtract, -1100)
        c, _ = collection(name, True)
        mel.connect_material_expressions(c, "", s, "A")
        mel.connect_material_expressions(camera, "", s, "B")
        return s, ""

    def position_cr():
        e = node(unreal.MaterialExpressionWorldPosition)
        e.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
        return e, ""

    uv = node(unreal.MaterialExpressionTextureCoordinate)
    inputs = [
        ("UV", (uv, "")),
        ("BaseColor", tex("BaseColor", "Grey", S.SAMPLERTYPE_COLOR)),
        ("Metallic", tex("Metallic", "Metallic", S.SAMPLERTYPE_MASKS)),
        ("Roughness", tex("Roughness", "Roughness", S.SAMPLERTYPE_MASKS)),
        ("Normal", tex("Normal", "Normal", S.SAMPLERTYPE_NORMAL)),
        ("Emission", tex("Emission", "Emission", S.SAMPLERTYPE_COLOR)),
        ("Tint", vec("Tint", (1, 1, 1))),
        ("_Tint", vec("_Tint", (1, 1, 1))),
        ("_Highlight", scalar("_Highlight", 0.0)),
        ("_EngineGlow", scalar("_EngineGlow", 0.6)),
        ("EmissiveIntensity", scalar("EmissiveIntensity", 2.0)),
        ("NormalStrength", scalar("NormalStrength", 1.0)),
        ("InvertRoughness", scalar("InvertRoughness", 0.0)),
        ("FlipGreen", scalar("FlipGreen", -1.0)),
        ("VertexNormal", (node(unreal.MaterialExpressionVertexNormalWS), "")),
        ("PositionCR", position_cr()),
        ("SunRel", relative("_SunPositionWS")),
        ("SunColor", collection("_SunColor", True)),
        ("Sun2Rel", relative("_Sun2PositionWS")),
        ("Sun2Color", collection("_Sun2Color", True)),
        ("Exposure", (node(unreal.MaterialExpressionEyeAdaptation), "")),
        ("SceneLuminance", collection("_UnitySpaceSceneLuminance", False)),
    ]
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", "USC/Ship")
    custom.set_editor_property("code", SHIP_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("include_file_paths", ["/Plugin/SpaceGodRender/SpaceGodCommon.ush"])
    custom_inputs = []
    for name, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        custom_inputs.append(ci)
    custom.set_editor_property("inputs", custom_inputs)
    for name, (source, out) in inputs:
        if not mel.connect_material_expressions(source, out, custom, name):
            raise RuntimeError(f"{GAME_MATERIAL}: could not connect input {name}")
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")
    return mat


def build_instances(parent, atlases):
    made = 0
    for atlas, maps in atlases.items():
        for colour in COLOURS:
            if colour not in maps:
                continue
            name = f"MI_USC_{atlas}_{colour}"
            path = f"{MATERIALS}/{name}"
            mi = unreal.load_asset(path) if eal.does_asset_exist(path) else None
            if mi is None:
                mi = tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, parent)
            for param, key in (("BaseColor", colour), ("Normal", "Normal"), ("Metallic", "Metallic"),
                               ("Emission", "Emission")):
                t = _tex(atlas, key) if key in maps else None
                if t is not None:
                    mel.set_material_instance_texture_parameter_value(mi, param, t)
            # A set without a roughness map has a glossiness one: roughness = 1 - glossiness.
            rough = "Roughness" if "Roughness" in maps else "Glossiness" if "Glossiness" in maps else None
            if rough:
                mel.set_material_instance_texture_parameter_value(mi, "Roughness", _tex(atlas, rough))
            mel.set_material_instance_scalar_parameter_value(mi, "InvertRoughness", 1.0 if rough == "Glossiness" else 0.0)
            mel.update_material_instance(mi)
            eal.save_asset(path, only_if_is_dirty=False)
            made += 1
    _log(f"built {made} material instance(s)")
    return made


# ---------------------------------------------------------------- meshes

def _mesh_settings(sm):
    sm.set_editor_property("combine_meshes", True)
    # Ships are drawn, never collided with in Unreal (the simulation has its own geometry): no collision.
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("build_nanite", False)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("remove_degenerates", True)
    sm.set_editor_property("convert_scene", True)
    sm.set_editor_property("force_front_x_axis", False)
    sm.set_editor_property("convert_scene_unit", True)
    sm.set_editor_property("transform_vertex_to_absolute", True)
    sm.set_editor_property("bake_pivot_in_vertex", False)
    sm.set_editor_property("import_rotation", unreal.Rotator(0, 0, 0))
    sm.set_editor_property("import_translation", unreal.Vector(0, 0, 0))
    sm.set_editor_property("import_uniform_scale", 1.0)


def _task(path, name):
    existing = f"{MESHES}/{name}"
    if eal.does_asset_exist(existing):
        mesh = unreal.load_asset(existing)
        data = mesh.get_editor_property("asset_import_data") if isinstance(mesh, unreal.StaticMesh) else None
        if isinstance(data, unreal.FbxStaticMeshImportData):
            _mesh_settings(data)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    _mesh_settings(ui.get_editor_property("static_mesh_import_data"))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", MESHES)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    return task


def atlas_of(mesh_name):
    """SM_USC_GalacticLeopard31_Main_Bow -> GalacticLeopard_Main; SM_USC_HyperFalcon07_Hull_Whole -> HyperFalcon;
    SM_USC_Mod_VoidWhaleTurretBase -> VoidWhale_Parts."""
    rest = mesh_name[len("SM_USC_"):]
    if rest.startswith("Mod_"):
        family = next(f for f in ("GalacticLeopard", "VoidWhale") if rest[4:].startswith(f))
        return f"{family}_Parts"
    model, atlas = rest.split("_")[0], rest.split("_")[1]
    family = model.rstrip("0123456789")
    return family if atlas == "Hull" else f"{family}_{atlas}"


def _lods(mesh):
    """Two simple reduced LODs (50 % and 20 % of the triangles), when the commandlet has the subsystem for it."""
    try:
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        options = unreal.EditorScriptingMeshReductionOptions()
        options.set_editor_property("reduction_settings", [
            unreal.EditorScriptingMeshReductionSettings(1.0, 1.0),
            unreal.EditorScriptingMeshReductionSettings(0.5, 0.35),
            unreal.EditorScriptingMeshReductionSettings(0.2, 0.12)])
        return sub.set_lods(mesh, options) > 1
    except Exception:  # no subsystem in this commandlet: the area streaming's detail rings (US-T133) stand in
        return False


def import_meshes(pieces):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    files = sorted(f for f in os.listdir(pieces) if f.lower().endswith(".fbx"))
    # A model dropped from usc_prepare.py's table leaves its meshes behind: remove whatever the pieces no longer have.
    keep = {os.path.splitext(f)[0] for f in files}
    for stale in eal.list_assets(MESHES, recursive=False):
        short = stale.split("/")[-1].split(".")[0]
        if short.startswith("SM_USC_") and short not in keep:
            eal.delete_asset(stale.split(".")[0])
            _log(f"removed {short} (no longer cut)")
    tasks =[_task(os.path.join(pieces, f), os.path.splitext(f)[0]) for f in files]
    tools.import_asset_tasks(tasks)
    meshes, lods = [], 0
    for f in files:
        name = os.path.splitext(f)[0]
        path = f"{MESHES}/{name}"
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            _fail(f"mesh import failed: {f}")
            continue
        atlas = atlas_of(name)
        mi = unreal.load_asset(f"{MATERIALS}/MI_USC_{atlas}_Grey")
        if mi is not None:
            for slot in range(len(mesh.get_editor_property("static_materials"))):
                mesh.set_material(slot, mi)
        if _lods(mesh):
            lods += 1
        eal.save_asset(path, only_if_is_dirty=False)
        box = mesh.get_bounding_box()
        size = box.max - box.min
        centre = (box.min + box.max) * 0.5
        _log(f"mesh {name}: {mesh.get_num_triangles(0)} tris, {size.x:.0f}x{size.y:.0f}x{size.z:.0f} cm, "
             f"centre ({centre.x:.0f},{centre.y:.0f},{centre.z:.0f}), atlas {atlas}")
        meshes.append(mesh)
    _log(f"LODs generated for {lods}/{len(meshes)} mesh(es)" if lods else
         "no LODs generated (no StaticMeshEditorSubsystem here); the area streaming's detail rings carry the load")
    return meshes


def main():
    src = os.environ.get("USC_SOURCE")
    pieces = os.environ.get("USC_PIECES")
    if not src or not os.path.isdir(src) or not pieces or not os.path.isdir(pieces):
        raise RuntimeError(f"USC_SOURCE ({src}) and USC_PIECES ({pieces}) must be folders (run tools/import-usc.ps1)")
    _log(f"source {src}, pieces {pieces}")
    atlases = textures_of(src)
    textures = import_textures(atlases)
    instances = 0
    try:
        parent = build_game_material("GalacticLeopard_Main")
        instances = build_instances(parent, atlases)
    except Exception as error:  # the summary names it; the meshes still import
        _fail(f"{GAME_MATERIAL}: {error}")
    meshes = import_meshes(pieces)
    eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    _log(f"SUMMARY meshes={len(meshes)} textures={textures} instances={instances} failures={len(failures)}")
    for f in failures:
        unreal.log_error(f"USC: FAILURE {f}")


main()
