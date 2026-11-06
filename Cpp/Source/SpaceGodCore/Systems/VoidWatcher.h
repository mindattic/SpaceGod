#pragma once
// US-T178: the Watcher in the void, the dread FTL draws and the signs of it. C++-only (no Unity.SpaceGod counterpart).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class DeepSpace;
	class Cruise;
	class Ship;

	/// <summary>One thing learned about the Watcher, unlocked once enough of its signs are on record.</summary>
	struct SPACEGODCORE_API VoidWatcherLore
	{
		/// <summary>Signs on record anywhere out here before it is known.</summary>
		int Signs = 0;
		cs::String Title;
		cs::String Text;
	};

	/// <summary>The kinds of sign the Watcher gives (never a shape: nothing of it is ever seen whole).</summary>
	enum class WatcherSignKind : int
	{
		/// <summary>A contact on the sensors that is not there.</summary>
		Phantom,
		/// <summary>Voices on a dead channel, through the hull.</summary>
		Whisper,
		/// <summary>The stars going out, a few at a time, and coming back.</summary>
		StarsOut,
		/// <summary>Time lost or doubled: the chronometers disagree (the charge slips at a void stop).</summary>
		TimeSlip,
		/// <summary>The angles of space wrong: a course that will not close (reaction mass burned correcting it).</summary>
		Geometry,
		/// <summary>A crewman driven out of his mind (the flagship's hull hurt).</summary>
		Madness,
		/// <summary>Its heralds: a Void Raider sent to a hull stopped in the void (US-T124).</summary>
		Herald,
	};
	SPACEGODCORE_API const char* ToString(WatcherSignKind v);

	/// <summary>
	/// The Watcher in the void (US-T178). An FTL jump breaks the law the universe keeps, and every jump, and every day spent
	/// in the dark between the stars, draws a little more of the attention of something that was old when the universe was
	/// young. Each hull's crew carries that attention as <see cref="Ship.Dread"/>, 0..1: it grows with every jump
	/// (<see cref="JumpDread"/>) and every day in the void (<see cref="VoidDreadPerDay"/>), and fades in a system's light
	/// (<see cref="FadeDays"/>). The more there is, the more often the signs come (<see cref="SignsPerDay"/> times its
	/// square), and the worse they are, by how far it has come (<see cref="StageOf"/>): sensor phantoms and whispers in the
	/// comms first, then stars going out and time slipping, then the geometry of space going wrong and crews going mad,
	/// and at the last the Void Raiders come where it is looking, its heralds.
	///
	/// Nobody knows at first what it is (void sickness); what it is comes out only as its signs pile up across the galaxy
	/// (<see cref="Lore"/>), in the deep-space log and to the player as news.
	///
	/// A crew's hull gathers dread and sees the signs as the player's does, but the Watcher's hand (the slips, the burns,
	/// the madness, the heralds) falls only on the player's own hull (<see cref="Cruise.Piloted"/>), so the traffic between
	/// the stars, and the Bench, run as they did. Run from <see cref="DeepSpace.Update"/> on its fixed step with its own
	/// random numbers.
	/// </summary>
	class SPACEGODCORE_API VoidWatcher final
	{
	public:
		/// <summary>The dread one FTL jump draws (of what is left of 1).</summary>
		static constexpr double JumpDread = 0.05;

		/// <summary>The dread a day in the void draws (of what is left of 1).</summary>
		static constexpr double VoidDreadPerDay = 0.04;

		/// <summary>How long dread takes to fade to a third (1/e) out of the void, days.</summary>
		static constexpr double FadeDays = 20;

		/// <summary>The rate signs come, per day, at full dread (it goes with the square of the dread).</summary>
		static constexpr double SignsPerDay = 2;

		/// <summary>The dread each stage of its attention begins at: stirring, watching, hunting, upon you.</summary>
		static constexpr double Stage1 = 0.15;
		static constexpr double Stage2 = 0.35;
		static constexpr double Stage3 = 0.6;
		static constexpr double Stage4 = 0.85;

		/// <summary>How much likelier a Void Raider finds the player's hull at a void stop at full dread (its heralds).</summary>
		static constexpr double HeraldBoost = 2;

		/// <summary>Everything there is to learn about it, in the order it comes out.</summary>
		static const cs::List<VoidWatcherLore>& Lore();

		/// <summary>How much is known after <paramref name="signs"/> signs: how many of <see cref="Lore"/>.</summary>
		static int StageFor(int signs);

		/// <summary>What it is called once <paramref name="lore"/> things are known: "void sickness", "something in the
		/// void", then "the Watcher".</summary>
		static cs::String Called(int lore);

		/// <summary>How far its attention has come at <paramref name="dread"/>: 0 (none) to 4 (upon you).</summary>
		static int StageOf(double dread);

		/// <summary>The stage's word for the HUD: CALM, UNEASY, WATCHED, HUNTED, SEEN.</summary>
		static cs::String StageName(int stage);

		/// <summary><paramref name="s"/>'s dread at <paramref name="jd"/>, faded for the time since it was last brought up to
		/// date (for a hull not in the void now).</summary>
		static double DreadAt(const Ship& s, double jd);

		/// <summary>A group's dread: its flagship's.</summary>
		static double DreadOf(const Cruise& c);

		/// <summary>What the player's dread does to a Void Raider's odds of finding it at a void stop (1 for anyone else).</summary>
		static double HeraldFactor(const Cruise& c);

		/// <summary>A step of the deep space, <paramref name="fromJd"/> to <paramref name="toJd"/>: every hull out here
		/// gathers dread, and signs come to the groups it has noticed.</summary>
		static void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng);

	private:
		static void Gather(const cs::Ref<Ship>& s, double fromJd, double toJd, double gain, bool fading);
		static void Sign(DeepSpace& space, const cs::Ref<Cruise>& c, int stage, double jd, cs::Random& rng);
		static void Learn(DeepSpace& space, const cs::Ref<Cruise>& c, double jd);
	};
}
