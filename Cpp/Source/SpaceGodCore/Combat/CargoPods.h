#pragma once
// Not a C# file (US-T95): cargo pods and the tractor beam. A destroyed hull spills what it carried as drifting pods; a
// tractor beam pulls one in and the crates go into the hold, through the same crate loader as latch-and-salvage.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Wreck.h" // SalvageStack
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class ShipClass : int;
	class CargoHold;
	class Faction;
	class LaneSite;
	class Ship;
	class World;
	struct CargoDrop;

	/// <summary>
	/// One drifting cargo pod: a canister of one good, spilled from a hull that died (<see cref="CargoPods.Spill"/>) or
	/// jettisoned to a threat (<see cref="CargoPods.Jettison"/>). Placed like a <see cref="Wreck"/>: <see cref="Anchor"/>
	/// in <see cref="Body"/>'s frame (or beside a lane, <see cref="Site"/>), and <see cref="LocalAt"/> its battle-local
	/// position, drifting out from where it was spilled and slowing as it goes; <see cref="TowKm"/> is how far a tractor
	/// beam has hauled it since.
	/// </summary>
	class SPACEGODCORE_API CargoPod final
	{
	public:
		int Id = 0;

		/// <summary>The hull it came from ("SFS Valiant").</summary>
		cs::String From;

		/// <summary>Whose cargo it was.</summary>
		cs::Ref<Core::Faction> Owners;

		Core::Good What{};
		double Tonnes = 0;

		/// <summary>Dropped under threat: stolen goods once aboard (contraband to a lawful patrol).</summary>
		bool Stolen = false;

		int Body = 0;
		DVec3 Anchor, Local;
		cs::Ref<LaneSite> Site;

		/// <summary>The way it drifts from where it was spilled (unit), and how far it goes at most, km.</summary>
		DVec3 DriftDir = DVec3::UnitX;
		double SpreadKm = 0;

		/// <summary>How fast it leaves the hull at first, km/s (the drift tails off from there).</summary>
		double DriftKmS = 0;

		/// <summary>How far a tractor beam has hauled it so far, km (battle-local axes).</summary>
		DVec3 TowKm;

		double SpilledJd = 0, LifetimeDays = 0;

		/// <summary>The wreck it spilled from, or -1 (a jettison).</summary>
		int WreckId = -1;

		bool Expired(double jd) const { return jd - SpilledJd > LifetimeDays || Tonnes <= 1e-6; }

		/// <summary>How far it has drifted from where it was spilled by <paramref name="jd"/>, km.</summary>
		double DriftKm(double jd) const;

		/// <summary>Its battle-local position at <paramref name="jd"/>, km from the anchor, without the tow.</summary>
		DVec3 DriftedAt(double jd) const;

		/// <summary>Its battle-local position at <paramref name="jd"/>, km from the anchor, tow included.</summary>
		DVec3 LocalAt(double jd) const { return DriftedAt(jd) + TowKm; }

		/// <summary>Where it is now, heliocentric km.</summary>
		DVec3 Helio(const cs::Ref<World>& world, double jd) const;

		CargoPod() = default;
		SG_SERIALIZABLE(CargoPod);
	};

	/// <summary>
	/// Every drifting cargo pod in the system (US-T95), held on <see cref="Wrecks.Pods"/>. Pods are made when a hull dies with
	/// cargo aboard (its economy cargo, <see cref="Ship.CargoGood"/>/<see cref="Ship.CargoTonnes"/>, as the wreck's
	/// manifest records it): most of it spills, the rest stays in the wreck's hold for latch-and-salvage. Pirates who win
	/// the fight tractor their share of it in (<see cref="Haul"/>, War's loot); the player's tractor beam takes the rest.
	/// Deterministic: positions and drift come from the ids, never from a shared random stream.
	/// </summary>
	class SPACEGODCORE_API CargoPods final
	{
	public:
		/// <summary>The share of a hull's cargo that spills as pods when it dies (the rest stays in the wreck's hold).</summary>
		static constexpr double SpillShare = 0.75;

		/// <summary>The most pods one stack spills as, however much there was.</summary>
		static constexpr int MaxPodsPerStack = 6;

		/// <summary>How long a jettisoned pod drifts before it is lost to sight for good, days.</summary>
		static constexpr double JettisonLifetimeDays = 6;

		cs::List<cs::Ref<CargoPod>> Items = cs::List<cs::Ref<CargoPod>>();

	private:
		int nextId = 0;

	public:
		/// <summary>Spills <see cref="SpillShare"/> of each stack in <paramref name="wreck"/>'s manifest as pods around it,
		/// taking it off the manifest. Returns the pods made.</summary>
		cs::List<cs::Ref<CargoPod>> Spill(const cs::Ref<Wreck>& wreck, double jd);

		/// <summary>
		/// Cargo dropped under threat (US-T137, <see cref="Comms.Tractor"/>): floated as pods where the victim is, in the
		/// frame of the nearest body. Returns the tonnes put out as pods (all of it).
		/// </summary>
		double Jettison(const cs::Ref<World>& world, const CargoDrop& drop);

		/// <summary>
		/// The winners of a fight haul in up to <paramref name="tonnes"/> of <paramref name="good"/> from what
		/// <paramref name="wreck"/> spilled, pods first, then its hold. Returns what they got.
		/// </summary>
		double Haul(const cs::Ref<Wreck>& wreck, Good good, double tonnes);

		/// <summary>Tonnes of <paramref name="good"/> still drifting in <paramref name="wreck"/>'s pods.</summary>
		double SpilledFrom(const cs::Ref<Wreck>& wreck, Good good) const;

		/// <summary>The nearest drifting pod to <paramref name="helioKm"/> within <paramref name="rangeKm"/>, or null.</summary>
		cs::Ref<CargoPod> Nearest(const cs::Ref<World>& world, DVec3 helioKm, double jd, double rangeKm, double& distanceKm,
			const cs::Ref<CargoPod>& skip = nullptr) const;

		/// <summary>Clears away every pod lost to sight or emptied as of <paramref name="jd"/>.</summary>
		void AdvanceTo(double jd);

	private:
		cs::Ref<CargoPod> Make(Good good, double tonnes, int salt);
		SG_SERIALIZABLE(CargoPods);
	};

	/// <summary>A hull class's tractor beam (US-T95): how far it reaches and how hard it pulls.</summary>
	struct SPACEGODCORE_API TractorSpec
	{
		/// <summary>How far the beam reaches, km (it lets go at <see cref="TractorBeam.SnapRange"/> times this).</summary>
		double RangeKm = 0;

		/// <summary>The pull on a light pod, km/s per second, and the fastest it hauls one in, km/s.</summary>
		double PullKmS2 = 0, MaxSpeedKmS = 0;

		/// <summary>A pod heavier than this many tonnes comes in slower, in proportion (down to a third).</summary>
		double RatedTonnes = 0;

		TractorSpec() = default;
		TractorSpec(double rangeKm, double pullKmS2, double maxSpeedKmS, double ratedTonnes)
			: RangeKm(rangeKm), PullKmS2(pullKmS2), MaxSpeedKmS(maxSpeedKmS), RatedTonnes(ratedTonnes) {}

		/// <summary>By hull class: strike craft reach a few km and tug gently, capital ships farther and harder, and the tugs
		/// (built for it) best of all.</summary>
		static TractorSpec Of(ShipClass c);
	};

	/// <summary>What became of one pod a tractor beam brought alongside.</summary>
	struct SPACEGODCORE_API TractorCatch
	{
		int Crates = 0;
		double Tonnes = 0;

		/// <summary>The hold filled before the pod was empty: what is left drifts on.</summary>
		bool HoldFull = false;
	};

	/// <summary>
	/// The tractor beam's mechanics (US-T95), the same for every hull and at either scale (everything here is in real km;
	/// the views draw it where the scale model puts the hull and the pod).
	/// </summary>
	class SPACEGODCORE_API TractorBeam final
	{
	public:
		/// <summary>The beam lets go when the pod is this many times its reach away (the hull ran off with it).</summary>
		static constexpr double SnapRange = 1.5;

		/// <summary>Within this far of the hull's skin (km, on top of half its length) the pod is taken aboard.</summary>
		static constexpr double CatchClearanceKm = 0.05;

		static double CatchRangeKm(double shipLengthKm) { return CatchClearanceKm + 0.5 * cs::Math::Max(0.0, shipLengthKm); }

		/// <summary>
		/// Pulls <paramref name="pod"/> toward the hull for <paramref name="dt"/> seconds: <paramref name="toShipKm"/> is the
		/// hull's position less the pod's (km), <paramref name="speedKmS"/> the pod's speed along the beam so far (updated).
		/// The pod's <see cref="CargoPod.TowKm"/> moves; never past the hull.
		/// </summary>
		static void Pull(const cs::Ref<CargoPod>& pod, const TractorSpec& spec, DVec3 toShipKm, double dt, double& speedKmS);

		/// <summary>
		/// Takes <paramref name="pod"/> aboard <paramref name="ship"/>: its crates into the ship's own grid hold
		/// (<see cref="SalvageSession.LoadCrates"/>, as latch-and-salvage loads them), the tonnes off the pod, and the haul
		/// on the record of the pilot flying it (<see cref="Characters"/>) -- never anyone else's.
		/// </summary>
		static TractorCatch Catch(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<CargoPod>& pod, double jd);
	};
}
