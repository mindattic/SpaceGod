#pragma once
// US-T110: every ship drawn from one parts kit, Ebal Studios' Star Sparrow (docs/ThirdPartyArt.md).

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
	/// Builds a ship's model from the Star Sparrow kit (US-T110): its 40 prebuilt fighters and its 8 module parts (Core,
	/// Engine, Thruster, Plasma, Weapon, Wing, Fin, Tail), imported to /Game/ThirdParty/StarSparrow by
	/// tools/import_starsparrow.py. Each hull class has a recipe (<c>StarSparrowKit.cpp</c>): parts by name with a place,
	/// a size and a turn in the game's model frame (one unit long, centred, nose +Z, up +Y, beam about 0.2), scaled to
	/// it non-uniformly where that reads well. The small craft are the pack's own fighters; everything bigger is
	/// assembled from the modules, its engines on its class's Engine hardpoints so the glows sit in the bells.
	///
	/// The pack is third-party art that is never committed: a clone without it (or a headless run, which cannot draw it)
	/// has <see cref="Available"/> false, and every caller falls back to the procedural hulls.
	/// </summary>
	class StarSparrowKit
	{
	public:
		/// <summary>Where the import puts the pack.</summary>
		static constexpr const char* Root = "/Game/ThirdParty/StarSparrow";

		/// <summary>Whether ships are drawn from the kit: the game renders, the pack is imported and its in-game material
		/// is there. Worked out once. <c>-NoStarSparrow</c> on the command line turns it off (the procedural look).</summary>
		static bool Available();

		/// <summary>
		/// Builds <paramref name="cls"/>'s model (or a local-traffic craft's, <paramref name="civil"/>) under
		/// <paramref name="parent"/> in <paramref name="faction"/>'s livery, every renderer added to
		/// <paramref name="renderers"/>. A warship comes in two sections split at ShipMeshFactory::SplitZ (each part
		/// under the one its middle is in); anything else is one, returned as the stern with no bow. Engines go on the
		/// Engine hardpoints of <paramref name="layout"/> when it has them. A truss freighter carries
		/// <paramref name="pods"/> containers (see <see cref="CargoName"/>) and <paramref name="spheres"/> fuel tanks.
		/// False (and nothing built) without the kit.
		/// </summary>
		static bool Build(Core::ShipClass cls, Core::CivilCraft civil, int id, const cs::Ref<Core::Faction>& faction,
			const cs::List<cs::Ref<Core::SubsystemDef>>* layout, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
			cs::Ref<Transform>& stern, cs::Ref<Transform>& bow, int pods = 0, int spheres = 0);

		/// <summary>The layout whose Engine hardpoints a class's kit model puts its engines on: its stock design's (or
		/// <paramref name="fitted"/>, a ship's own refit), null for a class with no combat design.</summary>
		static const cs::List<cs::Ref<Core::SubsystemDef>>* LayoutOf(Core::ShipClass cls, const cs::List<cs::Ref<Core::SubsystemDef>>* fitted);

		/// <summary>The name of a truss freighter's cargo containers (one GameObject each, front pair first) so its view
		/// can show as many as its load fills.</summary>
		static constexpr const char* CargoName = "Cargo";

		/// <summary>
		/// A gun turret's model from the kit (US-T110): the traversing base (a Plasma pod squashed into a barbette) or the
		/// elevating barrels (Weapon barrels, two or three by the gun), sized as ShipMeshFactory::GetPart's, upright
		/// (+Y) and facing +Z, in ship lengths, each renderer added to <paramref name="into"/>. US-T117: the base of a
		/// missile, hive, interceptor, rocket or torpedo launcher or a shield emitter is a kit part too (a plated box with
		/// tubes or cells, a breech and muzzle bell, a column under a dish) while the kit's own hulls are in use (not under
		/// Ultimate Spaceships Creator's). False (nothing built) for a hardpoint the kit does not draw (masts, hangars) or
		/// without the kit.
		/// </summary>
		static bool Turret(const cs::Ref<Transform>& parent, Core::SubsystemKind kind, const cs::Ref<Core::Faction>& faction, bool barrels,
			cs::List<cs::Ref<Renderer>>& into);

		/// <summary>The faction's livery on the kit's material: Neutral times four mask tints worked out from its colour
		/// (navy steel, pirate black and the clan's colour, civilian white with its stripe). One shared material each.</summary>
		static cs::Ref<Material> Livery(const cs::Ref<Core::Faction>& faction);

		/// <summary>The kit's parts by name, for the lineup's contact sheet (Rendering/ShipLineup.cpp): the 8 modules then
		/// the 40 fighters.</summary>
		static cs::List<cs::String> PieceNames();

		/// <summary>US-T133: the object path of every mesh the kit draws (the 8 modules, the 40 fighters) and of its
		/// material, so the area streaming can load them in the background before a hull built from them is ever wanted
		/// (each is otherwise loaded on the frame its first hull is built).</summary>
		static cs::List<cs::String> AssetPaths();

		/// <summary>One piece by name, nose +Z and scaled to <paramref name="length"/> (debug and the lineup).</summary>
		static cs::Ref<Renderer> Piece(const cs::Ref<Transform>& parent, const cs::String& name, float length, const cs::Ref<Core::Faction>& faction);
	};
}
