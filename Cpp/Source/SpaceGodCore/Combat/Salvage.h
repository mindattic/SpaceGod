#pragma once
// Port of Assets/UnitySpace/Core/Combat/Salvage.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Wreck.h" // SalvageStack

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class SubsystemKind : int;
	class Ship;
	class Faction;
	class Interior;
	class Subsystem;
	class Fleet;

	/// <summary>How the piloted ship stands toward a hulk it wants to latch on to.</summary>
	enum class LatchState : int
	{
		/// <summary>Further off than <see cref="SalvageSession.LatchRangeKm"/>.</summary>
		TooFar,

		/// <summary>Close enough, but still moving relative to it faster than <see cref="SalvageSession.MatchSpeedKmS"/>.</summary>
		TooFast,

		/// <summary>Alongside with velocities matched: the docking clamps can take hold.</summary>
		Ready,
	};
	SPACEGODCORE_API const char* ToString(LatchState v);

	/// <summary>What became of a part stripped from a hulk.</summary>
	enum class StripUse : int
	{
		/// <summary>Nothing was stripped (no such part, still fighting aboard, or nothing there).</summary>
		None,

		/// <summary>It replaced a shot-away hardpoint of the same kind on the player's own hull.</summary>
		Fitted,

		/// <summary>Broken down for parts: crates of a good into the grid hold.</summary>
		Scrapped,

		/// <summary>No room in the hold for what it breaks down into: it stays where it is.</summary>
		NoRoom,
	};
	SPACEGODCORE_API const char* ToString(StripUse v);

	/// <summary>The result of <see cref="SalvageSession.Strip"/>.</summary>
	struct SPACEGODCORE_API StripResult
	{
		StripUse Use{};
		cs::Ref<Subsystem> Part;

		/// <summary>The player's hardpoint it was fitted in place of (for <see cref="StripUse.Fitted"/>).</summary>
		cs::Ref<Subsystem> FittedTo;

		/// <summary>What it broke down into (for <see cref="StripUse.Scrapped"/>).</summary>
		SpaceGod::Core::Good Good{};
		int Crates = 0;

		StripResult() = default;
		StripResult(StripUse use, cs::Ref<Subsystem> part, cs::Ref<Subsystem> fittedTo, SpaceGod::Core::Good good, int crates)
		{
			Use = use;
			Part = part;
			FittedTo = fittedTo;
			Good = good;
			Crates = crates;
		}
	};

	/// <summary>
	/// The player's ship latched on to a hulk: a derelict (<see cref="Ship.Derelict"/>, stripped and left
	/// adrift by <see cref="War"/>) or a battle's wreck (<see cref="Wreck"/>). The pilot first matches
	/// velocity alongside it (<see cref="Check"/>), the clamps take hold (<see cref="Latch(Ship, Ship)"/>), and
	/// then, once nobody hostile is left aboard, chooses: salvage what is in its hold into the grid hold
	/// (<see cref="SalvageCargo"/>), strip a component (<see cref="Strip"/>: fitted in place of a shot-away
	/// hardpoint of the same kind, or broken down into crates), or claim the hull if it can still fly
	/// (<see cref="Claim"/>). The inside of a derelict is its own <see cref="Interior"/>: raiders left aboard
	/// to finish stripping it hold it as defenders, and have to be fought through compartment by
	/// compartment by the player's marines (<see cref="SendBoarders"/>, <see cref="Step"/>) before anything
	/// can be taken; crew left behind (<see cref="Compartment.Survivors"/>) are rescued once it is clear.
	/// </summary>
	class SPACEGODCORE_API SalvageSession final
	{
	public:
		/// <summary>Gap the clamps can bridge between the two hulls, km, on top of half of each one's length.</summary>
		static constexpr double LatchClearanceKm = 1.0;

		/// <summary>The fastest the two hulls may still be moving apart or together for the clamps to hold, km/s (20 m/s).</summary>
		static constexpr double MatchSpeedKmS = 0.02;

		/// <summary>The share of a merchant's cargo raiders in a hurry leave behind when they strip it and cast it adrift.</summary>
		static constexpr double LeftAboardShare = 0.2;

		/// <summary>How close the piloted ship's centre must come to a hulk's for the clamps to reach, km.</summary>
		static double LatchRangeKm(double hulkLengthKm, double playerLengthKm);

		/// <summary>Whether a ship <paramref name="relativeKm"/> from a hulk and moving at <paramref name="relativeVelocityKmS"/>
		/// relative to it can latch on, given the clamps' reach <paramref name="rangeKm"/>.</summary>
		static LatchState Check(DVec3 relativeKm, DVec3 relativeVelocityKmS, double rangeKm);

		// ---------------------------------------------------------------- left adrift

		/// <summary>
		/// Fills a hull <paramref name="raiders"/> have just stripped and cast adrift with what a salvager
		/// will find: <paramref name="leftTonnes"/> of <paramref name="good"/> they had no room or time for,
		/// the ship's own stores, and -- by a roll fixed per hull -- crew who hid and survived, raiders still
		/// aboard finishing the job, both or neither.
		/// </summary>
		static void Strand(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& raiders, Good good, double leftTonnes, double jd,
			double aboard = cs::Double::PositiveInfinity);

		// ---------------------------------------------------------------- the session

		cs::Ref<Ship> Player;

		/// <summary>The derelict latched on to, or null for a wreck.</summary>
		cs::Ref<Ship> Derelict;

		/// <summary>The wreck latched on to, or null for a derelict.</summary>
		cs::Ref<SpaceGod::Core::Wreck> Wreck;

		/// <summary>The inside of the hulk: a derelict's own, or the gutted, open-to-space shell of a wreck.</summary>
		cs::Ref<SpaceGod::Core::Interior> Interior;

		/// <summary>What is still aboard to be salvaged, crate by crate.</summary>
		cs::List<SalvageStack>& Manifest() const;

		cs::String Name() const;

		/// <summary>Whose the hulk was when the clamps took hold: the people any survivors belong to.</summary>
		cs::Ref<Faction> Owners;

		/// <summary>The raiders still aboard, if any are (the ones who stripped it).</summary>
		cs::Ref<Faction> Holders() const;

		/// <summary>Raiders aboard who will fight: nothing can be taken until they are dealt with.</summary>
		bool Hostile() const;

		/// <summary>The player's marines are fighting aboard right now.</summary>
		bool Fighting() const;

		/// <summary>Seconds since the clamps took hold (the clock the fight aboard runs on).</summary>
		double Seconds = 0;

		int CratesTaken = 0;
		int PartsStripped = 0;
		double SurvivorsRescued = 0;
		bool HostilesCleared = false;
		bool Claimed = false;

		/// <summary>The clamps let go (<see cref="CastOff"/>, or the hull was claimed).</summary>
		bool Ended = false;

		/// <summary>Anything came of it: something taken, someone saved, the raiders aboard beaten or the hull claimed.</summary>
		bool Worked() const { return CratesTaken > 0 || PartsStripped > 0 || SurvivorsRescued > 0 || HostilesCleared || Claimed; }

		/// <summary>What happened, in order, for the HUD.</summary>
		cs::List<cs::String> Log;

	private:
		/// <summary>Rescued survivors waiting in the airlock: they crew the hull if it is claimed, else go aboard the player's ship.</summary>
		double survivorsHeld = 0;
		cs::Random rng;

	public:
		// Private in C#; public here so cs::New (make_shared) can reach it. Only Latch calls it.
		SalvageSession(cs::Ref<Ship> player, cs::Ref<Ship> derelict, cs::Ref<SpaceGod::Core::Wreck> wreck, cs::Ref<SpaceGod::Core::Interior> interior, int seed);

		/// <summary>Latches <paramref name="player"/> on to a derelict.</summary>
		static cs::Ref<SalvageSession> Latch(const cs::Ref<Ship>& player, const cs::Ref<Ship>& derelict);

		/// <summary>Latches <paramref name="player"/> on to a wreck: a gutted shell, open to space, with nobody aboard.</summary>
		static cs::Ref<SalvageSession> Latch(const cs::Ref<Ship>& player, const cs::Ref<SpaceGod::Core::Wreck>& wreck);

		/// <summary>Marines the player's ship could send across.</summary>
		double MarinesReady() const;

		/// <summary>
		/// Sends every marine aboard the player's ship through the airlock to fight the raiders aboard.
		/// False if there is nobody to fight, a fight is already on, or the ship carries no marines.
		/// </summary>
		bool SendBoarders();

		/// <summary>Advances the fight aboard by <paramref name="dt"/> seconds.</summary>
		void Step(double dt);

	private:
		void RescueSurvivors();

	public:
		/// <summary>
		/// Moves crates from the hulk into the player's grid hold until it runs out or the hold is full;
		/// what is taken comes off the manifest, so the next salvager finds less. Returns the crates moved.
		/// </summary>
		int SalvageCargo();

		/// <summary>The hulk's parts still worth taking: guns, launchers, engines and shield generators that are not shot away.</summary>
		cs::List<cs::Ref<Subsystem>> Strippable() const;

	private:
		static bool CanStrip(SubsystemKind k);

	public:
		/// <summary>The player's own hardpoint a part could be fitted in place of: a shot-away one of the same kind.</summary>
		cs::Ref<Subsystem> FitFor(const cs::Ref<Subsystem>& part) const;

		/// <summary>The part most worth stripping: one that replaces a shot-away hardpoint on the player's hull
		/// first, else the soundest; null when nothing is left.</summary>
		cs::Ref<Subsystem> BestPart() const;

		/// <summary>What a stripped part breaks down into when it is not fitted.</summary>
		static Good ScrapOf(SubsystemKind k);

		/// <summary>
		/// Cuts <paramref name="part"/> out of the hulk. If the player's hull has a shot-away hardpoint of the
		/// same kind, the part is fitted in its place at the part's own health (and a launcher keeps the rounds
		/// it still had); otherwise it is broken down into a crate (<see cref="ScrapOf"/>) for the hold. Either
		/// way the hulk loses it -- strip its engines and it will never fly again.
		/// </summary>
		StripResult Strip(const cs::Ref<Subsystem>& part);

		/// <summary>A prize crew big enough to fly the hull home.</summary>
		double PrizeCrewNeeded() const;

		/// <summary>Hands the player's ship can spare (it keeps half its own crew) plus any survivors rescued.</summary>
		double PrizeCrewAvailable() const;

		/// <summary>Whether the hull can be claimed now, and if not, why not.</summary>
		// `why` is C#'s out string: empty stands for null.
		bool CanClaim(cs::String& why) const;

		/// <summary>
		/// Claims the derelict: a prize crew (rescued survivors first, then hands from the player's ship) goes
		/// aboard, it flies the player's colours under a new prefix, and -- given a <paramref name="fleet"/> --
		/// goes about that faction's business from where it lies. The session ends.
		/// </summary>
		bool Claim(const cs::Ref<Fleet>& fleet, double jd);

		/// <summary>Lets go of the hulk: any marines still fighting aboard pull back, and survivors come aboard.</summary>
		void CastOff();

	private:
		void Finish();

	public:
		/// <summary>A line for the event log: what came of it.</summary>
		cs::String Summary() const;

		static cs::String KindName(SubsystemKind k);

	private:
		void Note(const cs::String& text);
	};
}
