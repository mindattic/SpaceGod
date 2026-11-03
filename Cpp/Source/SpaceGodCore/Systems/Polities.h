#pragma once
// Port of Assets/UnitySpace/Core/Systems/Polities.cs (US-T17).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class World;
	class Settlement;
	class Ship;
	class Diplomacy;
	class StrategicCommand;
	class Statecraft;

	/// <summary>What sort of power a faction is (game layer), for the ownership the HUD shows.</summary>
	enum class PolityKind : int
	{
		/// <summary>A government with a navy: the Sol Federation, the Centauri Accord, a frontier colony's own state.</summary>
		State,

		/// <summary>A mining or industrial concern that owns stations and outposts but keeps no warships of its own.</summary>
		Company,

		/// <summary>Independent shipping: owns hulls, not settlements.</summary>
		FreeTraders,

		/// <summary>Raiders working from their havens, hostile to everyone.</summary>
		PirateClan,
	};
	SPACEGODCORE_API const char* ToString(PolityKind v);

	/// <summary>
	/// The interstellar polities (U.SPC-T91): one faction model shared by every star system. A faction is one
	/// object however many systems it holds settlements in, so it has one treasury, one navy (its warships
	/// wherever they are, in a system's fleet or crossing a gate), one capital and one reserve of spacers; and
	/// there is one diplomacy table for everyone, so a war declared in one system is a war in all of them.
	/// Each system's <see cref="World"/> registers here as it is built and founds (or joins) the polities it
	/// holds through <see cref="World.Polity"/>: the Sol Federation holds Sol and the Barnard's Star frontier,
	/// the Centauri Accord governs Proxima, each frontier colony has its own state, and the pirate clans keep
	/// their own havens. A world built on its own (a test, a single system) gets a private set.
	/// </summary>
	class SPACEGODCORE_API Polities final
	{
		cs::List<cs::Ref<Faction>> factions;
		cs::List<cs::Ref<World>> worlds;
		cs::Dictionary<cs::Ref<Faction>, cs::Ref<World>> homes;

	public:
		/// <summary>Who is at war with whom, for every system at once.</summary>
		cs::Ref<SpaceGod::Core::Diplomacy> Diplomacy;

		/// <summary>The strategy layer (U.SPC-T423): each polity's production, build and research priorities and its fleet groups' objectives.</summary>
		cs::Ref<SpaceGod::Core::StrategicCommand> Command;

		/// <summary>The diplomacy game (U.SPC-T427): opinions, pacts, proposals, declarations of war and peace between the states.</summary>
		cs::Ref<SpaceGod::Core::Statecraft> Statecraft;

		Polities();

		const cs::List<cs::Ref<Faction>>& Factions() const { return factions; }

		/// <summary>The systems' worlds, in the order they were built.</summary>
		const cs::List<cs::Ref<World>>& Worlds() const { return worlds; }

		/// <summary>Ships between systems, inside a gate (they are in no system's fleet until they come out); none if unset.</summary>
		cs::Func<cs::List<cs::Ref<Ship>>> InTransit;

		// internal
		void Register(const cs::Ref<World>& world);

		/// <summary>The faction called <paramref name="name"/>, or null.</summary>
		cs::Ref<Faction> Find(const cs::String& name) const;

		/// <summary>Founds a polity, its home being <paramref name="home"/> (where its recruiting and bookkeeping run).</summary>
		// internal
		void Found(const cs::Ref<Faction>& f, const cs::Ref<World>& home);

		/// <summary>The world that keeps a polity's books (the one that founded it), or null if it is not registered here.</summary>
		cs::Ref<World> HomeOf(const cs::Ref<Faction>& f) const;

		/// <summary>Every settlement in every system, in system order.</summary>
		cs::List<cs::Ref<Settlement>> Settlements() const;

		/// <summary>The settlements <paramref name="f"/> holds, across every system.</summary>
		cs::List<cs::Ref<Settlement>> SettlementsOf(const cs::Ref<Faction>& f) const;

		/// <summary>The world a settlement is in, or null.</summary>
		cs::Ref<World> WorldOf(const cs::Ref<Settlement>& s) const;

		/// <summary>
		/// Who holds a system: the owner of its most populous standing settlement (pirate havens never count).
		/// Null for a system nobody has settled.
		/// </summary>
		static cs::Ref<Faction> HolderOf(const cs::Ref<World>& w);

		/// <summary>The systems <paramref name="f"/> holds (see <see cref="HolderOf"/>).</summary>
		cs::List<cs::Ref<World>> SystemsHeldBy(const cs::Ref<Faction>& f) const;

		/// <summary>The systems <paramref name="f"/> has any standing settlement in, held or not.</summary>
		cs::List<cs::Ref<World>> SystemsWithHoldings(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// The seat of <paramref name="f"/>'s government: the settlement it was founded on, while it holds it;
		/// once that is lost (taken, or fallen) the most populous of its remaining holdings anywhere. Null for a
		/// polity that owns no settlement (free traders).
		/// </summary>
		cs::Ref<Settlement> CapitalOf(const cs::Ref<Faction>& f) const;

		/// <summary>The world <paramref name="f"/>'s capital stands in, or null.</summary>
		cs::Ref<World> CapitalSystemOf(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// <paramref name="f"/>'s one navy: every warship flying its colours, in whichever system's fleet it is or
		/// inside a gate between two of them.
		/// </summary>
		cs::List<cs::Ref<Ship>> Navy(const cs::Ref<Faction>& f) const;

		/// <summary>How many of <paramref name="f"/>'s supply convoys (freighters, so not in <see cref="Navy"/>) sail in any system or gate.</summary>
		int ConvoyCount(const cs::Ref<Faction>& f) const;

		/// <summary>Whether <paramref name="f"/> is at war with anyone anywhere (pirates aside: everyone is always at odds with them).</summary>
		bool AtWar(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// The polities whose books <paramref name="world"/> keeps (it founded them), out of those present in it:
		/// each recruits and recovers once a day, wherever it holds people, not once in every system it is in.
		/// A faction from no registry (made directly on a fleet) is kept by whichever world it is in.
		/// </summary>
		cs::List<cs::Ref<Faction>> KeptBy(const cs::Ref<World>& world, const cs::List<cs::Ref<Faction>>& present) const;
		SG_SERIALIZABLE(Polities); // ObjectGraph registration (Persistence/Registrations)
	};
}
