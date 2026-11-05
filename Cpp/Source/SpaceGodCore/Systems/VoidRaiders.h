#pragma once
// US-T124: the Void Raiders, ghost ships that strike hulls stranded in the void between jumps (US-T86). C++-only (no
// Unity.SpaceGod counterpart).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class DeepSpace;
	class Cruise;

	/// <summary>One thing learned about the Void Raiders, unlocked once enough raids are on record.</summary>
	struct SPACEGODCORE_API VoidRaiderLore
	{
		/// <summary>Raids (and ghost wrecks, which count double) on record before it is known.</summary>
		int Raids = 0;
		cs::String Title;
		cs::String Text;
	};

	/// <summary>
	/// The Void Raiders (US-T124): ghost ships that hunt hulls stranded in the void between jumps (<see cref="Cruise.AtVoidStop"/>),
	/// while their drives cool and spool for the next jump. Nobody knows what they are at first: they are unidentified sensor
	/// ghosts, and what they are comes clear only as raids pile up (<see cref="Lore"/>), each new thing learned put out as news.
	///
	/// <list type="bullet">
	/// <item>The odds. A ghost finds a hull at a stop at a rate that grows with how long it has sat there
	/// (<see cref="HazardPerDay2"/> times the days since it dropped out), so the chance over a whole stop grows with the
	/// square of its length: a capital drive's 18-hour stop is a few percent, a slow ring's or a drive scaled up in the menu
	/// far more. A fast spool is the best defence. Near a star or a void station (<see cref="ShelterLy"/>) they never come.</item>
	/// <item>The approach. A ghost found closes for <see cref="StalkDays"/> before it strikes; a hull that engages first is
	/// gone. Its sensors may pick the ghost out on the way (passively, <see cref="PassiveSpotPerStep"/> a step).</item>
	/// <item>The strike. One to three ghosts each wreck an escort or hit the flagship (hull, tanks, and the drive scrambled,
	/// putting its charge back), and some are wrecked by the group's guns, leaving a wreck of no known yard. A strike seen
	/// coming is met: it does less, and more ghosts are lost.</item>
	/// <item>The player's sensors (<see cref="SensorPosture"/>), at a stop only: Tab pings (a ghost closing is seen at once
	/// and may break off, but for <see cref="PingLitDays"/> the hull is lit and three times as findable); Shift+F3 runs dark
	/// (a quarter as findable, a ghost closing may lose the hull, but no charge builds while dark).</item>
	/// </list>
	/// Run from <see cref="DeepSpace.Update"/> on the deep space's fixed step with its own random numbers, so nothing else out
	/// there draws differently; a crew's hull meets them as the player's does, only the sensor keys are the player's.
	/// </summary>
	class SPACEGODCORE_API VoidRaiders final
	{
	public:
		/// <summary>The rate a ghost finds a hull at a stop, per day, for each day it has sat there.</summary>
		static constexpr double HazardPerDay2 = 0.18;

		/// <summary>No ghost comes within this of a star or a void station, light-years.</summary>
		static constexpr double ShelterLy = 0.5;

		/// <summary>How long a ghost closes before it strikes, days.</summary>
		static constexpr double StalkDays = 0.1;

		/// <summary>How findable a dark hull is, and a lit one (just pinged), against a hull at rest.</summary>
		static constexpr double DarkHazard = 0.25;
		static constexpr double LitHazard = 3;

		/// <summary>How long a ping leaves the hull lit, days, and how long before the next.</summary>
		static constexpr double PingLitDays = 0.05;
		static constexpr double PingCooldownDays = 0.05;

		/// <summary>The chance a step that sensors at rest pick out a ghost closing (half of it while dark), that a dark hull
		/// loses one closing, and that a ghost caught in a ping breaks off.</summary>
		static constexpr double PassiveSpotPerStep = 0.3;
		static constexpr double DarkLosePerStep = 0.35;
		static constexpr double PingBreakOff = 0.4;

		/// <summary>How far a hit on the flagship puts its charge back, days (less when the strike was met).</summary>
		static constexpr double ScrambleDays = 0.25;

		/// <summary>What a ghost wreck is worth to a salvager, credits.</summary>
		static constexpr double GhostWreckCredits = 250000;

		/// <summary>Everything there is to learn about them, in the order it comes out.</summary>
		static const cs::List<VoidRaiderLore>& Lore();

		/// <summary>How much is known after <paramref name="raids"/> raids: how many of <see cref="Lore"/>.</summary>
		static int StageFor(int raids);

		/// <summary>What they are called once <paramref name="stage"/> things are known: "unidentified sensor ghost",
		/// "ghost ship", then "Void Raider".</summary>
		static cs::String Called(int stage);

		/// <summary>Whether <paramref name="c"/> is near enough a star or a void station at <paramref name="jd"/> to be safe.</summary>
		static bool Sheltered(DeepSpace& space, const cs::Ref<Cruise>& c, double jd);

		/// <summary>The hazard (the expected number of ghosts finding it) for <paramref name="c"/> at its stop from
		/// <paramref name="t0"/> to <paramref name="t1"/>.</summary>
		static double Hazard(const Cruise& c, double t0, double t1);

		/// <summary>A step of the deep space, <paramref name="fromJd"/> to <paramref name="toJd"/>: ghosts find hulls at their
		/// stops, close, strike or lose them.</summary>
		static void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng);

		/// <summary>The player's ping at a void stop: why not (empty when done).</summary>
		static cs::String Ping(const cs::Ref<Cruise>& c, double jd);

		/// <summary>The player's hull at a void stop goes dark, or powers up again: what happened, or why not.</summary>
		static cs::String ToggleDark(const cs::Ref<Cruise>& c, double jd, bool& changed);

		/// <summary>Whether the hull at the stop is lit by its own ping at <paramref name="jd"/>.</summary>
		static bool Lit(const Cruise& c, double jd);

	private:
		static void Strike(DeepSpace& space, const cs::Ref<Cruise>& c, double jd, cs::Random& rng);
		static void Learn(DeepSpace& space, const cs::Ref<Cruise>& c, int found, double jd);
		static void Report(const cs::Ref<Cruise>& c, const cs::String& text, double jd);
	};
}
