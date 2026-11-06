#pragma once
// US-T172: real light on the hulls. See HullLighting below and docs/ThirdPartyArt.md.

#include "Rendering/Pipeline/RenderBackend.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Ship;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// US-T172: how the prebuilt hulls (Ultimate Spaceships Creator ships, Space Station Creator stations, Star Sparrow
	/// craft) are lit. They used to draw with self-lit materials that read the star from MPC_SpaceGod (as every
	/// SpaceGodRender shader does): no shadows, no light from the sky, a Blinn highlight and a flat night-side fill, which
	/// is what made them look flat. Now they draw with Unreal's Default Lit PBR (M_USCShipLit, M_SSCStationLit,
	/// M_StarSparrowShipLit: the packs' own normal, metallic and roughness maps) under real Unreal lights, the way EVE,
	/// Elite or Star Citizen light a ship in the dark:
	/// <list type="bullet">
	/// <item>the star as one hard key light (a movable directional light from the star, so it reaches across any distance at
	/// either scale) with cascaded shadows whose reach is fitted each frame to the hulls in view (a fighter a millimetre
	/// across at the realistic scale and a two-kilometre carrier at the game scale both self-shadow), contact shadows for
	/// the finest greebles, and cut off by a world between the camera and the star (an eclipse, with its penumbra);</item>
	/// <item>a companion star (or US-T132's fill from the painted sky's bright heart) as a second, unshadowed light;</item>
	/// <item>the system's sky (US-T132 SystemSkies, US-T145 sky layers) as image-based light: a sky light captured from
	/// the sky alone, so the shadow side of a hull takes the nebula's colour instead of going black;</item>
	/// <item>planetshine: near a world, its lit face bounces light (its own colour, by how much of its lit side the camera
	/// sees and how big it looms) onto the hulls beside it;</item>
	/// <item>a faint cool rim, a floor of ambient and the packs' emissive windows and panel lights (in the materials), and
	/// nav lights on every hull: red to port, green to starboard, a white strobe on top, a white stern light and, on big
	/// hulls, a slow red beacon underneath;</item>
	/// <item>lamps: headlights on the small craft nearest the camera, and a Void Station's Embers (US-T150) as real lamps
	/// lighting the station round them.</item>
	/// </list>
	/// The thumbnails' and the lineup's fixed light (RenderBackend::OverrideLighting, US-T116) is honoured: while it holds,
	/// the key and fill come from it and the sky, planetshine, lamps and Embers are off, so the snapshots stay as they were
	/// framed. <c>-LegacyHullLighting</c> (or <c>sg.HullLighting 0</c> before the first hull is built) keeps the old self-lit
	/// materials and no Unreal lights at all, for comparison. Nothing here touches the simulation.
	/// </summary>
	class HullLighting
	{
	public:
		/// <summary>The lit materials and real lights, unless the legacy path was asked for. Worked out once.</summary>
		static bool Enabled();

		/// <summary>The lit variant of a pack material or instance path (".../MI_USC_HyperFalcon_Grey" -> "..._Lit",
		/// ".../M_StarSparrowShip" -> "...Lit") when enabled and the asset is imported; otherwise the path as given.</summary>
		static cs::String LitVariant(const cs::String& path, const char* suffix = "_Lit");

		/// <summary>From the render backend: the star light now in effect (an override's, or the game's graded) and whether
		/// it is an override.</summary>
		static void SetStars(const StarLighting& light, bool overridden);

		/// <summary>From the render backend: the place's grade changed (a new system, a new sky): the sky light recaptures.</summary>
		static void SetGrade(const SystemGrade& grade);

		/// <summary>Each frame after the app's update (null in a scene without the app): eclipse, planetshine, the shadows'
		/// reach, the nav lights, the lamps and the Embers.</summary>
		static void Tick(UnitySpaceApp* app, float dt);

		/// <summary>Nav lights on a hull just built under <paramref name="root"/> (its frame: nose +Z, one unit long), placed
		/// on the hull's own extremes (its wingtips, its top, its keel, its stern).</summary>
		static void AddNavLights(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull);

		/// <summary>What the lights are doing now (sg.Lighting): intensities, shadow reach, eclipse, planetshine, lamps.</summary>
		static cs::String Describe();
	};
}
