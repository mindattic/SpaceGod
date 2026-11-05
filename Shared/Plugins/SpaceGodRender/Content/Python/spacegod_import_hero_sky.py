"""Imports a hand-painted sky sphere (an equirectangular 2:1 panorama) into the shared plugin's Resources (US-T145).

OPTIONAL, one-time import tool: its output (Content/Resources/Skyboxes/<name>.uasset) is committed, so neither the build
nor the game runs it. The first one is "Alien Space Nebula 1 (Skybox)" by Jungle Jim (CC BY 4.0, docs/ThirdPartyArt.md):
the game's hero sky, drawn as a sky sphere and as a painted volume (Views/SkyLayers).

  set SPACEGOD_SKY_PNG=C:/.../alien-space-nebula_7680x3840.png
  set SPACEGOD_SKY_NAME=jungle_jim_alien_space_nebula_8k
  UnrealEditor-Cmd Cpp/SpaceGod.uproject -run=pythonscript -script=<abs>/Shared/Plugins/SpaceGodRender/Content/Python/spacegod_import_hero_sky.py -unattended -nosplash

Settings as the other panoramas (spacegod_import_resources.py: sRGB, default compression, wrap U / clamp V), stretched to
a power of two (7680 x 3840 -> 8192 x 4096) so it gets the full mip chain: the painted volume reads its blurred mips.
"""
import os

import unreal

eal = unreal.EditorAssetLibrary
DESTINATION = "/SpaceGodRender/Resources/Skyboxes"

source = os.environ.get("SPACEGOD_SKY_PNG", "")
name = os.environ.get("SPACEGOD_SKY_NAME", "jungle_jim_alien_space_nebula_8k")
if not os.path.isfile(source):
    unreal.log_error(f"SpaceGod sky import: no panorama at '{source}' (set SPACEGOD_SKY_PNG)")
else:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("replace_existing", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = f"{DESTINATION}/{name}"
    tex = unreal.load_asset(path)
    if tex is None:
        unreal.log_error(f"SpaceGod sky import: {path} did not import")
    else:
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("power_of_two_mode", unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
        tex.set_editor_property("max_texture_size", 8192)
        # Never streamed: the sky is on screen whole from the first frame (the volume reads its blurred mips).
        tex.set_editor_property("never_stream", True)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        if not eal.save_loaded_asset(tex, only_if_is_dirty=False):
            unreal.log_error(f"SpaceGod sky import: could not save {path}")
        else:
            unreal.log(f"SpaceGod sky import: {path} imported from {source}")
