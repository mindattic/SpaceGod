# Third-party art

Art packs from the owner's own asset libraries (Fab, Unity Asset Store). Their licences allow use in the game but
forbid redistributing the raw assets, and this repo is public, so **none of it is committed**:

- `Cpp/SourceArt/ThirdParty/` (the extracted source files) and
- `Cpp/Content/ThirdParty/` (the imported `.uasset`s)

are both in `.gitignore`. What is committed is the import script for each pack, so any machine that has the pack's
download can rebuild the assets exactly. Never `git add -f` anything under those two folders. A packaged build that
uses these assets must be built on a machine that has run the import.

## Star Sparrow (Ebal Studios)

- **Source:** Ebal Studios' "Star Sparrow" modular spaceship pack, from the owner's library. The download comes as
  `starsparrow_fbx.zip`, `_blender.zip` and `_obj.zip`; the import uses the **FBX** one. The zip carries no
  licence file; the store licence it was obtained under applies (no redistribution of the source assets).
- **Re-import:** close any editor that has the project open, then

  ```powershell
  tools/import-starsparrow.ps1 -Zip <path>\starsparrow_fbx.zip
  ```

  (`-Zip` defaults to `%USERPROFILE%\Downloads\starsparrow\starsparrow_fbx.zip`). It extracts the zip into
  `Cpp/SourceArt/ThirdParty/StarSparrow/` if that is not there yet, then runs `tools/import_starsparrow.py`
  headless (`UnrealEditor-Cmd -run=pythonscript`). Re-running is safe: assets are re-imported in place and the
  material instances keep their paths. The script logs a `StarSparrow: SUMMARY` line and every failure.
- **Result** (`/Game/ThirdParty/StarSparrow`):
  - `Meshes/SM_StarSparrow01`..`40`: the 40 prebuilt fighters, each one combined static mesh (a single node and
    no material slots in the FBX), nose +X, up +Z, centimetres (about 10-16 m long), 1.4k-3.7k triangles, pivot
    at the middle of the length. No Nanite (no gain at this density), no lightmap UVs. Simple collision: one
    18-DOP convex hull (the importer's auto collision; the FBX has no UCX hulls).
  - `Meshes/SM_StarSparrowModule_<Part>`: the 8 parts of `StarSparrowModules.fbx`: Core (hull with cockpit, 856
    tris), Engine, Thruster (nozzle), Plasma (gun pod), Weapon (barrel), Wing, Fin, Tail. The FBX lays them out in
    a row along the ship's length with no hierarchy, sockets or attach empties; each node sits at the part's own
    pivot: the centre for Core/Engine/Thruster/Plasma/Weapon, the root edge for Wing/Fin/Tail, which are modelled
    standing up (+Z) and need a roll to mount sideways. The import cancels each node's offset, so every part is
    about its own pivot. Module meshes are recreated (delete + rename) on each run; fighters, textures and
    materials are re-imported in place.
  - `Textures/T_StarSparrow_*`: one shared 2048 atlas set. Colour variants (Black, Blue, Cyan, Green, Grey, Orange,
    Purple, Red, White, Yellow), `Neutral` and `Emission` are sRGB; `Normal` uses normal-map compression;
    `Metallic`, `Glossiness`, `Roughness`, `Specular` and `Mask1..4` are linear (`TC_Masks`, sRGB off). The
    pack's `Roughness` map is exactly 1 - `Glossiness`; the material uses the glossiness one.
  - `Materials/M_StarSparrow`: master material. Parameters: `BaseColor` (painted variant), `Neutral`, `Mask1..4`,
    `Tint1..4`, static switch `UseLivery`, `Metallic`, `Glossiness` (roughness = 1 - glossiness), `Normal`,
    `Emission`, `EmissiveIntensity`. With `UseLivery` on, base colour = Neutral x lerp(1, Tint_i, Mask_i) for
    i = 1..4: the masks split the atlas into main hull (Mask1, ~49 %), dark trim (Mask2, ~29 %), light panels
    (Mask3, ~14 %) and accent lights (Mask4, ~0.1 %), and Neutral times per-mask tints reproduces every painted
    variant to within about 1-2 % (the fitted tints are in the script).
  - `Materials/MI_StarSparrow_<Colour>`: one instance per supplied colour (painted texture, with the fitted tints
    stored too), and `MI_StarSparrow_Livery`: `UseLivery` on, the template for faction colours.
    Every mesh gets `MI_StarSparrow_Grey` by default.
