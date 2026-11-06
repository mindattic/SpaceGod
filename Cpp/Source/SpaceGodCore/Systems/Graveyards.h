#pragma once
// US-T131: ship graveyards (fields of derelict hulks and old battle wrecks) and derelict space stations, in the void and in
// some star systems.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Interior;
	class World;
	class Wreck;
	enum class ShipClass : int;
	enum class Good : int;

	/// <summary>One big hulk of a graveyard: what it was, where it lies (km from the root) and what is still aboard.</summary>
	struct SPACEGODCORE_API GraveHulk
	{
		cs::String Name;
		ShipClass Class{};
		DVec3 OffsetKm;
		Good Cargo{};
		double CargoTonnes = 0;
	};

	/// <summary>A derelict station: dark, holed, its modules broken off, salvageable, and (later) explorable by marines.</summary>
	struct SPACEGODCORE_API DerelictStation
	{
		cs::String Name;
		DVec3 OffsetKm;
		/// <summary>Its length, km (its model is drawn this size).</summary>
		double SizeKm = 3;
		uint32_t Seed = 0;
		/// <summary>US-T85: not a station at all but the Unison's cube (Wolf 359's): drawn as a lattice-skinned cube, faintly
		/// lit, turning slowly rather than tumbling.</summary>
		bool Cube = false;
		/// <summary>What the log says of it when a helm first comes in (empty for an ordinary derelict).</summary>
		cs::String Summary;
	};

	/// <summary>A graveyard: its hulks and stations, the dense field of drawn wreckage round them and the light it is seen in.</summary>
	class SPACEGODCORE_API Graveyard final
	{
	public:
		cs::String Name;
		DVec3 CentreKm;
		double RadiusKm = 0;

		/// <summary>
		/// Warm: backlit by a hot star close by, an orange haze of dust catching the light. Cold: by a cool or far star, an
		/// icy blue-white haze. Chosen from the place's star (<see cref="Graveyards.Of"/>).
		/// </summary>
		bool Warm = false;

		cs::List<GraveHulk> Hulks;
		cs::List<DerelictStation> Stations;

		/// <summary>The drawn wreckage (not simulated): chunks and container-sized pieces, km from the root, and their sizes.</summary>
		cs::List<DVec3> ChunkOffsetsKm;
		cs::List<double> ChunkRadiiKm;
		uint32_t Seed = 0;
	};

	/// <summary>A place a lurker could lie up in: a graveyard's heart, or a derelict station (see <see cref="Graveyards.LurkSites"/>).</summary>
	struct SPACEGODCORE_API LurkSite
	{
		cs::String Name;
		/// <summary>km from the system's root.</summary>
		DVec3 OffsetKm;
		double RadiusKm = 0;
		bool Station = false;
	};

	/// <summary>
	/// Ship graveyards and derelict stations (US-T131). Beyond Sol (kept as it really is) some star systems keep a graveyard
	/// (near a hot star it is backlit in an orange haze, by a cool or far one in an icy blue-white one), some a derelict
	/// station on its own, and some void stations a graveyard among their sites.
	///
	/// A graveyard is two things. Its big hulks and its derelict stations are real wrecks (<see cref="War.Wrecks"/>), salvaged
	/// through the existing latch-and-salvage system (<see cref="SalvageSession"/>): the hulks are ship models broken into
	/// sections (<see cref="ModularHull"/>) with what was left in their holds aboard; a station's wreck carries its stores.
	/// Round them, a dense field of drawn wreckage and a haze catching the light (Views' SpaceFeaturesView).
	///
	/// The plan comes from the place's root name through its own random stream; the wrecks are laid only when a hull the
	/// player flies first enters the place (<see cref="Populate"/>, idempotent), never at galaxy build, so content builds up
	/// ahead of the player and the Bench, which flies no player, runs exactly as before.
	///
	/// Hooks for other systems (none of this edits theirs): <see cref="LurkSites"/> lists where raiders or bio mass could lie
	/// up; <see cref="StationInterior"/> is where a boarding or interior system plugs in a derelict station's insides.
	///
	/// US-T85: Wolf 359 always keeps one, and not a random one: the Muster, the hulks of a picket fleet that met something
	/// here long ago, and at its heart what they met, the Unison's cube (<see cref="DerelictStation.Cube"/>), dark but for a
	/// faint light in its lattice, and still there.
	/// </summary>
	class SPACEGODCORE_API Graveyards final
	{
	public:
		/// <summary>The graveyards of the place whose root body is <paramref name="root"/> (empty for Sol).</summary>
		static const cs::List<Graveyard>& Of(const cs::Ref<BodyDefinition>& root);
		static const cs::List<Graveyard>& Of(const cs::String& rootName);

		/// <summary>
		/// Lays the place's hulks and derelict stations down as wrecks, the first time it is called for the world (later
		/// calls find them laid and do nothing). Called by the game when the player's place changes.
		/// </summary>
		static void Populate(const cs::Ref<World>& world, double jd);

		/// <summary>Where a lurker could lie up in this world: each graveyard's heart and each derelict station.</summary>
		static cs::List<LurkSite> LurkSites(const cs::Ref<World>& world);

		/// <summary>
		/// The hook a boarding or interior system sets to give a derelict station's wreck its insides (compartments, whatever
		/// is still aboard, whatever has moved in). Null until one does; a station is then salvaged from outside only.
		/// </summary>
		static cs::Func<cs::Ref<Interior>, cs::Ref<Wreck>, DerelictStation>& StationInterior();

		/// <summary>Is <paramref name="wreck"/> a derelict station (drawn as a station, not a ship)?</summary>
		static const DerelictStation* StationOf(const cs::Ref<World>& world, const cs::Ref<Wreck>& wreck);

		/// <summary>How long a graveyard's wrecks last, days: they have lain there for ages and will go on lying there.</summary>
		static constexpr double LifetimeDays = 1e7;
	};
}
