#pragma once
// Not a C# file (US-T146): gas giant harvesting. Every gas giant has a harvesting operation: skimmer platforms in low
// orbit dipping into its atmosphere, and tankers shuttling the gas out to the markets; protection contracts to guard a
// platform for some orbits of its planet; and raids on both that cut their owner's fuel supply.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Economy/Goods.h"     // GoodInfo::Count (the per-good arrays' length)
#include "Economy/Contracts.h" // ContractState
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Faction;
	class Settlement;
	class Ship;
	class OrbitalStructure;

	/// <summary>Who comes for a gas works (US-T146): a protection contract's waves, and the raids it suffers on its own.</summary>
	enum class HarvestThreat : int
	{
		/// <summary>A raider band after the tanks.</summary>
		Pirates,
		/// <summary>A globule of the Bio Mass (US-T125) drifting in to feed on the platform.</summary>
		BioMass,
		/// <summary>Ghost ships out of the dark (US-T124): only at the outermost giant of a system beyond Sol.</summary>
		VoidRaiders,
		/// <summary>A power at war with the works' owner, cutting its fuel supply.</summary>
		Rival,
	};
	SPACEGODCORE_API const char* ToString(HarvestThreat v);

	/// <summary>
	/// One skimmer platform (US-T146): a tank farm in low orbit about its giant, whose skimmers dive into the cloud tops and
	/// climb back with what they scooped. Its place is an <see cref="OrbitalStructure"/> of its own (not in the world's
	/// infrastructure), so the teleports and the views place it as they do any station.
	/// </summary>
	class SPACEGODCORE_API HarvestPlatform final
	{
	public:
		cs::String Name;
		int Seed = 0;
		cs::Ref<OrbitalStructure> Structure;

		/// <summary>Its state of repair, 0..1: a raid knocks it down, the crews mend it a little each day.</summary>
		double Condition = 1;

		/// <summary>Knocked out of work until this date by a raid (NaN: working).</summary>
		double DownUntilJd = cs::Double::NaN;

		/// <summary>Gas aboard waiting for a tanker, tonnes of each good.</summary>
		cs::List<double> Tank = cs::NewArray<double>(GoodInfo::Count);

		int Raids = 0;

		bool Down(double jd) const { return !cs::Double::IsNaN(DownUntilJd) && jd < DownUntilJd; }
		double TankTonnes() const;

		HarvestPlatform() = default;
		SG_SERIALIZABLE(HarvestPlatform);
	};

	/// <summary>Where a tanker is in its round trip (US-T146).</summary>
	enum class TankerLeg : int
	{
		/// <summary>Lying at its giant waiting for a full tank to take on.</summary>
		Waiting,
		/// <summary>Under way with a load for <see cref="HarvestTanker.To"/>.</summary>
		Outbound,
		/// <summary>Coming home empty.</summary>
		Returning,
	};
	SPACEGODCORE_API const char* ToString(TankerLeg v);

	/// <summary>One gas tanker (US-T146): it loads at a platform and carries the gas to the port that wants it most.</summary>
	class SPACEGODCORE_API HarvestTanker final
	{
	public:
		cs::String Name;
		int Seed = 0;
		TankerLeg Leg = TankerLeg::Waiting;

		/// <summary>The platform it loaded at (its index in the works), and the port its load is for.</summary>
		int Platform = 0;
		cs::Ref<Settlement> To;

		double DepartJd = 0, ArriveJd = 0;
		cs::List<double> Cargo = cs::NewArray<double>(GoodInfo::Count);
		int Trips = 0;

		/// <summary>Taken by raiders: the works replaces it on <see cref="ReplacedJd"/>.</summary>
		bool Lost = false;
		double ReplacedJd = cs::Double::NaN;

		double CargoTonnes() const;

		/// <summary>How far along its leg it is, 0..1.</summary>
		double Progress(double jd) const;

		HarvestTanker() = default;
		SG_SERIALIZABLE(HarvestTanker);
	};

	/// <summary>One gas giant's works (US-T146): its platforms, its tankers, who owns it and what it has done.</summary>
	class SPACEGODCORE_API HarvestOperation final
	{
	public:
		cs::String Name;
		/// <summary>The giant (its catalog index).</summary>
		int Body = 0;
		cs::Ref<Faction> Owner;
		/// <summary>Its home port: the biggest settlement among the giant's moons, else the nearest; null in an unsettled system.</summary>
		cs::Ref<Settlement> Port;

		cs::List<cs::Ref<HarvestPlatform>> Platforms = cs::List<cs::Ref<HarvestPlatform>>();
		cs::List<cs::Ref<HarvestTanker>> Tankers = cs::List<cs::Ref<HarvestTanker>>();

		/// <summary>Tonnes a working platform scoops a day, of each good.</summary>
		cs::List<double> Rate = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Whether it is the outermost giant of a system beyond Sol (the Void Raiders reach it).</summary>
		bool Outermost = false;

		/// <summary>Running totals, tonnes of each good: scooped, delivered to markets, taken by raiders.</summary>
		cs::List<double> Harvested = cs::NewArray<double>(GoodInfo::Count);
		cs::List<double> Delivered = cs::NewArray<double>(GoodInfo::Count);
		cs::List<double> Plundered = cs::NewArray<double>(GoodInfo::Count);

		int Raids = 0, TankersLost = 0, Deliveries = 0;

		/// <summary>Helium-3 (its drives' fuel) its owner has lost to raids, tonnes, and when the last raid was.</summary>
		double FuelLost = 0;
		double LastRaidJd = cs::Double::NaN;
		HarvestThreat LastThreat = HarvestThreat::Pirates;

		/// <summary>The days one of its platforms takes to go once round the giant.</summary>
		double OrbitDays(int platform) const;

		/// <summary>Platforms at work now.</summary>
		int Working(double jd) const;

		cs::String ToString() const { return Name; }

		HarvestOperation() = default;
		SG_SERIALIZABLE(HarvestOperation);
	};

	/// <summary>One wave of attackers on a guarded platform (US-T146): due on a date, spawned by the game beside the guard.</summary>
	class SPACEGODCORE_API HarvestWave final
	{
	public:
		double Jd = 0;
		HarvestThreat Kind = HarvestThreat::Pirates;
		/// <summary>The faction it flies for (pirates, ghosts, a hostile navy); null for the Bio Mass.</summary>
		cs::Ref<Faction> By;
		/// <summary>How hard it hits: 1 a few small craft, 2 a light warship group, 3 a heavy one.</summary>
		int Size = 1;

		bool Spawned = false, Resolved = false, Beaten = false;
		double SpawnedJd = cs::Double::NaN;

		/// <summary>The hulls the game spawned for it, or the Bio Mass globule (its id, -1 none).</summary>
		cs::List<cs::Ref<Ship>> Ships = cs::List<cs::Ref<Ship>>();
		int Globule = -1;

		HarvestWave() = default;
		SG_SERIALIZABLE(HarvestWave);
	};

	/// <summary>
	/// A protection contract (US-T146): guard one platform of a works for <see cref="Orbits"/> of its orbits round the
	/// giant. Taken by an embodied unit: the pay goes to that hull's purse and its pilot, the standing to its captain's
	/// own record and its faction, never to anyone above it. Waves of attackers come during it; a guard away from the
	/// giant when one comes leaves the platform to it, and the contract fails.
	/// </summary>
	class SPACEGODCORE_API HarvestContract final
	{
	public:
		int Id = 0;
		ContractState State = ContractState::Open;
		cs::Ref<HarvestOperation> Operation;
		int Platform = 0;
		/// <summary>Who pays: the works' owner.</summary>
		cs::Ref<Faction> Patron;

		int Orbits = 3;
		double OrbitDays = 0.1;
		double Pay = 0;

		double PostedJd = 0, ExpiresJd = 0;
		double StartJd = cs::Double::NaN, EndJd = cs::Double::NaN, ClosedJd = cs::Double::NaN;

		/// <summary>The hull that took it, the colours it flew then, and its pilot's name then.</summary>
		cs::Ref<Ship> Taker;
		cs::Ref<Faction> TakerFaction;
		cs::String PilotName;

		cs::List<cs::Ref<HarvestWave>> Waves = cs::List<cs::Ref<HarvestWave>>();

		/// <summary>The guarded platform's hull while the contract runs, 0..1: a live wave wears it down.</summary>
		double PlatformHp = 1;

		/// <summary>The guard as last seen: when, and how far from the giant (km).</summary>
		double GuardSeenJd = cs::Double::NaN;
		double GuardKmFromGiant = cs::Double::PositiveInfinity;

		cs::String Outcome;

		/// <summary>Whole orbits done by <paramref name="jd"/>.</summary>
		int OrbitsDone(double jd) const;

		/// <summary>The live wave (spawned, not resolved), or null; and the next one still to come, or null.</summary>
		cs::Ref<HarvestWave> LiveWave() const;
		cs::Ref<HarvestWave> NextWave() const;

		cs::String Title() const;

		HarvestContract() = default;
		SG_SERIALIZABLE(HarvestContract);
	};

	/// <summary>
	/// Gas giant harvesting in one star system (US-T146). Every gas giant of the system (in Sol Jupiter, Saturn, Uranus and
	/// Neptune; beyond it every giant a generated system has) has a works, planned once from the system's own random
	/// stream (seeded from the root's name, never one of the subsystems' seed offsets):
	///  - 3 to 6 skimmer platforms in low orbit (1.06 to 1.3 radii), their skimmers dipping into the cloud tops, each
	///    scooping a day's helium-3 (the drives' fusion fuel), hydrocarbons and nitrogen (ammonia and methane, the
	///    volatiles); a Jupiter-like giant gives more helium-3, an ice giant more volatiles;
	///  - a tanker for each platform, which takes on a full tank and carries it to the port that wants it most (the
	///    owner's first, its own moons' first), the trip timed as a merchant's flip and burn: the gas goes into that
	///    market's stock, and its owner is paid for it. It is a major input: the helium-3 is what every port sells as
	///    drive fuel (Propulsion) and what navies' supply ships draw, the volatiles are trade goods.
	/// Raiders prey on it (in proportion to the raiders about, the danger at the giant and the Bio Mass near it): a platform
	/// raided loses its tank and is knocked out for days, a tanker taken loses its load and the hull; either cuts the owner's
	/// fuel supply (<see cref="HarvestOperation.FuelLost"/>), logged as a loss. Protection contracts
	/// (<see cref="HarvestContract"/>) are posted at each works and cut the raids while a guard is on station.
	/// It runs in whole days (the harvest, the tankers, the raids, the postings) plus, for a contract under way, every
	/// update (its waves and the platform's hull).
	/// </summary>
	class SPACEGODCORE_API GasHarvest final
	{
	public:
		cs::List<cs::Ref<HarvestOperation>> Operations = cs::List<cs::Ref<HarvestOperation>>();

		/// <summary>Every protection contract posted and not yet forgotten (open, taken, the latest closed).</summary>
		cs::List<cs::Ref<HarvestContract>> Contracts = cs::List<cs::Ref<HarvestContract>>();

		/// <summary>Goodwill a faction has earned with a works' owner by guarding its platforms ("patron|guard" faction names).</summary>
		cs::Dictionary<cs::String, double> Goodwill = cs::Dictionary<cs::String, double>();

		// ---------------------------------------------------------------- tuning

		/// <summary>The most gas a platform's tanks hold, and a tanker's load, tonnes.</summary>
		static constexpr double TankCapacity = 9000;
		static constexpr double TankerLoad = 4000;

		/// <summary>Chances a day, at full pressure, that raiders hit a platform, and take a tanker under way.</summary>
		static constexpr double PlatformRaidPerDay = 0.006;
		static constexpr double TankerRaidPerDay = 0.008;

		/// <summary>Share of the raids a guard on station prevents, and a port's own defence platforms.</summary>
		static constexpr double GuardCut = 0.85;
		static constexpr double DefendedCut = 0.35;

		/// <summary>Days a raided platform is down (from, to), and what each raid costs its repair.</summary>
		static constexpr double DownDaysMin = 2;
		static constexpr double DownDaysMax = 7;
		static constexpr double RaidWear = 0.35;
		/// <summary>Days to build a tanker to replace one taken.</summary>
		static constexpr double TankerRebuildDays = 18;

		/// <summary>Share of a delivery's market value paid to the works' owner.</summary>
		static constexpr double OwnerShare = 0.5;

		/// <summary>Protection contracts: how long an offer stays up (days), the most open at once per works, the pay
		/// for each orbit guarded (credits, before danger and goodwill), and standing a done or failed one moves.</summary>
		static constexpr double OfferDays = 6;
		static constexpr int OpenPerWorks = 2;
		static constexpr double PayPerOrbit = 45000;
		static constexpr double CompleteStanding = 3;
		static constexpr double FailStanding = 5;

		/// <summary>A live wave's wear on the guarded platform: the share a second at its full size (a heavy wave on an
		/// unguarded platform guts it in about 4 minutes).</summary>
		static constexpr double WaveWearPerS = 1.0 / 240;

		/// <summary>A wave the game has not spawned within this (seconds) after it was due hits the platform on its own.</summary>
		static constexpr double WaveGraceS = 90;

		/// <summary>A guard counts as on station within this of the giant (km; at least its neighbourhood).</summary>
		double GuardKm(int body) const;

		GasHarvest() = default;
		GasHarvest(const cs::Ref<World>& world, int seed, double jd);

		/// <summary>Runs the works to <paramref name="jd"/>: whole days of harvest, tankers, raids and postings, and the
		/// protection contracts under way.</summary>
		void Update(double jd);

		// ---------------------------------------------------------------- queries

		/// <summary>The works at <paramref name="body"/>, or null.</summary>
		cs::Ref<HarvestOperation> At(int body) const;

		/// <summary>Where a platform is (heliocentric km).</summary>
		DVec3 PlatformHelio(const cs::Ref<HarvestOperation>& op, int platform, double jd) const;

		/// <summary>Where a tanker is (heliocentric km): from its giant to its port and back.</summary>
		DVec3 TankerHelio(const cs::Ref<HarvestOperation>& op, const cs::Ref<HarvestTanker>& t, double jd) const;

		/// <summary>The platform nearest a point, its works and its distance (km); false when there is none.</summary>
		bool Nearest(DVec3 helioKm, double jd, cs::Ref<HarvestOperation>& op, int& platform, double& km) const;

		/// <summary>An open offer to guard <paramref name="platform"/> of <paramref name="op"/> (-1: any of its platforms), or null.</summary>
		cs::Ref<HarvestContract> OpenAt(const cs::Ref<HarvestOperation>& op, int platform = -1) const;

		/// <summary>The contract <paramref name="ship"/> has taken and not finished, or null.</summary>
		cs::Ref<HarvestContract> ActiveFor(const cs::Ref<Ship>& ship) const;

		double GoodwillOf(const cs::Ref<Faction>& patron, const cs::Ref<Faction>& guard) const;

		// ---------------------------------------------------------------- protection contracts

		/// <summary>Posts an offer to guard <paramref name="platform"/> of <paramref name="op"/> now (the dev menu's, and
		/// the daily postings').</summary>
		cs::Ref<HarvestContract> Offer(const cs::Ref<HarvestOperation>& op, int platform, double jd);

		/// <summary>
		/// <paramref name="ship"/> (the unit embodied, at <paramref name="helioKm"/>) takes <paramref name="c"/>: it must be
		/// armed, at the giant, and the owner must not be at war with it. The orbits start now and the waves are drawn.
		/// <paramref name="firstWaveS"/>, if positive, brings the first wave that many seconds from now (the dev menu).
		/// Empty <paramref name="why"/> on success.
		/// </summary>
		bool Take(const cs::Ref<HarvestContract>& c, const cs::Ref<Ship>& ship, DVec3 helioKm, double jd, cs::String& why, double firstWaveS = 0);

		/// <summary>The taker gives it up: no pay, standing and goodwill fall.</summary>
		void Abandon(const cs::Ref<HarvestContract>& c, double jd);

		/// <summary>Where the guard is (the game, each frame, for the embodied unit): on station or not.</summary>
		void Guard(const cs::Ref<Ship>& ship, DVec3 helioKm, double jd);

		/// <summary>The game spawned <paramref name="w"/>'s attackers: these hulls (or a Bio Mass globule's id).</summary>
		void Spawned(const cs::Ref<HarvestWave>& w, const cs::List<cs::Ref<Ship>>& ships, int globule, double jd);

		/// <summary>A wave the game should spawn now beside the guard of <paramref name="c"/> (due, not spawned), or null.</summary>
		cs::Ref<HarvestWave> DueWave(const cs::Ref<HarvestContract>& c, double jd) const;

		// ---------------------------------------------------------------- raiding a platform

		/// <summary>
		/// <paramref name="ship"/> raids a platform's tanks: as much gas as its hold takes comes aboard as stolen crates
		/// (helium-3 first), the platform is knocked out, and it is piracy on its pilot's own record, seen by the platform's
		/// crew. Returns the tonnes taken (0 if there was nothing to take or nowhere to put it).
		/// </summary>
		double Raid(const cs::Ref<HarvestOperation>& op, int platform, const cs::Ref<Ship>& ship, double jd, cs::String& said);

	private:
		cs::Ref<World> world;
		cs::Random rng;
		double nextDayJd = cs::Double::NaN;
		double lastJd = cs::Double::NaN;
		int nextContractId = 1, tankerSerial = 0;

		void Plan(double jd);
		void Day(double jd);
		void Rehome(const cs::Ref<HarvestOperation>& op, double jd);
		void Harvest(const cs::Ref<HarvestOperation>& op, double jd);
		void SailTankers(const cs::Ref<HarvestOperation>& op, double jd);
		void MoveTankers(const cs::Ref<HarvestOperation>& op, double jd, double pressure);
		void RaidPlatforms(const cs::Ref<HarvestOperation>& op, double jd, double pressure);
		void PostContracts(double jd);
		void RunContracts(double jd, double dt);
		void Close(const cs::Ref<HarvestContract>& c, ContractState state, double jd, const cs::String& why);
		void Trim();

		/// <summary>The port with most room for a load from <paramref name="op"/>, by its value over the days to get there.</summary>
		cs::Ref<Settlement> Destination(const cs::Ref<HarvestOperation>& op, const cs::List<double>& load, double jd) const;
		double TransitDays(int from, int to, double jd) const;

		/// <summary>How many raiders are about, 0..1 (as the belt works reckon it), and the danger at a giant.</summary>
		double PiratePressure() const;
		double Pressure(const cs::Ref<HarvestOperation>& op, double basePressure) const;

		/// <summary>Who would come for a works now (weighted draw: pirates, the Bio Mass near it, ghosts at the outermost
		/// giant beyond Sol, a power at war with its owner), and the faction they fly for.</summary>
		HarvestThreat DrawThreat(const cs::Ref<HarvestOperation>& op, cs::Ref<Faction>& by);
		cs::Ref<Faction> RivalOf(const cs::Ref<Faction>& owner, const cs::Ref<Faction>& guard) const;
		bool BioMassNear(int body, double jd) const;

		void Knock(const cs::Ref<HarvestOperation>& op, int platform, HarvestThreat by, const cs::Ref<Faction>& raiders, double jd, const cs::String& how);
		void Log(double jd, const cs::String& text, int body, bool loss);

		SG_SERIALIZABLE(GasHarvest);
	};
}
