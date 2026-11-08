#include "Credits.h"

#include "Rendering/Skyboxes.h"

namespace SpaceGod::Core
{
	namespace
	{
		cs::Ref<CreditSection> Section(const cs::String& title, std::initializer_list<CreditEntry> entries)
		{
			auto s = cs::New<CreditSection>(title);
			for (const auto& e : entries) s->Entries.Add(e);
			return s;
		}

		cs::List<cs::Ref<CreditSection>> Build()
		{
			cs::List<cs::Ref<CreditSection>> all;

			all.Add(Section("THE TEAM", {
				CreditEntry("Ryan DeBraal", "Design, direction, programming"),
				CreditEntry("MindAttic", "Studio"),
				CreditEntry("Claude Code (Anthropic)", "AI coding agents, working to the team's tickets"),
			}));

			all.Add(Section("THE ORIGINAL GAME", {
				CreditEntry("Unity.SpaceGod", "MindAttic's original game, made in Unity: its simulation core and runtime are what this game "
					"ports faithfully to C++ (docs/MIGRATION.md)"),
				CreditEntry("The race", "A C++ port and a C# (UnrealSharp) port raced head to head; the C++ one won and became this game "
					"(docs/RACE.md)"),
				CreditEntry("Unity", "The engine the original was made in. Unity is a trademark of Unity Technologies; this game reproduces "
					"its immediate-mode GUI's shape in its own code and carries none of its code"),
			}));

			all.Add(Section("ENGINE", {
				CreditEntry("Unreal Engine 5.8", "Epic Games, Inc. Unreal and its logo are Epic's trademarks or registered trademarks "
					"in the United States and elsewhere", "Unreal Engine EULA (unrealengine.com/eula)"),
				CreditEntry("Unreal plugins", "Procedural Mesh Component; Python Editor Script Plugin and Editor Scripting Utilities "
					"(asset imports, in the editor only)", "Part of Unreal Engine, under its EULA"),
			}));

			all.Add(Section("SHIPS AND STATIONS", {
				CreditEntry("Ultimate Spaceships Creator", "Ebal Studios: every hull class's prebuilt ship and the modules bolted on",
					"Fab standard licence: used in the game, source assets not redistributed"),
				CreditEntry("Star Sparrow", "Ebal Studios: the modular fighter kit, drawn when Ultimate Spaceships Creator is not there",
					"Store licence: used in the game, source assets not redistributed"),
				CreditEntry("Space Station Creator Remastered", "Ebal Studios: every station, structure, Void Station and derelict",
					"Store licence: used in the game, source assets not redistributed"),
				CreditEntry("Space Kit", "Kenney (kenney.nl): the small civilian craft", "CC0 1.0 (public domain)"),
				CreditEntry("MetalPlates015A", "ambientCG (ambientcg.com): the plating on every procedural hull and station",
					"CC0 1.0 (public domain)"),
			}));

			auto skies = cs::New<CreditSection>("SKIES");
			skies->Entries.Add(CreditEntry("Alien Space Nebula 1 (Skybox)", "Jungle Jim (sketchfab.com/jungle_jim): the hero system's sky, "
				"stretched to 8192 x 4096 and projected into a painted volume", "CC BY 4.0"));
			for (const auto& sky : SkyboxCatalog::BuiltIn())
			{
				if (sky->Credit.empty() || sky->Licence == "Part of the game") continue; // the game's own stars need no credit
				skies->Entries.Add(CreditEntry(sky->Name, sky->Credit, sky->Licence));
			}
			all.Add(skies);

			all.Add(Section("PLANET AND MOON MAPS", {
				CreditEntry("NASA SVS CGI Moon Kit", "Ernie Wright, NASA Goddard Scientific Visualization Studio: the Moon's colour "
					"(LRO LROC WAC) and relief (LRO LOLA)", "Public domain (NASA)"),
				CreditEntry("USGS Astrogeology Science Center", "Global mosaics from MESSENGER, Viking, Galileo, Voyager, Cassini, Dawn, "
					"New Horizons and Mars Express (NASA, JPL, JHUAPL, SwRI, ESA, DLR, FU Berlin): Mercury, Mars, Io, Europa, Ganymede, "
					"Callisto, Titan, Enceladus, Phobos, Deimos, Ceres, Pluto, Charon, Triton, and Mercury's relief", "Public domain (NASA/USGS)"),
				CreditEntry("Hubble Maps Jupiter in 4K", "NASA SVS; Hubble WFC3 OPAL programme (Simon, Wong and Orton)", "Public domain (NASA)"),
				CreditEntry("Mars relief", "MGS MOLA MEGDR, the PDS Geosciences Node", "Public domain (NASA)"),
				CreditEntry("JPL Solar System Simulator", "Miranda, Titania and Oberon (NASA/JPL/Caltech/USGS, Voyager imagery)",
					"JPL image-use policy: free use with credit"),
				CreditEntry("Solar System Scope", "Textures by Solar System Scope (solarsystemscope.com/textures): Saturn, Uranus, Neptune",
					"CC BY 4.0"),
			}));

			all.Add(Section("DATA", {
				CreditEntry("JPL Small-Body Database", "111,267 asteroids and Jupiter Trojans (NASA/JPL)", "Public domain (NASA)"),
				CreditEntry("JPL Solar System Dynamics", "Planetary and satellite orbital elements (ssd.jpl.nasa.gov)", "Public domain (NASA)"),
				CreditEntry("IAU WGCCRE", "Poles, prime meridians and rotation (Archinal et al. 2018)"),
				CreditEntry("NASA planetary fact sheets", "Radii, masses and the rest of every body's numbers", "Public domain (NASA)"),
				CreditEntry("NASA Exoplanet Archive", "The planets of the nearby star systems (NASA/Caltech IPAC)", "Public domain (NASA)"),
				CreditEntry("Gaia DR3", "The nearby stars (ESA/Gaia/DPAC)", "Free use with acknowledgement"),
				CreditEntry("Gas giant weather", "Zonal winds after Porco et al. 2003, Sanchez-Lavega et al. 2000, Sromovsky and Fry 2005, "
					"Hammel et al. 2001, Sromovsky et al. 1993; the Great Red Spot from the BAA Jupiter Section and JUPOS"),
			}));

			all.Add(Section("CODE AND LIBRARIES", {
				CreditEntry("zlib", "Jean-loup Gailly and Mark Adler: the saves' compression, through Unreal Engine", "zlib licence"),
				CreditEntry("Space 3D", "wwwtyro (github.com/wwwtyro/space-3d): the method behind each system's own nebulae",
					"Unlicense (public domain)"),
				CreditEntry("MindAttic.Capture", "MindAttic: the recorder behind -capture (Vendor/)"),
				CreditEntry("NAudio", "Mark Heath: audio capture for the recorder", "MIT licence"),
				CreditEntry("Vortice.Windows", "Amer Koleci: Direct3D 11 and DXGI for the recorder", "MIT licence"),
				CreditEntry(".NET", "The .NET Foundation and Microsoft: the recorder's runtime, System.Drawing.Common and "
					"Microsoft.Extensions abstractions", "MIT licence"),
				CreditEntry("FFmpeg", "The FFmpeg developers: video encoding for recordings (not bundled; the player's own copy)",
					"LGPL 2.1 or later / GPL, as built"),
			}));

			all.Add(Section("TOOLS", {
				CreditEntry("Blender", "The Blender Foundation: cutting the packs' ships and stations into pieces", "GPL"),
				CreditEntry("Pillow", "Alex Clark and contributors: resampling the planet maps", "MIT-CMU (HPND)"),
				CreditEntry("Visual Studio", "Microsoft: the C++ compiler"),
			}));

			return all;
		}
	}

	const cs::List<cs::Ref<CreditSection>>& Credits::Sections()
	{
		static const cs::List<cs::Ref<CreditSection>> value = Build();
		return value;
	}
}
