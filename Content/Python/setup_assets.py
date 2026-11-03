"""Headless asset setup: imports the textures and builds M_Body and M_Ring without opening the editor UI.

Run with tools/setup-assets.ps1 (UnrealEditor-Cmd -run=pythonscript). The editor's own startup hook
(init_unreal.py) does the same work on first open; this exists so a build machine never needs the UI.
"""
import unreal
import init_unreal

init_unreal.import_textures()
init_unreal.build_body_material()
init_unreal.build_ring_material()
unreal.EditorAssetLibrary.save_directory("/Game/UnrealSpaceGod", only_if_is_dirty=True, recursive=True)
unreal.log("Unreal.SpaceGod: headless asset setup done")
