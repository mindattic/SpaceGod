#pragma once
// US-T142: every ship drawn from Ebal Studios' Ultimate Spaceships Creator (docs/ThirdPartyArt.md).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	class Faction;
	class SubsystemDef;
	enum class ShipClass : int;
	enum class CivilCraft : int;
	enum class SubsystemKind : int;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Builds a ship's model from Ultimate Spaceships Creator (US-T142), imported to /Game/ThirdParty/USC by
	/// tools/import-usc.ps1. Each hull class flies one of the pack's ships (<c>UscKit.cpp</c>'s table: Galactic Leopard
	/// capitals for the navies, Void Whale for the pirates and the heavy industrial hulls, Hyper Falcon, Night Aye and
	/// Strider Ox small craft, Force Badger local traffic), scaled to the game's model frame (one unit long, centred,
	/// nose +Z, up +Y) with its own proportions. A warship comes as the bow and stern pieces the import cut at
	/// ShipMeshFactory::SplitZ, so a destroyed one breaks in two. Engines get a thruster bell from the pack on every Engine
	/// hardpoint of the class's layout (the glows sit in them); gun turrets are the pack's turret and barrel modules.
	/// Each faction's livery is one of the pack's unified colours (navy by its colour's hue, pirates black, civilians
	/// white) with a light wash of the faction's own colour.
	///
	/// The pack is third-party art that is never committed. Without it, in a headless run (which cannot draw it) or with
	/// <c>-NoUSC</c>, <see cref="Available"/> is false and every caller falls back to the Star Sparrow kit, then to the
	/// procedural hulls.
	///
	/// Hooks for what comes next: every piece is its own named GameObject (<c>Hull:&lt;Atlas&gt;:&lt;Section&gt;</c>), every
	/// module bolted on is <c>Module:&lt;Name&gt;</c> under the section it sits in (per-module sectional destruction, US-T91);
	/// <see cref="Extent"/> gives the hull's real box in the model frame (shield arrays fitted to it, US-T102); and
	/// <see cref="ModelOf"/> / <see cref="Turret"/> are the one place a refit (hardpoints, the Mechlab, US-T89) would
	/// choose another hull or another gun.
	/// </summary>
	class UscKit
	{
	public:
		/// <summary>Where the import puts the pack.</summary>
		static constexpr const char* Root = "/Game/ThirdParty/USC";

		/// <summary>Whether ships are drawn from the pack: the game renders, the pack is imported and its in-game material is
		/// there. Worked out once. <c>-NoUSC</c> on the command line turns it off.</summary>
		static bool Available();

		/// <summary>
		/// Builds <paramref name="cls"/>'s model (or a local-traffic craft's, <paramref name="civil"/>) under
		/// <paramref name="parent"/> in <paramref name="faction"/>'s livery, every renderer added to
		/// <paramref name="renderers"/>, as StarSparrowKit::Build does: a warship in two sections (stern and bow), anything
		/// else as one returned as the stern with no bow. A truss freighter carries <paramref name="pods"/> containers named
		/// StarSparrowKit::CargoName + index. False (and nothing built) without the pack.
		/// </summary>
		static bool Build(Core::ShipClass cls, Core::CivilCraft civil, int id, const cs::Ref<Core::Faction>& faction,
			const cs::List<cs::Ref<Core::SubsystemDef>>* layout, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
			cs::Ref<Transform>& stern, cs::Ref<Transform>& bow, int pods = 0, int spheres = 0);

		/// <summary>A gun turret from the pack's modules: the traversing base or the barrels, sized as ShipMeshFactory::GetPart's,
		/// upright (+Y) and facing +Z in ship lengths. Navies get the Galactic Leopard turret, pirates the Void Whale one.
		/// False for a hardpoint the pack does not draw, or without the pack.</summary>
		static bool Turret(const cs::Ref<Transform>& parent, Core::SubsystemKind kind, const cs::Ref<Core::Faction>& faction, bool barrels,
			cs::List<cs::Ref<Renderer>>& into);

		/// <summary>The pack's ship (its model name, e.g. GalacticLeopard31) a class flies for a faction.</summary>
		static cs::String ModelOf(Core::ShipClass cls, Core::CivilCraft civil, int id, const cs::Ref<Core::Faction>& faction);

		/// <summary>The hull's box in the model frame (one unit long, nose +Z): false without the pack.</summary>
		static bool Extent(Core::ShipClass cls, Core::CivilCraft civil, int id, const cs::Ref<Core::Faction>& faction, Vector3& min, Vector3& max);

		/// <summary>The name of the unified colour a faction's livery is painted in (Grey, Red, Blue, Green, Purple, White, Black).</summary>
		static cs::String ColourOf(const cs::Ref<Core::Faction>& faction);

		/// <summary>Every ship model the game uses, for the lineup's contact sheet.</summary>
		static cs::List<cs::String> ModelNames();

		/// <summary>US-T133: the object path of every mesh and material instance the game draws from the pack, so the area
		/// streaming can load them in the background.</summary>
		static cs::List<cs::String> AssetPaths();

		/// <summary>One model by name, whole, nose +Z and <paramref name="length"/> long (the lineup).</summary>
		static bool Piece(const cs::Ref<Transform>& parent, const cs::String& model, float length, const cs::Ref<Core::Faction>& faction,
			cs::List<cs::Ref<Renderer>>& into);
	};
}
