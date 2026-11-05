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
  - `Materials/M_StarSparrowShip` (US-T110): the material the game draws the kit with. The game has no Unreal lights
    (every SpaceGodRender material is unlit and lights itself from the star(s) in `MPC_SpaceGod`), so this is the
    pack's atlas in the game's terms: Neutral times `Tint1..4` inside the masks, the pack's normal, metallic,
    glossiness and emission maps, lit by the star(s) as `M_Ship` is, then `SG_Expose3`. `_Tint`, `_Highlight` and
    `_EngineGlow` take ShipView's property block. Rebuild just this material with
    `tools/import-starsparrow.ps1 -MaterialOnly` (no zip needed once the textures are imported).

### Ships built from the kit (US-T110)

When the pack is imported, every ship is drawn from it (`Cpp/Source/SpaceGod/Rendering/StarSparrowKit.cpp`): a recipe
per hull class names parts with a place, a size and a turn in the game's model frame (one unit long, nose +Z, beam
about 0.2), scaled non-uniformly where that reads well. Fighters, interceptors, recon craft, couriers and the local
traffic (taxi, skiff, runabout, tourer) are the pack's prebuilt fighters with their span narrowed; everything else is
assembled from the eight modules, its engines on its class's Engine hardpoints so the glows sit in the bells. Warships
still come in a stern and a bow split at `SplitZ`; gun turrets are kit parts too (a pod barbette and Weapon barrels),
on the same mounts. Each faction gets a livery from its colour (`StarSparrowKit::Livery`): navies steel washed in
their colour, pirates soot-black with the clan's colour on the trim and red lights, civilians white with a stripe.

Without the pack (a fresh clone), in a headless run (`-nullrhi`, which cannot draw imported meshes), or with
`-NoStarSparrow`, `StarSparrowKit::Available()` is false and the game draws the procedural hulls and Kenney craft
exactly as before. `-SpaceGodShipLineup=<dir>` on the game's command line (it
quits when done), or the console command `SpaceGod.ShipLineup <dir>`, screenshots contact sheets of the kit's parts
and of every class in every livery, drawn by a camera of its own as the model viewer is (`Rendering/ShipLineup.cpp`).

## Ultimate Spaceships Creator (Ebal Studios, US-T142)

- **Source:** Ebal Studios' "Ultimate Spaceships Creator" (Fab listing e2e41618-a3e0-440b-be4b-78b01a1cd42f), bought by
  the owner. The Fab download is Blender only (`.blend` files saved by Blender 4.2, which Blender 3.3 cannot open);
  the import uses the separate FBX download, `usc_fbx.zip` (1.3 GB): 18 sets (AstroEagle ... VoidWhale) of prebuilt
  ships plus a `<Set>Modules.fbx` each, and each set's `Textures/` (the unified colours Red, Green, Blue, Purple, Grey
  and White, extra colours of its own, normal, metallic, roughness or glossiness, emission, and masks). The store
  licence applies: no redistribution of the source assets.
- **Re-import:** close any editor that has the project open, then

  ```powershell
  tools/import-usc.ps1 -Zip <path>\usc_fbx.zip   # -Blender <blender.exe> if it is not under Program Files
  ```

  (`-Zip` defaults to `%USERPROFILE%\Downloads\usc_fbx.zip`). It extracts the zip into
  `Cpp/SourceArt/ThirdParty/USC/USC_FBX/` if that is not there yet, runs `tools/usc_prepare.py` in Blender (3.3 or
  later, FBX in and out only) to cut the ships the game uses into pieces in `Cpp/SourceArt/ThirdParty/USC/Pieces/`,
  then `tools/import_usc.py` headless (`UnrealEditor-Cmd -run=pythonscript`). Re-running is safe; `-SkipPrepare`
  re-imports the pieces already cut. Both scripts log `USC: SUMMARY` and every failure.
- **The Blender step:** the pack's FBX ships are one mesh each, or two for Galactic Leopard and Void Whale (`Main`
  and `Parts`, each painted from its own texture atlas), with no material slots. For every ship in `MODELS` (kept in
  step with `Rendering/UscKit.cpp`) it writes each mesh centred on the whole ship's box, nose -Y and up +Z in Blender,
  as `SM_USC_<Model>_<Main|Parts|Hull>_Whole.fbx`; a warship (battleship, carrier, gate breaker, cruiser, command
  cruiser, destroyer, frigate, corvette) is cut across its length at `ShipMeshFactory::SplitZ` (6 % of its length aft of
  the middle), the cut capped, as `..._Bow.fbx` and `..._Stern.fbx`, so the two meet exactly and a destroyed ship
  breaks in two. The modules the game bolts on (Galactic Leopard's turret, gun, thruster and cargo, Void Whale's turret
  base, cannon, laser and thruster, a few more for later use) are written centred on their own boxes as
  `SM_USC_Mod_<Set><Part>.fbx`.
- **Result** (`/Game/ThirdParty/USC`):
  - `Meshes/SM_USC_*`: those pieces (no collision, no Nanite). LODs: the script asks `StaticMeshEditorSubsystem` for
    two reduced LODs (50 % and 20 %) and logs whether the commandlet had it; when it does not, the area streaming's
    detail rings (US-T133) carry the load, as they do for every other hull.
  - `Textures/T_USC_<Atlas>_<Map>`: per atlas (`GalacticLeopard_Main`, `GalacticLeopard_Parts`, `VoidWhale_Main`,
    `VoidWhale_Parts`, `StriderOx`, `NightAye`, `HyperFalcon`, `ForceBadger`) the unified colours (and Black where the
    set has it), normal, metallic, roughness (or glossiness) and emission, at 2048 at most.
  - `Materials/M_USCShip`: the in-game material, the pack's maps in the game's terms (unlit, lit from the star(s) in
    `MPC_SpaceGod` as `M_StarSparrowShip` is, then `SG_Expose3`): the painted colour times `Tint` (the faction's wash)
    and `_Tint`, `_Highlight`, `_EngineGlow` from ShipView's property block. `Materials/MI_USC_<Atlas>_<Colour>`: one
    instance per atlas and colour.

### Ships built from the pack (US-T142)

When the pack is imported every hull is drawn from it (`Cpp/Source/SpaceGod/Rendering/UscKit.cpp`) ahead of the Star
Sparrow kit: each class flies one of the pack's prebuilt ships, scaled to the game's model frame with its own
proportions. The navies' warships are Galactic Leopard capitals and the pirates' Void Whale ones; the work hulls (miner,
ore and capture tugs) are Void Whale, the lane tender, gate picket, explorer and truss freighter Galactic Leopard;
fighters, interceptors, recon craft and couriers are Hyper Falcon, bombers Night Aye, gunships and assault shuttles
Strider Ox, and the local traffic Force Badger. Warships come in the bow and stern pieces; a thruster bell from the
pack goes on every Engine hardpoint (the glows sit in it); gun turrets are the pack's turret and barrel modules on the
same mounts; a truss freighter carries the pack's cargo modules, as many as its load. A livery is one of the unified
colours (`UscKit::ColourOf`): a navy the one nearest its own colour's hue, pirates Black (Red where a set has no
black), civilians White, each with a light wash of the faction's colour.

Hooks for what comes next: every hull piece is its own GameObject (`Hull:<Atlas>:<Section>`) and every module
`Module:<Name>` under the section it is in (per-module sectional destruction, US-T91); `UscKit::Extent` is the hull's
real box in the model frame (shield arrays, US-T102); `UscKit::ModelOf` and `UscKit::Turret` are where a refit or the
Mechlab (US-T89) would choose another hull or gun.

The order is USC, then Star Sparrow, then procedural: without USC (or with `-NoUSC`) the Star Sparrow kit draws them,
without that too (or headless) the procedural hulls. `-SpaceGodShipLineup=<dir>` adds a `usc-models` sheet of every
model the classes use.

## Space Station Creator Remastered (Ebal Studios, US-T143)

- **Source:** Ebal Studios' "Space Station Creator Remastered", bought by the owner; the import uses its FBX
  download, `spacestationscreator_fbx.zip` (300 MB): four sets (Bridge, Cylinderical, Hangars, Storage) of 20 stations
  each with a `<Set>Modules.fbx`, 30 `Combined` stations mixing all four, and `Textures/<Set>/` (Red, Green, Blue,
  Purple, Grey, White, Brown, Silver, normal, metallic, roughness or glossiness, emission, masks). The store licence
  applies: no redistribution.
- **Re-import:** `tools/import-ssc.ps1 -Zip <path>\spacestationscreator_fbx.zip` (default
  `%USERPROFILE%\Downloads\spacestationscreator_fbx.zip`). The same pipeline as USC: the zip into
  `Cpp/SourceArt/ThirdParty/SSC/`, `tools/usc_prepare.py ... ssc` in Blender (the stations in `SSC_MODELS`, whole,
  centred, a piece per atlas; the Hangars girders and hangars as modules), then `tools/import_usc.py` with
  `USC_PACK=ssc` into `/Game/ThirdParty/SSC` (`SM_SSC_*`, `T_SSC_*`, `M_SSCStation`, `MI_SSC_<Atlas>_<Colour>`).
- **Drawn** (`Cpp/Source/SpaceGod/Rendering/SscKit.cpp`), ahead of the procedural models:
  - orbital structures (`Views/StructureViews.cpp`): factories on the Storage complexes (by industry), depots on the
    tank farm, habitats on the wheel and gateways on the gyroscope ring (both turning about +Z as before); shipyards
    keep their slip and the ISS and Tiangong their real shapes;
  - free-flying stations (Rigil Gate, Toliman Forge, Blackhollow: `BodyKind::Station`): one of the big `Combined`
    complexes, by name (`Views/BodyView.cpp`);
  - Void Stations (US-T149): the pack's largest prebuilt station, `Combined20`, whole, in place of the procedural
    Dyson-sphere generator (`VoidStationModel` and its core, rings and glow are now only the fallback without the pack);
  - the megastructure (US-T130's void site, US-T149): the next grandest prebuilt complex, `Combined28`, whole;
  - derelict stations (US-T131): `SscKit::Derelict(parent, seed, size, into)` draws one of three prebuilt complexes
    no working station uses (`Combined16`, `Combined24`, `Combined10`) darkened and lightless with a module torn off
    and tumbling beside it, and `SscKit::Station(..., Condition::Derelict)` any station so; the void-content work
    places them (GraveyardView), this only draws them.

  US-T149 (the owner: "use the prebuilt ships and space stations first before you invent your own"): every station
  role is one of the pack's complete prebuilt stations, used whole and one per role; nothing is assembled from its
  parts. The ships already follow it: each hull class flies one of USC's complete prebuilt example ships, the pack's
  modules only bolted on where the game needs a part of its own (thruster bells on Engine hardpoints, turrets on gun
  mounts, the truss freighter's cargo).

  A station is painted in one of the pack's colours (a navy's nearest hue, civilians White, pirates Brown, unowned
  Grey) with a light wash of its owner's colour. Without the pack, headless, or with `-NoSSC`, every station is
  procedural as before. The lineup adds `ssc-stations` and `ssc-archetypes` sheets.

## Alien Space Nebula 1 (Skybox), by Jungle Jim (US-T145)

The one third-party piece that **is** committed: its licence allows redistribution with credit.

- **Source:** "Alien Space Nebula 1 (Skybox)" by Jungle Jim (sketchfab.com/jungle_jim),
  sketchfab.com/3d-models/alien-space-nebula-1-skybox-4a4e7aec37c04320a09ac4c7434b8810. The FBX download (a
  960-triangle sky sphere) carries `textures/alien-space-nebula_7680x3840.png`, a 7680 x 3840 (2:1) equirectangular
  painting: a violet, teal and gold nebula round a white-hot core, dust lanes, filaments and a dense star field. Only
  the texture is used (any sphere at infinity draws it the same).
- **Licence:** CC BY 4.0 (creativecommons.org/licenses/by/4.0): use, change and redistribute, commercially too, with
  credit. Credit line, also on the sky in game (Settings and the Shift+K toast name it, and the manual's "The sky"):
  *"Alien Space Nebula 1 (Skybox)" by Jungle Jim, CC BY 4.0.* We use it changed: stretched to 8192 x 4096 and, in the
  painted-volume mode, projected into a 3D volume.
- **Imported** with `Shared/Plugins/SpaceGodRender/Content/Python/spacegod_import_hero_sky.py` (set `SPACEGOD_SKY_PNG`
  to the PNG; run headless with `-run=pythonscript`) to
  `Shared/Plugins/SpaceGodRender/Content/Resources/Skyboxes/jungle_jim_alien_space_nebula_8k.uasset`: sRGB, default
  compression, stretched to a power of two for the whole mip chain, never streamed, wrap U / clamp V.
- **Drawn** (`Cpp/Source/SpaceGod/Views/SkyLayers.cpp`): the hero system's sky (the first violet-storm system beyond
  Sol), as a sky sphere (`sg.SkyMode sphere`) and as a painted volume (`sg.SkyMode volume`, the default); its core's
  direction (longitude -3.16, latitude +1.05 degrees in its frame) is where the hero system's fill light comes from.
