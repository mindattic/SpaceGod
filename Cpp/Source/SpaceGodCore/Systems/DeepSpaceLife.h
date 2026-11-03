#pragma once
// Port of Assets/UnitySpace/Core/Systems/DeepSpaceLife.cs (see docs/MIGRATION.md). The DeepSpace partial half of
// that file is declared in Systems/DeepSpace.h and defined in DeepSpaceLife.cpp.

#include "Cs/Cs.h"
#include "GalacticPosition.h"
#include "Systems/DeepSpace.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Cruise;
	class GravityProjector;
	class DeepSpaceObject;

	/// <summary>Where on a route a kind of deep-space feature is put.</summary>
	enum class FeaturePlacement : int
	{
		/// <summary>Anywhere along the way, a little off the line.</summary>
		Scattered,

		/// <summary>Half way, on the line (a relay between two systems).</summary>
		Midway,

		/// <summary>On a settled end's approach, a tenth of the way out (a listening post).</summary>
		Approach,
	};
	SPACEGODCORE_API const char* ToString(FeaturePlacement v);

	/// <summary>
	/// One kind of thing that lies out between the stars (U.SPC-T421): how many of it a route holds, on the gate network
	/// and off it, where along the route it is put, and what it is made of (<see cref="Make"/>). A new kind plugs in by
	/// registering one with <see cref="DeepSpaceFeatures.Register"/>, never by editing the seeding.
	/// </summary>
	class DeepSpaceFeatureSpec final
	{
	public:
		DeepSpaceObjectKind Kind = DeepSpaceObjectKind::Wreck;

		/// <summary>What it is called in the logs ("rogue planet").</summary>
		cs::String Noun;

		/// <summary>How many lie along ten light-years of a route the gates join, and of one they do not.</summary>
		double OnNetworkPer10Ly = 0, OffNetworkPer10Ly = 0;

		FeaturePlacement Placement = FeaturePlacement::Scattered;

		/// <summary>The most km off the route's line it lies (a scattered one).</summary>
		double MaxOffsetKm = 0;

		/// <summary>Owned by a power at a settled end of the route (a relay, a listening post): none is put where there is none.</summary>
		bool Owned = false;

		/// <summary>Lost to the dark when too many are left out here (wrecks, derelicts, distress calls).</summary>
		bool Transient = false;

		/// <summary>What its first survey pays, credits (times <see cref="DeepSpaceFeatures.OffNetworkSurveyFactor"/> off the network).</summary>
		double SurveyCredits = 0;

		/// <summary>Fills in what this one is: its name, size, mass, salvage (the route's own numbers, so the same every time).</summary>
		cs::Action<cs::Ref<DeepSpaceObject>, cs::Random&> Make;
	};

	/// <summary>
	/// The things that lie out between the stars (U.SPC-T421): rogue planets, nebulae, ice bodies, derelicts, and the
	/// powers' relays and listening posts. Every route a group flies (or the helm plots) is seeded with them the first
	/// time, from the route's own name and the galaxy's seed, so the same route always holds the same things. Routes the
	/// gates do not join hold the most, and pay the most to survey.
	/// </summary>
	class SPACEGODCORE_API DeepSpaceFeatures
	{
	public:
		/// <summary>Jupiter's gravitational parameter, km³/s² (a rogue planet's mass is reckoned in it).</summary>
		static constexpr double JupiterGm = 1.26686534e8;

		/// <summary>What a first survey off the gate network pays, against one on it.</summary>
		static constexpr double OffNetworkSurveyFactor = 3;

		/// <summary>The longest route seeded, light-years (beyond it the stars are too far apart for anyone to fly it).</summary>
		static constexpr double MaxRouteLy = 60;

		/// <summary>The most of one kind put along one route.</summary>
		static constexpr int MaxPerRoute = 6;

	private:
		static const cs::List<cs::String>& Greek();
		static const cs::List<cs::String>& Wanderers();
		static const cs::List<cs::String>& Veils();
		static const cs::List<cs::String>& Ices();
		static const cs::List<cs::String>& Hulls();

		static cs::String Pick(const cs::List<cs::String>& list, cs::Random& r);

		static cs::Dictionary<DeepSpaceObjectKind, cs::Ref<DeepSpaceFeatureSpec>>& specs();

		/// <summary>C#'s static constructor (static DeepSpaceFeatures()): run once, before the first use of any member.</summary>
		static void StaticConstructor();

	public:
		/// <summary>Adds (or replaces) the kind <paramref name="spec"/> describes; null <paramref name="spec"/> with
		/// <see cref="Unregister"/> takes one away.</summary>
		static void Register(cs::Ref<DeepSpaceFeatureSpec> spec);

		static void Unregister(DeepSpaceObjectKind kind);

		static cs::Ref<DeepSpaceFeatureSpec> Of(DeepSpaceObjectKind kind);

		/// <summary>Every registered kind, in the enum's order (so seeding draws its numbers the same way every time).</summary>
		static cs::List<cs::Ref<DeepSpaceFeatureSpec>> All();

		/// <summary>Lost to the dark when there are too many (see <see cref="DeepSpace.Add"/>).</summary>
		static bool Transient(DeepSpaceObjectKind kind);

		/// <summary>A hash of <paramref name="s"/> that is the same in every run (string.GetHashCode is not).</summary>
		static uint64_t StableHash(const cs::String& s);
	};

	/// <summary>A route between two stars that has been seeded with what lies along it (U.SPC-T421).</summary>
	class SPACEGODCORE_API DeepSpaceRoute final
	{
	public:
		/// <summary>The two ends, in name order, and where they are.</summary>
		cs::String A, B;
		GalacticPosition AtA, AtB;

		/// <summary>A gate link joins the two (so it is charted, and its finds pay less).</summary>
		bool OnNetwork = false;

		double Ly() const;

		cs::String Name() const;
	};

	/// <summary>A band lying in wait in open space with a gravity projector on an FTL route (U.SPC-T421).</summary>
	class DeepSpaceAmbush final
	{
	public:
		cs::Ref<Cruise> Band;
		cs::Ref<GravityProjector> Projector;

		/// <summary>When it lifts, and the leg the band was holding on (a fight that sends it off early ends it too).</summary>
		double UntilJd = 0;
		double LegJd = 0;

		cs::String Where;
	};

	/// <summary>
	/// Survey finds (U.SPC-T421): a deep-space explorer, or any hull out on a survey, passing within <see cref="RangeKm"/> of
	/// something nobody has surveyed (a rogue planet, a nebula, an ice body) drops out to look it over for
	/// <see cref="SurveyDays"/>, and its flag is paid for the charts: three times as much off the gate network. The player's
	/// own hull logs it on the way past without stopping.
	/// </summary>
	class SPACEGODCORE_API SurveyFinds final : public IDeepSpaceEncounter
	{
	public:
		/// <summary>How near (km) a survey hull's instruments pick a find out: about five light-hours.</summary>
		static constexpr double RangeKm = 5e9;

		static constexpr double SurveyDays = 0.5;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;
	};

	/// <summary>
	/// Ice for the tanks (U.SPC-T421): a group with a hull below <see cref="RefuelBelow"/> of its reaction mass, passing
	/// within <see cref="RangeKm"/> of an ice body with water left on it, drops out for <see cref="IceDays"/> to mine it and
	/// fills its tanks as far as the ice goes.
	/// </summary>
	class SPACEGODCORE_API IceRefuelling final : public IDeepSpaceEncounter
	{
	public:
		static constexpr double RangeKm = 1e9;
		static constexpr double IceDays = 1;
		static constexpr double RefuelBelow = 0.5;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;
	};

	/// <summary>
	/// Distress calls (U.SPC-T421). Now and then a hull goes adrift on a route with its crew aboard and calls for help; a
	/// passing group within <see cref="RangeKm"/> answers it (a war fleet on campaign does not), drops out for
	/// <see cref="RescueDays"/> and is paid by the survivors' rescue bounty. Pirates who hear it strip it instead. Some calls
	/// are lures: a clan lies in wait at them, robs whoever answers or, against a strong enough warship, is beaten off. A
	/// call nobody answers falls silent and leaves a derelict.
	/// </summary>
	class SPACEGODCORE_API DistressCalls final : public IDeepSpaceEncounter
	{
	public:
		/// <summary>How many calls go out a day across all the routes seeded.</summary>
		static constexpr double CallsPerDay = 0.1;

		/// <summary>How near (km) a group must pass to hear one and turn aside: about a light-hour.</summary>
		static constexpr double RangeKm = 1e9;

		static constexpr double RescueDays = 1;

		/// <summary>What a rescued soul's bounty pays the rescuer, credits.</summary>
		static constexpr double BountyPerSoul = 4000;

		/// <summary>Share of calls that are pirate lures (when there is a clan to set one).</summary>
		static constexpr double LureShare = 0.2;

		/// <summary>The fighting strength lying in wait at a lure.</summary>
		static constexpr double LureStrength = 60;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;

	private:
		static void Call(DeepSpace& space, double jd, cs::Random& r);

		static void Answer(DeepSpace& space, cs::Ref<Cruise> c, cs::Ref<DeepSpaceObject> o, double jd);
	};

	/// <summary>
	/// The powers' relays and listening posts out on the routes (U.SPC-T421). A listening post hears an enemy group
	/// pass within <see cref="ListenRangeKm"/> and tells its home system; an enemy group with guns passing within a
	/// light-hour of either knocks it out, leaving a wreck.
	/// </summary>
	class SPACEGODCORE_API DeepSpaceOutposts final : public IDeepSpaceEncounter
	{
	public:
		/// <summary>How far (km) a listening post hears a drive: about a light-day.</summary>
		static constexpr double ListenRangeKm = 2.6e10;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;

	private:
		static void KnockOut(DeepSpace& space, cs::Ref<Cruise> c, cs::Ref<DeepSpaceObject> o, double jd);
	};

	/// <summary>
	/// The bands lying in wait on the routes (U.SPC-T421): it keeps their gravity projectors up while they hold at their
	/// stations, and takes them down when they go (<see cref="DeepSpace.LieInWait"/>). The fights themselves are the
	/// ordinary meetings out here (<see cref="CruiseMeeting"/>): a passer pulled out by the projector is within reach.
	/// </summary>
	class SPACEGODCORE_API RouteAmbushes final : public IDeepSpaceEncounter
	{
	public:
		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;
	};
}
