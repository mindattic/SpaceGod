#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/ShipModels.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

#include <tuple>

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Builds the model under a ship's root: a Kenney Space Kit craft (CC0) for small civilian
	/// classes, or a procedural hull for warships (and as a fallback), re-materialed with the Sun-lit
	/// ship shader. The Kenney craft themselves are pre-normalised offline (one unit long on Z, centred,
	/// nose on +Z, yaw already baked into the mesh data) by MindAttic.AssetForge's mesh pipeline, not at
	/// runtime here — see github.com/mindattic/AssetForge and Resources/Ships/*.glb.
	/// </summary>
	class ShipModels
	{
	public:
		/// <summary>The vertex-coloured, Sun-lit material every procedural hull uses.</summary>
		static cs::Ref<Material> ProceduralMaterial();

		/// <summary>
		/// Builds the hull under <paramref name="parent"/>. Warships come as two sections (stern and
		/// bow) that can break apart; anything else is one piece, returned as the stern with no bow.
		/// </summary>
		static void Build(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
			cs::Ref<Transform>& stern, cs::Ref<Transform>& bow);

		/// <summary>The name of a truss freighter's one hull section, whose mesh its view swaps as its load changes.</summary>
		static constexpr const char* TrussSection = "Truss";

		/// <summary>A truss freighter's hull with the pods its load fills and the spheres it carries.</summary>
		static cs::Ref<Mesh> TrussMesh(const cs::Ref<Core::Ship>& ship);

		/// <summary>What a truss freighter's model shows: its pods, how many of them its load fills, and its spheres. A hull
		/// with no trade hold of its own (a thumbnail's, a gate constructor's) is drawn with every pod on.</summary>
		static std::tuple<int, int, int> TrussLoad(const cs::Ref<Core::Ship>& ship);

		/// <summary>A hardpoint part (or scar) under a hull section, using the procedural material.</summary>
		static cs::Ref<MeshRenderer> Part(const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh, const cs::String& name);

		/// <summary>
		/// Whether <paramref name="ship"/>'s model can be built this frame without waiting on the disk (U.SPC-T414): a
		/// procedural hull always can; a Kenney craft once its model has been loaded in the background (asked for here
		/// if it has not been).
		/// </summary>
		static bool Ready(const cs::Ref<Core::Ship>& ship);

		/// <summary>The Kenney craft a ship is drawn as, or null for a procedural hull.</summary>
		static cs::String KenneyModelFor(const cs::Ref<Core::Ship>& ship);

		/// <summary>The seamless sci-fi panel texture every hull is plated with (see Ship.shader): set on the material
		/// as soon as it has loaded in the background (at once if it already has).</summary>
		static void Plate(const cs::Ref<Material>& m, float tilesPerUnit);

	private:
		static void BuildHull(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
			cs::Ref<Transform>& stern, cs::Ref<Transform>& bow);
		static cs::Ref<Transform> Section(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, int section,
			const cs::String& name);
		static cs::Ref<Transform> Section(const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, const cs::Ref<Mesh>& mesh, const cs::String& name);
		static cs::Ref<GameObject> InstantiateKenney(const cs::String& modelName, const cs::Ref<Core::Faction>& faction, const cs::Ref<Transform>& parent,
			cs::List<cs::Ref<Renderer>>& renderers);
		static cs::Ref<GameObject> Normalise(const cs::Ref<GameObject>& source, const cs::String& modelName);
		static cs::List<cs::Ref<Material>> MaterialsFor(const cs::String& modelName, const cs::Ref<Core::Faction>& faction, const cs::Ref<GameObject>& prototype);

		static constexpr const char* PlatingPath = "Textures/ship_plating";
	};
}
