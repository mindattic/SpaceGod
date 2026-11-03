#pragma once
// Port of Assets/UnitySpace/Core/Systems/FtlNavigation.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Systems/CruiseDrive.h"

namespace SpaceGod::Core
{
	class FtlTarget;
	class StarSystem;
	class Ship;
	class CruiseDriveSpec;
	class Galaxy;
	class RangeSetting;

	/// <summary>
	/// A course plotted for the helm's FTL drive (U.SPC-T420): from where the hull is to a star or a beacon, every part of
	/// the trip laid out before it is flown. The run out under the ordinary drive to the departure point beyond the outer
	/// rim (canon, U.SPC-T455: no jump inside a system's gravity wells), any wait for the drive to cool, the charge, the
	/// light-years under FTL, each star's well or gravity projector on the line that would pull the hull out to cross it
	/// under the ordinary drive (U.SPC-T418), the reaction mass all of it burns, and when it drops out at the far end.
	///
	/// Worked out with the same rules the trip is then flown by (<see cref="FtlRules"/>, <see cref="CruiseProfile"/>,
	/// <see cref="DeepSpace.FieldsAlong"/>), so the ETA and the fuel are the trip's own, not a guess.
	/// </summary>
	// A C# null string (Refusal: none) is the empty string here.
	class SPACEGODCORE_API FtlCourse final
	{
	public:
		cs::Ref<FtlTarget> Target;
		cs::Ref<StarSystem> From;
		cs::Ref<SpaceGod::Core::Ship> Ship;
		cs::Ref<CruiseDriveSpec> Drive;

		/// <summary>When it was plotted, and where the hull was then (km from <see cref="From"/>'s root).</summary>
		double PlottedJd = 0;
		DVec3 StartKm;

		/// <summary>Where the drive engages: beyond the outer rim on the line to the target, clear of every well (km from the root).</summary>
		DVec3 DepartureKm;

		/// <summary>The run out to <see cref="DepartureKm"/> under the ordinary drive: how far, how long, its acceleration
		/// (the hull's own, or less to leave the jump its reaction mass) and what it burns.</summary>
		double OutRunKm = 0;
		double OutRunDays = 0;
		double OutRunAccelKmS2 = 0;
		double OutRunFuelTonnes = 0;

		/// <summary>Days waiting at the departure point for the drive to cool from its last drop-out.</summary>
		double CoolDays = 0;

		/// <summary>The charge before the first engagement, days.</summary>
		double SpoolDays = 0;

		/// <summary>Light-years from the departure point to the drop-out at the edge of the far star's well.</summary>
		double Ly = 0;

		/// <summary>Days under way under FTL, each re-spool after an interdiction included.</summary>
		double FtlDays = 0;

		/// <summary>The wells and projector fields on the line that would pull the hull out (in order), and the days spent
		/// crossing them under the ordinary drive.</summary>
		cs::List<cs::String> Interdictions;
		double CrossingDays = 0;

		/// <summary>Reaction mass the jump (every leg of it) burns, tonnes.</summary>
		double JumpFuelTonnes = 0;

		/// <summary>The tanks when plotted, tonnes, and how far one jump on them goes, light-years.</summary>
		double TanksTonnes = 0;
		double RangeLy = 0;

		/// <summary>Why the course cannot be flown, or null.</summary>
		cs::String Refusal;

		bool Flyable() const { return Refusal.empty(); }

		/// <summary>The whole trip, days: out-run, cooling, charge, FTL and crossings.</summary>
		double TotalDays() const { return OutRunDays + CoolDays + SpoolDays + FtlDays + CrossingDays; }

		/// <summary>When it drops out at the far end.</summary>
		double EtaJd() const { return PlottedJd + TotalDays(); }

		/// <summary>Reaction mass the whole trip burns, tonnes.</summary>
		double FuelTonnes() const { return OutRunFuelTonnes + JumpFuelTonnes; }

		/// <summary>Whether the hull needs a run out before it can engage (it is inside the rim, or a well or a field).</summary>
		bool NeedsOutRun() const { return OutRunKm > 1; }

		/// <summary>How far past the rim the departure point is put, so a drift does not fall back inside it.</summary>
		static constexpr double RimMargin = 1.05;

		/// <summary>The most interdictions looked for along a course (a run through a crowded cluster stops looking there).</summary>
		static constexpr int MaxInterdictions = 12;

		// private in C#; public so cs::New can make one (only Plot does).
		FtlCourse() = default;

		/// <summary>
		/// Plots the course for <paramref name="ship"/>, at <paramref name="fromKm"/> (km from <paramref name="here"/>'s root),
		/// to <paramref name="target"/> at <paramref name="jd"/>. Always returns a course; one that cannot be flown says why
		/// in <see cref="Refusal"/> (and still carries what could be worked out, such as the distance).
		/// </summary>
		static cs::Ref<FtlCourse> Plot(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<SpaceGod::Core::Ship>& ship, DVec3 fromKm, const cs::Ref<FtlTarget>& target, double jd);

		/// <summary>The most of the reaction mass left past the jump's that the run out may burn.</summary>
		static constexpr double OutRunShare = 0.5;

		/// <summary>The gentlest run out the autopilot will fly (about a ten-thousandth of a g): below it, a refusal.</summary>
		static constexpr double MinOutRunAccelKmS2 = 1e-6;

	private:
		/// <summary>Days into <paramref name="profile"/> (its charge not counted) until <paramref name="ly"/> is covered: where a
		/// run at full speed is pulled out at a field's edge.</summary>
		static double DaysToReach(const CruiseProfile& profile, double ly);

	public:
		/// <summary>Seconds to fly <paramref name="km"/> from rest to rest at <paramref name="accelKmS2"/>, flipping half way.</summary>
		static double OutRunSeconds(double km, double accelKmS2) { return km <= 0 ? 0 : 2 * cs::Math::Sqrt(km / cs::Math::Max(1e-12, accelKmS2)); }

		/// <summary>
		/// Where a hull at <paramref name="fromKm"/> in <paramref name="here"/> engages for <paramref name="target"/>: where it is,
		/// if it is already clear to jump; otherwise just beyond the outer rim on the line from the star to the target, pushed
		/// clear of any planet's or moon's well and of any gravity projector's field.
		/// </summary>
		static DVec3 DeparturePoint(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, DVec3 fromKm, const cs::Ref<FtlTarget>& target);

		/// <summary>The way from <paramref name="fromKm"/> to the target (unit) and how far, as the helm sees it.</summary>
		DVec3 Bearing(DVec3 fromKm, double& km) const;

		cs::String ToString() const;
	};

	/// <summary>Where the FTL autopilot is in flying a plotted course (U.SPC-T420).</summary>
	enum class FtlAutopilotStage : int
	{
		Off,

		/// <summary>Flying out to the departure point beyond the rim under the ordinary drive, the clock compressed.</summary>
		OutRun,

		/// <summary>At the departure point, turning the nose onto the target.</summary>
		Aligning,

		/// <summary>Aligned, but the drive is still cooling from its last jump: held, the nose on the target, until it has cooled.</summary>
		Cooling,

		/// <summary>The drive charging (the helm's own <see cref="HelmFtl"/>), the nose held on the target.</summary>
		Spooling,
	};
	SPACEGODCORE_API const char* ToString(FtlAutopilotStage v);

	/// <summary>What a step of the autopilot came to.</summary>
	enum class FtlAutopilotEvent : int
	{
		None,

		/// <summary>Out at the departure point: turning onto the target next.</summary>
		Arrived,

		/// <summary>The nose is on the target: the drive can be spooled.</summary>
		Aligned,

		/// <summary>Let go of (a fight, or the course could not be flown on): see <see cref="FtlAutopilot.Reason"/>.</summary>
		Disengaged,
	};
	SPACEGODCORE_API const char* ToString(FtlAutopilotEvent v);

	/// <summary>
	/// The FTL autopilot (U.SPC-T420): given a plotted <see cref="FtlCourse"/>, flies the hull out beyond the rim under its
	/// ordinary drive, turns its nose onto the target, spools the drive and engages it, so a trip to another star is one
	/// key or one click on the galaxy map. The run out is worked out closed-form, flip-and-burn from rest to rest, so where
	/// the hull is never depends on the frame rate and the clock can be run as fast as a long trip needs
	/// (<see cref="FtlNavigation.WarpFor"/>). A fight lets go of it, as does a course gone bad.
	///
	/// Engine-free: the Runtime helm puts the hull where <see cref="PositionKm"/> says and turns it as <see cref="Turn"/> does.
	/// </summary>
	class SPACEGODCORE_API FtlAutopilot final
	{
	public:
		FtlAutopilotStage Stage = FtlAutopilotStage::Off;
		cs::Ref<FtlCourse> Course;

		/// <summary>The run out: from, to (km from the root), when it began and ends, at what acceleration.</summary>
		DVec3 RunFromKm;
		DVec3 RunToKm;
		double RunStartJd = cs::Double::NaN;
		double RunEndJd = cs::Double::NaN;
		double RunAccelKmS2 = 0;

		/// <summary>Why it last let go, or why it would not engage.</summary>
		cs::String Reason;

		bool Active() const { return Stage != FtlAutopilotStage::Off; }

		/// <summary>How fast the nose is swung onto the target while aligning, degrees a real second.</summary>
		static constexpr double TurnDegPerSecond = 30;

		/// <summary>
		/// Takes the helm to fly <paramref name="course"/> from <paramref name="fromKm"/> at <paramref name="jd"/>: refused (with
		/// <see cref="Reason"/>) if it cannot be flown or the hull is fighting. The run out's reaction mass is spent now.
		/// </summary>
		bool Engage(const cs::Ref<FtlCourse>& course, DVec3 fromKm, bool inCombat, double jd);

		/// <summary>0..1 of the way out.</summary>
		double RunFraction(double jd) const;

		/// <summary>Where the hull is at <paramref name="jd"/> (km from the root): on the run out, else where it ended.</summary>
		DVec3 PositionKm(double jd) const { return DVec3::Lerp(RunFromKm, RunToKm, RunFraction(jd)); }

		/// <summary>Its velocity at <paramref name="jd"/> on the run out (km/s against the root); nil at either end.</summary>
		DVec3 VelocityKmS(double jd) const;

		/// <summary>The way the run heads (unit), the nose prograde as every hull's is through a transit.</summary>
		DVec3 RunDirection() const;

		/// <summary>Days left of the run out.</summary>
		double RunDaysLeft(double jd) const { return Stage == FtlAutopilotStage::OutRun ? cs::Math::Max(0.0, RunEndJd - jd) : 0; }

		/// <summary>
		/// A step at <paramref name="jd"/>: a fight lets go of the helm, and the run out, once done, gives way to aligning.
		/// </summary>
		FtlAutopilotEvent Step(bool inCombat, double jd);

		/// <summary>
		/// Aligning: <paramref name="forward"/> turned toward the target by at most <paramref name="maxDeg"/> degrees; once
		/// within <see cref="HelmFtl.AlignDeg"/>, <see cref="FtlAutopilotEvent.Aligned"/>.
		/// </summary>
		FtlAutopilotEvent Turn(DVec3& forward, DVec3 fromKm, double maxDeg);

		/// <summary>The drive is still cooling: hold here, nose on the target, until <see cref="FtlRules.CoolUntil"/>.</summary>
		void Cooling() { if (Active()) Stage = FtlAutopilotStage::Cooling; }

		/// <summary>The drive is spooling for the course: the helm's charge has it from here.</summary>
		void Spooling() { if (Active()) Stage = FtlAutopilotStage::Spooling; }

		/// <summary><paramref name="from"/> (unit) swung toward <paramref name="to"/> by at most <paramref name="maxRad"/>.</summary>
		static DVec3 TurnToward(DVec3 from, DVec3 to, double maxRad);

		/// <summary>Lets go of the helm, saying why, giving back the reaction mass of the part of the run out not flown by <paramref name="jd"/>.</summary>
		FtlAutopilotEvent Disengage(const cs::String& why, double jd);

		/// <summary>Off, the course forgotten, the unflown share of the run out's reaction mass refunded as of <paramref name="jd"/>.</summary>
		void Abandon(double jd);

		/// <summary>Off, the course forgotten.</summary>
		void Reset();
	};

	/// <summary>
	/// Interstellar navigation (U.SPC-T420): the stars and beacons in the order a navigator offers them, and the time
	/// compression a long trip is flown at, tied to the sim clock so everything else in the galaxy moves on with it. Its
	/// tuning is in the settings menu's FTL group.
	/// </summary>
	// [SettingsSource]: the settings registry finds TripRealSeconds by touching its accessor.
	class SPACEGODCORE_API FtlNavigation
	{
	public:
		static const cs::Ref<RangeSetting>& TripRealSeconds();

		/// <summary>
		/// The time warp (an index into <see cref="SimClock.Levels"/>) that gets <paramref name="days"/> of a trip over in about
		/// <see cref="TripRealSeconds"/> of real time (or <paramref name="realSeconds"/>), never slower than <paramref name="atLeast"/>.
		/// </summary>
		static int WarpFor(double days, int atLeast = 0, double realSeconds = cs::Double::NaN);

		/// <summary>Every star and beacon on offer from <paramref name="here"/>, nearest to <paramref name="fromKm"/> first.</summary>
		static cs::List<cs::Ref<FtlTarget>> Nearest(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, DVec3 fromKm);

		/// <summary>The target after (or, with <paramref name="step"/> -1, before) <paramref name="current"/> in
		/// <paramref name="ordered"/>, wrapping round; the first if <paramref name="current"/> is not in it.</summary>
		static cs::Ref<FtlTarget> Cycle(const cs::List<cs::Ref<FtlTarget>>& ordered, const cs::Ref<FtlTarget>& current, int step);

		/// <summary>Whether two targets are the same place: the same running system, or the same star on the charts by name.</summary>
		static bool Same(const cs::Ref<FtlTarget>& a, const cs::Ref<FtlTarget>& b);

		/// <summary>A short description of a trip's length: hours, days or years.</summary>
		static cs::String Describe(double days);
	};
}
