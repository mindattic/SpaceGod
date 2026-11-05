"""Headless import of Ebal Studios' Star Sparrow fighter pack into /Game/ThirdParty/StarSparrow (US-T109).

The pack is third-party art from the owner's library and must not be redistributed, so neither the source files
(Cpp/SourceArt/ThirdParty/) nor the imported assets (Cpp/Content/ThirdParty/) are committed; this script rebuilds
them on any machine that has the zip. Run it through tools/import-starsparrow.ps1, which extracts the zip and calls
    UnrealEditor-Cmd Cpp/SpaceGod.uproject -run=pythonscript -script=<abs>/tools/import_starsparrow.py
It is idempotent: meshes and textures are re-imported over the old ones, the material graph is rebuilt in place,
and the material instances are updated, so references to them survive a re-run.

What it builds (see docs/ThirdPartyArt.md):
  Meshes/SM_StarSparrow01..40     the 40 prebuilt fighters, one combined static mesh each (nose +X, up +Z, cm)
  Meshes/SM_StarSparrowModule_*   the 8 separate parts from StarSparrowModules.fbx, each about its own pivot
  Textures/T_StarSparrow_*        the shared 2048 atlas set (colours sRGB; normal; metallic/gloss/masks linear)
  Materials/M_StarSparrow         master material; MI_StarSparrow_<Colour> per supplied colour, MI_StarSparrow_Livery
"""

import os

import unreal

ROOT = "/Game/ThirdParty/StarSparrow"
MESHES = ROOT + "/Meshes"
TEXTURES = ROOT + "/Textures"
MATERIALS = ROOT + "/Materials"
DEFAULT_INSTANCE = "MI_StarSparrow_Grey"

COLOURS = ["Black", "Blue", "Cyan", "Green", "Grey", "Orange", "Purple", "Red", "White", "Yellow"]

# Per-mask tints that turn the Neutral texture into each painted colour variant. Fitted (least squares, sRGB space)
# from the supplied textures: painted ~= Neutral * tint inside each mask, mean error 0.6-1.9 %. Stored here in sRGB and
# converted to linear below, since the material multiplies linear values. Mask4 (accent lights) is ~0.1 % of the atlas.
LIVERY_TINTS = {
    "Black":  [(0.31, 0.31, 0.30), (0.32, 0.32, 0.32), (0.93, 0.93, 0.93), (0.93, 0.89, 0.32)],
    "Blue":   [(0.12, 0.48, 1.00), (0.53, 0.56, 0.60), (1.62, 1.62, 1.62), (0.94, 0.91, 0.90)],
    "Cyan":   [(0.14, 1.28, 1.47), (0.32, 0.34, 0.34), (0.93, 0.95, 0.96), (0.93, 1.03, 1.22)],
    "Green":  [(0.20, 0.54, 0.33), (0.32, 0.33, 0.32), (0.99, 0.73, 0.68), (0.93, 0.91, 0.34)],
    "Grey":   [(0.54, 0.54, 0.54), (0.33, 0.33, 0.33), (1.08, 1.08, 1.08), (0.94, 0.91, 0.50)],
    "Orange": [(1.20, 0.77, 0.07), (0.34, 0.33, 0.32), (0.95, 0.94, 0.93), (1.01, 0.93, 0.17)],
    "Purple": [(0.38, 0.20, 0.70), (0.32, 0.32, 0.33), (0.93, 0.93, 0.93), (0.94, 0.89, 0.62)],
    "Red":    [(0.79, 0.20, 0.20), (0.33, 0.32, 0.32), (0.94, 0.93, 0.93), (0.96, 0.89, 0.25)],
    "White":  [(1.40, 1.40, 1.40), (0.55, 0.55, 0.55), (1.19, 1.19, 1.19), (1.05, 1.08, 1.24)],
    "Yellow": [(1.62, 1.32, 0.26), (0.34, 0.34, 0.32), (0.96, 0.95, 0.93), (1.09, 1.06, 0.29)],
}

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
failures = []


def _log(msg):
    unreal.log(f"StarSparrow: {msg}")


def _fail(msg):
    failures.append(msg)
    unreal.log_error(f"StarSparrow: {msg}")


def _source_dir():
    src = os.environ.get("STARSPARROW_SOURCE")
    if not src:
        project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
        src = os.path.join(project, "SourceArt", "ThirdParty", "StarSparrow", "StarSparrow_FBX")
    if not os.path.isfile(os.path.join(src, "StarSparrowModules.fbx")):
        raise RuntimeError(f"Star Sparrow source not found at {src} (run tools/import-starsparrow.ps1 -Zip <zip>)")
    return src


# ---------------------------------------------------------------- textures

def _texture_kind(name):
    if name == "Normal":
        return "normal"
    if name in ("Metallic", "Glossiness", "Roughness", "Specular") or name.startswith("Mask"):
        return "linear"
    return "color"


def import_textures(src):
    tex_dir = os.path.join(src, "Textures")
    files = []
    for dirpath, _, names in os.walk(tex_dir):
        files += [os.path.join(dirpath, n) for n in sorted(names) if n.lower().endswith(".png")]
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", TEXTURES)
        task.set_editor_property("destination_name", "T_" + os.path.splitext(os.path.basename(path))[0])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    count = 0
    for path in files:
        base = os.path.splitext(os.path.basename(path))[0]  # StarSparrow_Blue, StarSparrow_Mask1, ...
        asset = f"{TEXTURES}/T_{base}"
        tex = unreal.load_asset(asset)
        if tex is None:
            _fail(f"texture import failed: {path}")
            continue
        kind = _texture_kind(base.split("_", 1)[1])
        if kind == "normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif kind == "linear":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        else:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property("srgb", True)
        eal.save_asset(asset, only_if_is_dirty=False)
        count += 1
    _log(f"imported {count}/{len(files)} texture(s)")
    return count


def _tex(base):
    tex = unreal.load_asset(f"{TEXTURES}/T_StarSparrow_{base}")
    if tex is None:
        raise RuntimeError(f"missing texture T_StarSparrow_{base}")
    return tex


# ---------------------------------------------------------------- material

def _expr(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def _tex_param(mat, name, texture, sampler, x, y):
    e = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", texture)
    e.set_editor_property("sampler_type", sampler)
    # Shared wrap samplers, so the 10 textures stay well inside the 16-sampler limit.
    e.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    return e


def _vector(mat, name, rgb, x, y):
    e = _expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return e


def _scalar(mat, name, value, x, y):
    e = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def _rgb(mat, source, output, x, y):
    m = _expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    for c, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        m.set_editor_property(c, on)
    mel.connect_material_expressions(source, output, m, "")
    return m


def _linear(rgb):
    return tuple(max(c, 0.0) ** 2.2 for c in rgb)


def build_material():
    path = f"{MATERIALS}/M_StarSparrow"
    mat = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if mat is None:
        mat = tools.create_asset("M_StarSparrow", MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    else:
        mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)

    S = unreal.MaterialSamplerType
    painted = _tex_param(mat, "BaseColor", _tex(COLOURS[4]), S.SAMPLERTYPE_COLOR, -1600, -600)
    neutral = _tex_param(mat, "Neutral", _tex("Neutral"), S.SAMPLERTYPE_COLOR, -1600, -300)

    # Livery: Neutral * lerp(... lerp(1, Tint1, Mask1) ..., Tint4, Mask4). Defaults reproduce the Grey variant.
    tint = _expr(mat, unreal.MaterialExpressionConstant3Vector, -900, -100)
    tint.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
    for i in range(4):
        y = -100 + i * 250
        mask = _tex_param(mat, f"Mask{i + 1}", _tex(f"Mask{i + 1}"), S.SAMPLERTYPE_MASKS, -1600, y)
        colour = _rgb(mat, _vector(mat, f"Tint{i + 1}", _linear(LIVERY_TINTS["Grey"][i]), -1300, y + 120), "",
                      -1100, y + 120)
        lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -700 + i * 120, y)
        mel.connect_material_expressions(tint, "", lerp, "A")
        mel.connect_material_expressions(colour, "", lerp, "B")
        mel.connect_material_expressions(mask, "R", lerp, "Alpha")
        tint = lerp
    livery = _expr(mat, unreal.MaterialExpressionMultiply, -150, -300)
    mel.connect_material_expressions(neutral, "RGB", livery, "A")
    mel.connect_material_expressions(tint, "", livery, "B")

    switch = _expr(mat, unreal.MaterialExpressionStaticSwitchParameter, 50, -450)
    switch.set_editor_property("parameter_name", "UseLivery")
    switch.set_editor_property("default_value", False)
    mel.connect_material_expressions(livery, "", switch, "True")
    mel.connect_material_expressions(painted, "RGB", switch, "False")
    mel.connect_material_property(switch, "", unreal.MaterialProperty.MP_BASE_COLOR)

    metallic = _tex_param(mat, "Metallic", _tex("Metallic"), S.SAMPLERTYPE_MASKS, -400, 950)
    mel.connect_material_property(metallic, "R", unreal.MaterialProperty.MP_METALLIC)

    # The pack ships glossiness (Unity smoothness): roughness = 1 - glossiness.
    gloss = _tex_param(mat, "Glossiness", _tex("Glossiness"), S.SAMPLERTYPE_MASKS, -400, 1200)
    rough = _expr(mat, unreal.MaterialExpressionOneMinus, -100, 1200)
    mel.connect_material_expressions(gloss, "R", rough, "")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    normal = _tex_param(mat, "Normal", _tex("Normal"), S.SAMPLERTYPE_NORMAL, -400, 1450)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    emission = _tex_param(mat, "Emission", _tex("Emission"), S.SAMPLERTYPE_COLOR, -400, 1700)
    intensity = _scalar(mat, "EmissiveIntensity", 4.0, -400, 1950)
    glow = _expr(mat, unreal.MaterialExpressionMultiply, -100, 1750)
    mel.connect_material_expressions(emission, "RGB", glow, "A")
    mel.connect_material_expressions(intensity, "", glow, "B")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_asset(path, only_if_is_dirty=False)
    _log(f"built {path}")
    return mat


# ---------------------------------------------------------------- the in-game material (US-T110)

GAME_MATERIAL = "M_StarSparrowShip"
MPC = "/SpaceGodRender/Materials/MPC_SpaceGod"

# The game draws with no Unreal lights: every material is unlit and lights itself from the star(s) in MPC_SpaceGod, as
# SpaceGodRender's M_Ship does (Shaders/Ship.ush). This is the pack's atlas in those terms: the livery (Neutral times a
# tint per mask), its normal, metallic, glossiness and emission maps, lit by the star(s) with a cool fill on the night
# side, then SG_Expose3 so it reaches the image as the game's own hulls do. The C++ side (Rendering/StarSparrowKit)
# sets Tint1..4 per faction, and ShipView's property block sets _Tint (a wreck's char), _Highlight and _EngineGlow.
SHIP_CODE = r"""
float3 tint = float3(1, 1, 1);
tint = lerp(tint, Tint1, Texture2DSample(Mask1, Mask1Sampler, UV).r);
tint = lerp(tint, Tint2, Texture2DSample(Mask2, Mask2Sampler, UV).r);
tint = lerp(tint, Tint3, Texture2DSample(Mask3, Mask3Sampler, UV).r);
tint = lerp(tint, Tint4, Texture2DSample(Mask4, Mask4Sampler, UV).r);
float3 albedo = Texture2DSample(Neutral, NeutralSampler, UV).rgb * tint * _Tint;
float metallic = Texture2DSample(Metallic, MetallicSampler, UV).r;
float gloss = Texture2DSample(Glossiness, GlossinessSampler, UV).r;
float2 nxy = Texture2DSample(Normal, NormalSampler, UV).rg * 2.0 - 1.0;
nxy.y = -nxy.y; // the pack's maps are OpenGL-style (Unity); Unreal's tangent frame wants DirectX green
float3 tn = float3(nxy * NormalStrength, sqrt(saturate(1.0 - dot(nxy, nxy))));
float3 n = mul(normalize(tn), (float3x3)Parameters.TangentToWorld);
// A mesh without a usable tangent frame falls back to its vertex normal rather than going black (NaN).
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


def build_game_material():
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

    def tex(name, base, sampler):
        e = node(unreal.MaterialExpressionTextureObjectParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("texture", _tex(base))
        e.set_editor_property("sampler_type", sampler)
        return e, ""

    def vec(name, rgb, mask=True):
        e = node(unreal.MaterialExpressionVectorParameter)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        return (_rgb(mat, e, "", -1300, y[0]), "") if mask else (e, "")

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
    grey = LIVERY_TINTS["Grey"]
    inputs = [
        ("UV", (uv, "")),
        ("Neutral", tex("Neutral", "Neutral", S.SAMPLERTYPE_COLOR)),
        ("Mask1", tex("Mask1", "Mask1", S.SAMPLERTYPE_MASKS)),
        ("Mask2", tex("Mask2", "Mask2", S.SAMPLERTYPE_MASKS)),
        ("Mask3", tex("Mask3", "Mask3", S.SAMPLERTYPE_MASKS)),
        ("Mask4", tex("Mask4", "Mask4", S.SAMPLERTYPE_MASKS)),
        ("Metallic", tex("Metallic", "Metallic", S.SAMPLERTYPE_MASKS)),
        ("Glossiness", tex("Glossiness", "Glossiness", S.SAMPLERTYPE_MASKS)),
        ("Normal", tex("Normal", "Normal", S.SAMPLERTYPE_NORMAL)),
        ("Emission", tex("Emission", "Emission", S.SAMPLERTYPE_COLOR)),
        ("Tint1", vec("Tint1", _linear(grey[0]))),
        ("Tint2", vec("Tint2", _linear(grey[1]))),
        ("Tint3", vec("Tint3", _linear(grey[2]))),
        ("Tint4", vec("Tint4", _linear(grey[3]))),
        ("_Tint", vec("_Tint", (1, 1, 1))),
        ("_Highlight", scalar("_Highlight", 0.0)),
        ("_EngineGlow", scalar("_EngineGlow", 0.6)),
        ("EmissiveIntensity", scalar("EmissiveIntensity", 2.0)),
        ("NormalStrength", scalar("NormalStrength", 1.0)),
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
    custom.set_editor_property("description", "StarSparrow/Ship")
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


def _instance(parent, name):
    path = f"{MATERIALS}/{name}"
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else None
    if mi is None:
        mi = tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    return mi, path


def build_instances(parent):
    made = []
    for colour in COLOURS + ["Livery"]:
        name = f"MI_StarSparrow_{colour}"
        mi, path = _instance(parent, name)
        fit = LIVERY_TINTS["Grey" if colour == "Livery" else colour]
        if colour != "Livery":
            mel.set_material_instance_texture_parameter_value(mi, "BaseColor", _tex(colour))
        # Every instance carries the fitted tints, so flipping UseLivery keeps the look; the Livery instance is the
        # faction template (Neutral + masks, starting from the Grey fit).
        for i, rgb in enumerate(fit):
            lin = _linear(rgb)
            mel.set_material_instance_vector_parameter_value(mi, f"Tint{i + 1}", unreal.LinearColor(*lin, 1.0))
        mel.set_material_instance_static_switch_parameter_value(mi, "UseLivery", colour == "Livery")
        mel.update_material_instance(mi)
        eal.save_asset(path, only_if_is_dirty=False)
        made.append(name)
    _log(f"built {len(made)} material instance(s): {', '.join(made)}")
    return made


# ---------------------------------------------------------------- meshes

def _fbx_options(combine, offset=None):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    _mesh_settings(ui.get_editor_property("static_mesh_import_data"), combine, offset)
    return ui


def _mesh_settings(sm, combine, offset=None):
    sm.set_editor_property("combine_meshes", combine)
    # The FBX carries no UCX hulls: generate one 18-DOP convex hull per mesh (simple collision, cheap for gameplay).
    # A convex decomposition would need StaticMeshEditorSubsystem, which a -run=pythonscript commandlet does not have.
    sm.set_editor_property("auto_generate_collision", True)
    # ~1.4k-3.7k triangles per fighter: Nanite would only add overhead, and nothing here uses baked lighting.
    sm.set_editor_property("build_nanite", False)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("remove_degenerates", True)
    sm.set_editor_property("convert_scene", True)
    sm.set_editor_property("force_front_x_axis", True)
    sm.set_editor_property("convert_scene_unit", True)
    sm.set_editor_property("transform_vertex_to_absolute", True)
    sm.set_editor_property("bake_pivot_in_vertex", False)
    sm.set_editor_property("import_rotation", unreal.Rotator(0, 0, 0))
    sm.set_editor_property("import_translation", offset if offset is not None else unreal.Vector(0, 0, 0))


def _mesh_task(path, name, combine, offset=None):
    # Importing over an existing mesh is a reimport, which reads the settings stored on the mesh rather than the
    # task's options: write the same settings there first so every run gives the same result.
    for existing in eal.list_assets(MESHES, recursive=False):
        short = existing.split("/")[-1].split(".")[0]
        if short == name or (not combine and short.startswith(name + "_")):
            mesh = unreal.load_asset(existing)
            data = mesh.get_editor_property("asset_import_data") if isinstance(mesh, unreal.StaticMesh) else None
            if isinstance(data, unreal.FbxStaticMeshImportData):
                _mesh_settings(data, combine, offset)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", MESHES)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", _fbx_options(combine, offset))
    return task


def _meshes_of(task):
    found = [unreal.load_asset(p) for p in task.get_editor_property("imported_object_paths")]
    return [m for m in found if isinstance(m, unreal.StaticMesh)]


def _fbx_node_translations(path):
    """{node name: Lcl Translation} for the Model nodes of a binary FBX 7.x file. Reads data only."""
    import struct
    import zlib

    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"Kaydara FBX Binary"):
        raise RuntimeError(f"{path} is not a binary FBX")
    wide = struct.unpack_from("<I", data, 23)[0] >= 7500

    def node(pos):
        if wide:
            end, count, _ = struct.unpack_from("<QQQ", data, pos)
            pos += 24
        else:
            end, count, _ = struct.unpack_from("<III", data, pos)
            pos += 12
        name = data[pos + 1:pos + 1 + data[pos]].decode("ascii", "replace")
        pos += 1 + data[pos]
        if end == 0:
            return None, pos
        props = []
        for _ in range(count):
            kind = chr(data[pos])
            pos += 1
            if kind in "YCIFDL":
                fmt = {"Y": "<h", "C": "<?", "I": "<i", "F": "<f", "D": "<d", "L": "<q"}[kind]
                props.append(struct.unpack_from(fmt, data, pos)[0])
                pos += struct.calcsize(fmt)
            elif kind in "fdlib":
                n, enc, size = struct.unpack_from("<III", data, pos)
                raw = data[pos + 12:pos + 12 + size]
                pos += 12 + size
                raw = zlib.decompress(raw) if enc else raw
                props.append(struct.unpack("<%d%s" % (n, {"l": "q"}.get(kind, kind)), raw))
            else:  # S, R
                n = struct.unpack_from("<I", data, pos)[0]
                props.append(data[pos + 4:pos + 4 + n])
                pos += 4 + n
        children = []
        while pos < end:
            child, pos = node(pos)
            if child is None:
                break
            children.append(child)
        return (name, props, children), end

    out = {}
    pos = 27
    while pos < len(data) - 13:
        top, pos = node(pos)
        if top is None:
            break
        if top[0] != "Objects":
            continue
        for obj in top[2]:
            if obj[0] != "Model":
                continue
            model = obj[1][1].split(b"\x00")[0].decode("utf-8", "replace")
            for child in obj[2]:
                if child[0] == "Properties70":
                    for p in child[2]:
                        if p[1] and p[1][0] == b"Lcl Translation":
                            out[model] = tuple(p[1][4:7])
    return out


def import_modules(src):
    """StarSparrowModules.fbx holds 8 parts laid out in a row along the ship's length, each with its node at the
    part's own pivot. A plain import bakes that layout into the vertices, and the legacy importer only converts axes
    when it does; so each part is imported once with a translation that cancels its own node offset, and kept."""
    path = os.path.join(src, "StarSparrowModules.fbx")
    nodes = _fbx_node_translations(path)
    for stale in eal.list_assets(MESHES, recursive=False):  # names used by an earlier version of this script
        if "/SM_StarSparrowModules_" in stale:
            eal.delete_asset(stale.split(".")[0])
    made = []
    for part, (tx, ty, tz) in sorted(nodes.items()):
        short = part.replace("StarSparrow_", "")
        tmp = f"{ROOT}/_Import/{short}"
        task = _mesh_task(path, "SM_StarSparrowModules", False, unreal.Vector(0, 0, 0))
        task.set_editor_property("destination_path", tmp)
        # Unity space: x right, y up, z forward. With the scene converted (front axis X) that is UE (z, x, y).
        task.get_editor_property("options").get_editor_property("static_mesh_import_data").set_editor_property(
            "import_translation", unreal.Vector(-tz, -tx, -ty))
        tools.import_asset_tasks([task])
        wanted = [m for m in _meshes_of(task) if m.get_name().endswith("_" + part)]
        final = f"{MESHES}/SM_StarSparrowModule_{short}"
        if not wanted:
            _fail(f"module {part} not imported")
        else:
            if eal.does_asset_exist(final):
                eal.delete_asset(final)
            if eal.rename_asset(wanted[0].get_path_name().split(".")[0], final):
                made.append(unreal.load_asset(final))
            else:
                _fail(f"could not move module {part} to {final}")
        eal.delete_directory(tmp)
    eal.delete_directory(f"{ROOT}/_Import")
    return made


def import_meshes(src):
    # The pipeline below drives the legacy FBX importer (FbxImportUI); turn off the Interchange FBX path for this run.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    ships = [(os.path.join(src, f"StarSparrow{i:02d}.fbx"), f"SM_StarSparrow{i:02d}") for i in range(1, 41)]
    tasks = [_mesh_task(path, name, True) for path, name in ships]
    tools.import_asset_tasks(tasks)

    # Put the pivot at the middle of each fighter's length if an export left it elsewhere.
    redo = []
    for (path, name), task in zip(ships, tasks):
        got = _meshes_of(task)
        if len(got) != 1:
            continue
        box = got[0].get_bounding_box()
        centre = (box.min.x + box.max.x) * 0.5
        if abs(centre) > 100.0:
            redo.append(_mesh_task(path, name, True, unreal.Vector(-centre, 0, 0)))
    if redo:
        tools.import_asset_tasks(redo)
        _log(f"re-centred {len(redo)} fighter(s) along their length")

    meshes = []
    for task in tasks:
        found = _meshes_of(task)
        if not found:
            _fail(f"mesh import failed: {task.get_editor_property('filename')}")
        meshes += found
    return meshes + import_modules(src)

def finish_meshes(meshes, instance):
    for mesh in meshes:
        path = mesh.get_path_name().split(".")[0]
        try:
            for slot in range(len(mesh.get_editor_property("static_materials"))):
                mesh.set_material(slot, instance)
            eal.save_asset(path, only_if_is_dirty=False)
        except Exception as error:  # keep going; the summary names every failure
            _fail(f"{path}: {error}")
            continue
        box = mesh.get_bounding_box()
        size = box.max - box.min
        tris = mesh.get_num_triangles(0)
        pivot = (box.min + box.max) * 0.5
        _log(f"mesh {mesh.get_name()}: {tris} tris, size {size.x:.0f}x{size.y:.0f}x{size.z:.0f} cm, "
             f"bounds centre ({pivot.x:.0f},{pivot.y:.0f},{pivot.z:.0f}), "
             f"simple collision {'yes' if mesh.get_editor_property('body_setup') else 'NO'}")


def main():
    if os.environ.get("STARSPARROW_MATERIAL_ONLY"):
        # Just the in-game material (needs the imported textures, not the source art).
        build_game_material()
        _log(f"SUMMARY material only, failures={len(failures)}")
        return
    src = _source_dir()
    _log(f"source {src}")
    textures = import_textures(src)
    master = build_material()
    instances = build_instances(master)
    try:
        build_game_material()
    except Exception as error:  # the summary names it; the meshes still import
        _fail(f"{GAME_MATERIAL}: {error}")
    default = unreal.load_asset(f"{MATERIALS}/{DEFAULT_INSTANCE}")
    meshes = import_meshes(src)
    finish_meshes(meshes, default)
    eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    _log(f"SUMMARY meshes={len(meshes)} (40 fighters + module parts) textures={textures} "
         f"materials=1 master + {len(instances)} instances (default {DEFAULT_INSTANCE}) failures={len(failures)}")
    for f in failures:
        unreal.log_error(f"StarSparrow: FAILURE {f}")


main()
