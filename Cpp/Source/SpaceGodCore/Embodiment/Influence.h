#pragma once
// US-T140: the SpaceGod's Influence, its meta-progression.
//
// The SpaceGod is never one pilot. What it gains as it acts through the bodies it takes is its own, not any pilot's: XP
// for its interventions (an act done in a body that the pilot then owns as their own choice, a squad talked down), for
// the missions it carries through (contracts completed at a helm) and for the careers it steers (a pilot it has flown
// promoted). XP makes levels, and the level is its Influence: how big a hull, and how famous a name, it can take. A
// pilot's reputation, crimes and story stay the pilot's (Law, Characters); only the reach is the SpaceGod's.
//
// One Influence for the whole galaxy, held on Polities (the one object every system shares, so a jump to another star
// keeps it) and saved with it. Co-op seats share it: the players together are the one SpaceGod, so a seat's deeds raise
// everyone's reach, and since it is world state, changed only by the simulation on every lockstep peer alike, the embody
// gate (Embodiables::MayEmbody, run by SeatAuthority on every peer) answers the same everywhere.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class IEmbodiable;
	class Ship;
	class World;
	enum class ShipClass : int;

	/// <summary>Where Influence XP came from (US-T140).</summary>
	enum class InfluenceSource : int
	{
		/// <summary>An act done in a body (owned by its pilot once left), a squad talked down, a pilot first taken.</summary>
		Intervention,
		/// <summary>A contract carried through at a helm.</summary>
		Mission,
		/// <summary>A pilot the SpaceGod has flown promoted up their ladder.</summary>
		Career,
		/// <summary>The dev menu's grant (testing only, single-player).</summary>
		Dev,
		/// <summary>US-T260: a first discovery made in a body (a star charted, a body, sight, anomaly or graveyard surveyed).</summary>
		Discovery,
	};
	SPACEGODCORE_API const char* ToString(InfluenceSource v);

	/// <summary>
	/// The SpaceGod's Influence (US-T140): its XP, the level that makes, and what it has done to earn it. One for the galaxy
	/// (<see cref="Of"/>). The level gates what the embody gate lets it take (<see cref="Needed"/>).
	/// </summary>
	class SPACEGODCORE_API SpaceGodInfluence final
	{
	public:
		static constexpr int MaxLevel = 10;

		double Xp = 0;

		/// <summary>What the XP was earned for, counted.</summary>
		int Interventions = 0, Missions = 0, CareersSteered = 0;

		/// <summary>US-T260: first discoveries credited (absent from older saves).</summary>
		int Discoveries = 0;

		// ---- transient (not saved): the HUD's flash of the last gain, and the dev waiver.

		/// <summary>The last gain, for the HUD: what for, how much, and a sequence number that moves on each gain.</summary>
		cs::String LastGain;
		double LastGainXp = 0;
		int GainSeq = 0;

		/// <summary>Dev only (the dev menu, sg.EvaRepair): the embody limits are off. Never saved.</summary>
		bool Waived = false;

		/// <summary>The XP a level asks for: 50 L (L - 1), so 100 for level 2, 1000 for 5, 4500 for 10.</summary>
		static double XpFor(int level);

		/// <summary>The level (1..<see cref="MaxLevel"/>): the SpaceGod's Influence.</summary>
		int Level() const;

		/// <summary>How far to the next level, 0..1 (1 at the top).</summary>
		double Progress() const;

		/// <summary>What the level is called ("Whisper" ... "Godhead").</summary>
		static cs::String Title(int level);

		/// <summary>The galaxy's Influence (made the first time it is asked for: older saves have none); null without a world.</summary>
		static cs::Ref<SpaceGodInfluence> Of(const cs::Ref<World>& world);

		/// <summary>The galaxy's Influence if it has one yet, never making it (for readers that must not change the world).</summary>
		static cs::Ref<SpaceGodInfluence> Find(const cs::Ref<World>& world);

		/// <summary>Adds <paramref name="xp"/> for <paramref name="why"/>; returns whether a level was gained.</summary>
		static bool Grant(const cs::Ref<World>& world, InfluenceSource source, double xp, const cs::String& why);

		/// <summary>The Influence a hull class asks for (US-T140): small craft and working hulls 1, up to a gate breaker's 7.</summary>
		static int TierOf(ShipClass c);

		/// <summary>
		/// The Influence <paramref name="seat"/> asks for: its hull's tier (a gun or an EVA worker on it two less, a station by
		/// what it is), more for a named Epic entity: the flagship of a feared outlaw captain, or a pilot at the top of their
		/// ladder (warlord, pirate king, captain). <paramref name="why"/> names what it is. Reads the world only, never changes it.
		/// </summary>
		static int Needed(const cs::Ref<World>& world, const cs::Ref<IEmbodiable>& seat, cs::String& why);

		SG_SERIALIZABLE(SpaceGodInfluence);
	};
}
