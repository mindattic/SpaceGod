#pragma once
// US-T285: Culture, what the galaxy's people believe, celebrate, cheer, wear and remember (Civilization.h).
//  - Faiths: the pious found faiths (Org Faith); a faith takes a world's people by degrees (Civics.Faith/FaithShare),
//    carried by trips and by sharing a system, converts the people of the houses there, sends pilgrims to its seat (real
//    trips), splits in schisms, and preaches holy war against a state whose worlds keep its rival faith (opinion falls;
//    a hawkish state may go to war over it).
//  - Festivals: every world keeps a festival on its own day of the year: spending (hotels and traders), visitors from
//    other worlds (trips), a lift to the mood, and raiders where pirates lurk.
//  - Sports: the big worlds field teams (Org Team, backed by a rich house); a league plays a round a month, syndicates fix
//    games, and a champion is crowned every year (its world celebrates; fans travel to the final).
//  - Trends: a fashion sweeps a world and spreads along the trips (hotels and traders sell it).
//  - Cultural drift: colonies far from their capital, keeping another faith, drift apart in speech and custom (Civics.Drift),
//    which feeds GalaxyLife's yearning to secede and Strife's insurgents.
//  - History: historians chronicle the great events (the chronicle the Encyclopedia reads) and worlds raise monuments.
//  - Rumours and propaganda: unverified stories about the famous; states at war, and strongmen, spread propaganda that
//    stiffens their people.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Citizen;
	class Civilization;
	class Galaxy;
	class Org;
	class Settlement;
	class SocialEvent;
	class Society;

	class SPACEGODCORE_API Culture final
	{
	public:
		static constexpr int MaxFaiths = 8;
		static constexpr int MaxTeams = 16;
		static constexpr int MaxChronicle = 200;

		/// <summary>The great events as the historians kept them (oldest first).</summary>
		cs::List<cs::Ref<SocialEvent>> Chronicle = cs::List<cs::Ref<SocialEvent>>();
		/// <summary>The league's season (counting from 1) and the month of it.</summary>
		int Season = 1, Round = 0;

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		/// <summary>The fashion names (Civics.Trend indexes them).</summary>
		static const char* TrendName(int i);

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Preach(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& prophet, double jd);
		cs::String Celebrate(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd);
		cs::String Rig(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& team, double jd);
		cs::String Whisper(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& about, double jd);

	private:
		int seenEvent = 0;

		void Faiths(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Festivals(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Festival(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd, bool steered);
		void League(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Trends(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Drift(Civilization& civ, Galaxy& galaxy, double jd);
		void Talk(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void React(Civilization& civ, Galaxy& galaxy, double jd);

		SG_SERIALIZABLE(Culture);
	};
}
