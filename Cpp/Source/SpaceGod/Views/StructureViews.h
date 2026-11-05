#pragma once
// Port of Unity.SpaceGod Runtime/Views/StructureViews.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"
#include "Ships.h" // ShipClass (by value, in View::HullClass)

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class OrbitalStructure;
	class DockingRing;
	class Faction;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// What a system has in orbit: settlements' yards (with the hull on the slip framed out rib by rib
	/// and plated as the yard banks build points), factory stations and habitats, the real spacecraft,
	/// and the docking rings hovering over their cities. Drawn at the ship scale in their body's
	/// neighbourhood, like parked ships; hidden when smaller than a few pixels (the HUD shows icons).
	/// </summary>
	class StructureViews final
	{
		class View final
		{
		public:
			cs::Ref<Core::OrbitalStructure> S;
			cs::Ref<GameObject> Root;
			cs::List<cs::Ref<Renderer>> Renderers;
			cs::Ref<Transform> Slip;
			cs::List<cs::Ref<Transform>> Ribs;
			cs::Ref<GameObject> Hull;
			cs::Nullable<Core::ShipClass> HullClass;
			cs::Ref<Core::Faction> HullOwner;
			/// <summary>The hull's Star Sparrow kit parts (US-T110), shown stern first as the plating goes forward.</summary>
			cs::List<cs::Ref<Renderer>> HullParts;
			float Spin = 0;
			cs::Ref<MeshFilter> Model;
			cs::Ref<Core::Faction> Owner;
			/// <summary>US-T143: the Space Station Creator station drawn in the procedural model's place, when the pack is there.</summary>
			cs::Ref<GameObject> Station;
			/// <summary>How many of <see cref="Renderers"/> are the procedural model's (the station's follow them).</summary>
			int BaseRenderers = -1;
		};

		/// <summary>US-T143: (re)builds a structure's Space Station Creator model in its owner's livery, hiding the procedural
		/// one; leaves the procedural model for a kind the pack does not draw or without the pack.</summary>
		static void AttachStation(const cs::Ref<View>& v);

		class RingView final
		{
		public:
			cs::Ref<Core::DockingRing> Ring;
			cs::Ref<GameObject> Root;
			cs::Ref<UnityCompat::Renderer> Renderer;
			cs::Ref<MeshFilter> Filter;
			cs::Ref<Core::Faction> Owner;
		};

		UnitySpaceApp* app;
		cs::List<cs::Ref<View>> views;
		cs::List<cs::Ref<RingView>> rings;
		MaterialPropertyBlock block;
		static constexpr const char* EngineGlowId = "_EngineGlow"; // Shader.PropertyToID("_EngineGlow")
		static constexpr const char* EngineColorId = "_EngineColor"; // Shader.PropertyToID("_EngineColor")
		static constexpr const char* ColorId = "_Tint"; // Shader.PropertyToID("_Tint")
		static constexpr const char* HighlightId = "_Highlight"; // Shader.PropertyToID("_Highlight")

	public:
		/// <summary>Every structure's scene position this frame, for the HUD and picking.</summary>
		cs::Dictionary<cs::Ref<Core::OrbitalStructure>, Core::DVec3> Scenes;
		cs::Dictionary<cs::Ref<Core::DockingRing>, Core::DVec3> RingScenes;

		/// <summary>Drawn size (scene units) of each structure and ring this frame.</summary>
		// C#'s Dictionary<object, double>: keyed by cs::Ref<void>, which both cs::Ref<OrbitalStructure> and
		// cs::Ref<DockingRing> convert to implicitly (hashed and compared by the object's address, as C#'s reference keys).
		cs::Dictionary<cs::Ref<void>, double> Sizes;

		/// <summary>A docking ring is drawn this many km across (fiction: the size of a city district).</summary>
		static constexpr double RingDiameterKm = 10;

		StructureViews(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

	private:
		cs::Ref<View> Create(const cs::Ref<Core::OrbitalStructure>& s, const cs::Ref<Transform>& parent);

		/// <summary>A structure's model, in its owner's livery.</summary>
		static cs::Ref<Mesh> ModelMesh(const cs::Ref<Core::OrbitalStructure>& s);

		static cs::Ref<Renderer> Part(const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh, const cs::String& name);

	public:
		/// <summary>Where everything is this frame (before the camera moves, so it can follow a structure).</summary>
		void Layout();

		void Update(bool visible);

	private:
		/// <summary>Ribs go up stern to bow as the yard banks build points; then the hull is plated over them.</summary>
		void UpdateSlip(const cs::Ref<View>& v);
	};
}
