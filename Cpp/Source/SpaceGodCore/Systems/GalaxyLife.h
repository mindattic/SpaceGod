#pragma once
// US-T100: the galaxy goes on living with nobody at the controls. Colonisation (U.SPC-T98/T293) and the territory review
// (US-T190) already send colony ships out while there is crowding or money; this is what keeps the settled worlds
// themselves changing over the years after that:
//  - each settlement's owner builds new works in its world's sky as its people grow: a satellite network once it is a
//    colony (a second, wider shell once it passes a million), and stations in orbit (a depot, a relay, a habitat, a
//    gateway), one for each order of magnitude of its people past ten thousand;
//  - a big settlement on a planet with a free moon sends a party out to found a base on it;
//  - a colony far from its owner's seat of government (light-years away, in another system) grows apart from it, the
//    faster the more restless it is, and in the end secedes: its owner's holdings in that system become a new state.
//
// Everything is galaxy state changed only by the simulation, with no random draws: the review runs on a fixed grid of
// whole days, settlements are walked in system and settlement order, and choices tie-break by body index, so every
// lockstep peer and every replay reaches the same galaxy. A settlement's first works come a little over a year after the
// review first sees it, so a new game opens as built and starts changing in its second year.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Galaxy;
	class Settlement;
	class StarSystem;
	class World;

	/// <summary>What the review keeps on one settlement (US-T100): when its owner next builds there, and how far it has grown
	/// apart from a distant seat of government.</summary>
	class SPACEGODCORE_API SettlementLife final
	{
	public:
		cs::Ref<SpaceGod::Core::Settlement> Settlement;

		/// <summary>When its owner next builds something there (a station, satellites or a moon base).</summary>
		double NextWorksJd = cs::Double::NaN;

		/// <summary>How far it has grown apart from its owner (0 loyal; at 1 it secedes, see <see cref="GalaxyLife.Secede"/>).</summary>
		double Yearning = 0;
	};

	/// <summary>A colony that seceded (US-T100): which, from whom, as what, and when.</summary>
	class SPACEGODCORE_API Secession final
	{
	public:
		cs::Ref<StarSystem> System;
		cs::Ref<SpaceGod::Core::Settlement> Settlement;
		cs::Ref<Faction> From, To;
		double Jd = 0;
	};

	/// <summary>
	/// The living galaxy's monthly review (US-T100, see the file comment), run from <see cref="Galaxy.Update"/> after the
	/// territory review. Held on <see cref="Territory.Life"/> (made on first use: older saves have none).
	/// </summary>
	class SPACEGODCORE_API GalaxyLife final
	{
	public:
		/// <summary>Days between reviews.</summary>
		static constexpr double ReviewDays = 30;

		/// <summary>A settlement's first works come this long after the review first sees it, plus a month for every
		/// <see cref="Settlement.Id"/> modulo twelve (so a system's settlements do not all build in the same month).</summary>
		static constexpr double FirstWorksDays = 400;

		/// <summary>Days between one settlement's works.</summary>
		static constexpr double WorksEveryDays = 365.25;

		/// <summary>People a settlement needs for its owner to put up a satellite network over it, and for its second shell.</summary>
		static constexpr double SatellitesAt = 25000;
		static constexpr double SecondShellAt = 1e6;

		/// <summary>A station in orbit for each order of magnitude past this many people (one at 10,000, two at 100,000...), up to <see cref="MaxStations"/>.</summary>
		static constexpr double StationsFrom = 1e4;
		static constexpr int MaxStations = 4;

		/// <summary>People a settlement on a planet needs before it founds a base on one of its moons, and the party it sends (at most this share of its people).</summary>
		static constexpr double MoonBaseAt = 2e5;
		static constexpr double MoonBaseParty = 2500;
		static constexpr double MoonBaseShare = 0.02;

		/// <summary>What each work costs its owner, credits (assumed); an owner short of twice it waits for the next review.</summary>
		static constexpr double SatelliteCost = 2.5e7;
		static constexpr double StationCost = 6e7;
		static constexpr double MoonBaseCost = 8e7;

		/// <summary>A colony at least this far from its owner's capital system (light-years) grows apart from it.</summary>
		static constexpr double SecedeLy = 4;

		/// <summary>Years a colony <see cref="SecedeLy"/> away with no unrest takes to secede (nearer the farther it is, and the more restless).</summary>
		static constexpr double SecedeYears = 30;

		/// <summary>A settlement needs this many people to stand as a state of its own.</summary>
		static constexpr double SecedePopulation = 25000;

		/// <summary>The secessions so far, oldest first.</summary>
		cs::List<cs::Ref<Secession>> Secessions;

		/// <summary>Works built so far, by kind (for the log and the tests): stations, satellite networks (and shells added), moon bases.</summary>
		int StationsBuilt = 0, SatelliteNetworks = 0, MoonBases = 0;

		/// <summary>The galaxy's life (made on first use).</summary>
		static cs::Ref<GalaxyLife> Of(Galaxy& galaxy);

		/// <summary>The review's record of <paramref name="s"/>, or null.</summary>
		cs::Ref<SettlementLife> LifeOf(const cs::Ref<Settlement>& s) const;

		/// <summary>Runs the review when its day has come.</summary>
		void Update(Galaxy& galaxy, double jd);

		/// <summary>
		/// <paramref name="s"/> secedes from its owner: every standing settlement its owner holds in that system becomes a new
		/// state's, with its share of the treasury and spacers and the civilian hulls homed there; the old owner resents it.
		/// Returns the new state, or null if it cannot (no owner, or the owner holds nothing elsewhere).
		/// </summary>
		cs::Ref<Faction> Secede(Galaxy& galaxy, const cs::Ref<StarSystem>& system, const cs::Ref<Settlement>& s, double jd);

	private:
		cs::List<cs::Ref<SettlementLife>> lives;
		double nextReviewJd = cs::Double::NaN;

		void Review(Galaxy& galaxy, double jd, bool first);
		void Works(const cs::Ref<World>& w, const cs::Ref<SettlementLife>& life, double jd);
		bool Satellites(const cs::Ref<World>& w, const cs::Ref<Settlement>& s);
		bool Station(const cs::Ref<World>& w, const cs::Ref<Settlement>& s);
		bool MoonBase(const cs::Ref<World>& w, const cs::Ref<Settlement>& s, double jd);
		void Drift(Galaxy& galaxy, double jd);

		SG_SERIALIZABLE(GalaxyLife); // ObjectGraph registration (GalaxyLife.cpp)
	};
}
