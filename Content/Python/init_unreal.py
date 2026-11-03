"""Editor startup: builds the assets the C++ loads by path, once.

Unreal runs this file whenever the editor opens the project (Python Editor Script Plugin). It waits until the
asset registry has finished scanning, then, only for what is missing:
  - imports SourceArt/Textures/*.jpg into /Game/UnrealSpaceGod/Textures (mipmapped; normal maps flagged as such),
  - creates /Game/UnrealSpaceGod/Materials/M_Body (unlit, lit per body from the Sun by the SunDir parameter,
    with map tint, haze and an atmosphere rim) and M_Ring (translucent, vertex-colour opacity).
Commit the generated .uasset files so a packaged build has them.
"""

import os

import unreal

ROOT = "/Game/UnrealSpaceGod"
MATERIALS = ROOT + "/Materials"
TEXTURES = ROOT + "/Textures"

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def _expr(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def _vector(mat, name, rgba, x, y):
    e = _expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*rgba))
    return e


def _scalar(mat, name, value, x, y):
    e = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def _constant(mat, value, x, y):
    e = _expr(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", value)
    return e


def _rgb(mat, source, output, x, y):
    m = _expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    m.set_editor_property("r", True)
    m.set_editor_property("g", True)
    m.set_editor_property("b", True)
    m.set_editor_property("a", False)
    mel.connect_material_expressions(source, output, m, "")
    return m


def _binary(mat, cls, a, b, x, y, a_out="", b_out=""):
    e = _expr(mat, cls, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def _lerp(mat, a, b, alpha, x, y):
    e = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    mel.connect_material_expressions(a, "", e, "A")
    mel.connect_material_expressions(b, "", e, "B")
    mel.connect_material_expressions(alpha, "", e, "Alpha")
    return e


def _saturate(mat, source, x, y):
    e = _expr(mat, unreal.MaterialExpressionSaturate, x, y)
    mel.connect_material_expressions(source, "", e, "")
    return e


def _new_material(name):
    path = f"{MATERIALS}/{name}"
    if eal.does_asset_exist(path):
        return None, path
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    return mat, path


def build_body_material():
    mat, path = _new_material("M_Body")
    if mat is None:
        return

    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    tex = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1400, -200)
    tex.set_editor_property("parameter_name", "BaseMap")
    if white:
        tex.set_editor_property("texture", white)

    tint = _rgb(mat, _vector(mat, "Tint", (1, 1, 1, 1), -1400, 100), "", -1150, 100)
    atmo = _rgb(mat, _vector(mat, "AtmosphereColor", (0, 0, 0, 1), -1400, 300), "", -1150, 300)
    sun = _rgb(mat, _vector(mat, "SunDir", (1, 0, 0, 0), -1400, 500), "", -1150, 500)
    haze = _scalar(mat, "Haze", 0.0, -1150, 700)
    strength = _scalar(mat, "AtmosphereStrength", 0.0, -1150, 800)
    ambient = _scalar(mat, "Ambient", 0.01, -1150, 900)
    self_lit = _scalar(mat, "SelfLit", 0.0, -400, 900)
    brightness = _scalar(mat, "Brightness", 1.0, -650, 1000)

    # Surface colour: the map (tinted for single-band mosaics), hidden by haze where the atmosphere is opaque.
    base = _binary(mat, unreal.MaterialExpressionMultiply, tex, tint, -900, -100, a_out="RGB")
    hazed = _lerp(mat, base, atmo, haze, -700, 0)

    # Lambert from the Sun, per body, so every world is lit from the right side wherever the camera is.
    normal = _expr(mat, unreal.MaterialExpressionPixelNormalWS, -1150, 400)
    ndl = _saturate(mat, _binary(mat, unreal.MaterialExpressionDotProduct, normal, sun, -900, 450), -750, 450)
    lit = _binary(mat, unreal.MaterialExpressionMultiply, hazed,
                  _binary(mat, unreal.MaterialExpressionAdd, ndl, ambient, -600, 500), -450, 100)

    # Atmosphere rim on the day side.
    fresnel = _expr(mat, unreal.MaterialExpressionFresnel, -900, 650)
    fresnel.set_editor_property("exponent", 3.0)
    day = _saturate(mat, _binary(mat, unreal.MaterialExpressionAdd, ndl, _constant(mat, 0.15, -750, 650), -600, 650), -450, 650)
    rim = _binary(mat, unreal.MaterialExpressionMultiply,
                  _binary(mat, unreal.MaterialExpressionMultiply, fresnel, atmo, -600, 750),
                  _binary(mat, unreal.MaterialExpressionMultiply, strength, day, -450, 800), -300, 700)

    surface = _binary(mat, unreal.MaterialExpressionAdd, lit, rim, -200, 300)
    glowing = _binary(mat, unreal.MaterialExpressionMultiply, base, brightness, -400, 1000)
    final = _lerp(mat, surface, glowing, self_lit, 0, 500)
    mel.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_asset(path)
    unreal.log(f"Unreal.SpaceGod: created {path}")


def build_ring_material():
    mat, path = _new_material("M_Ring")
    if mat is None:
        return
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    vc = _expr(mat, unreal.MaterialExpressionVertexColor, -800, 0)
    color = _rgb(mat, _vector(mat, "Color", (1, 1, 1, 1), -800, 250), "", -550, 250)
    rgb = _rgb(mat, vc, "", -550, 0)
    emissive = _binary(mat, unreal.MaterialExpressionMultiply, rgb, color, -300, 100)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(vc, "A", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    eal.save_asset(path)
    unreal.log(f"Unreal.SpaceGod: created {path}")


def import_textures():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    source = os.path.join(project, "SourceArt", "Textures")
    if not os.path.isdir(source):
        return
    tasks = []
    for file in sorted(os.listdir(source)):
        if not file.lower().endswith((".jpg", ".png")):
            continue
        name = os.path.splitext(file)[0]
        if eal.does_asset_exist(f"{TEXTURES}/{name}"):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(source, file))
        task.set_editor_property("destination_path", TEXTURES)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("replace_existing", False)
        tasks.append((name, task))
    if not tasks:
        return
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, t in tasks])
    for name, _ in tasks:
        if not name.endswith("_normal"):
            continue
        path = f"{TEXTURES}/{name}"
        texture = unreal.load_asset(path)
        if texture:
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("srgb", False)
            eal.save_asset(path)
    unreal.log(f"Unreal.SpaceGod: imported {len(tasks)} texture(s)")


_handle = None


def _when_ready(_delta):
    global _handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return
    unreal.unregister_slate_post_tick_callback(_handle)
    _handle = None
    try:
        import_textures()
        build_body_material()
        build_ring_material()
    except Exception as error:  # an editor startup script must never stop the editor opening
        unreal.log_error(f"Unreal.SpaceGod asset setup failed: {error}")


_handle = unreal.register_slate_post_tick_callback(_when_ready)
