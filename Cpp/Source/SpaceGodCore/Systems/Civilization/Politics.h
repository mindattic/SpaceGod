#pragma once
// US-T278: Politics, the government side of the Civilization layer (Civilization.h).
//  - Ideology: each world's people lean hawk or dove, crown or vote (Civics); war with high approval breeds hawks, war
//    weariness doves, prosperity the vote and unrest the strongman. A state's stance follows its people's, weighted by
//    their numbers: a hawkish state sours toward everyone and, past a point, may go to war with the one it likes least
//    (Statecraft::DeclareWar); a dovish one warms.
//  - Parties and elections: a state whose people lean to the vote keeps parties (Org Party, hawk or dove, funded by the
//    houses whose heads want power) and holds an election every four years; money and the public mood carry the vote, a
//    bribed count may be exposed (a rigged election, unrest). The winner rules: doves warm the state to its enemies, hawks
//    cool it.
//  - Rulers, coups and successions: each state has a named ruler (the winner's leader, else the head of the richest
//    house at its capital). Unrest, low approval and an ambitious plotter make a coup; a ruler's death leaves a crisis.
//  - Courts: scandals, frauds, murders and failed coups go to trial; verdicts follow, unless the judge is bought.
//  - Peace summits: two war-weary states meet on a neutral world and sign (Statecraft::MakePeace), or the talks fail.
//  - Polls: every quarter each state's approval is published.
// Changes to the wider simulation (opinions, wars, unrest) happen only here, so a Core-only run never moves.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Citizen;
	class Civilization;
	class Faction;
	class Galaxy;
	class Org;
	class Settlement;
	class Society;
	enum class SocialKind : int;

	/// <summary>One state's politics (US-T278).</summary>
	class SPACEGODCORE_API Regime final
	{
	public:
		cs::Ref<SpaceGod::Core::Faction> Faction;
		/// <summary>-1 dove .. 1 hawk; -1 crown .. 1 vote (its people's, weighted by their numbers).</summary>
		double Hawk = 0, Liberty = 0;
		/// <summary>-1..1: how its people feel about their rulers (weighted).</summary>
		double Approval = 0;
		cs::Ref<Citizen> Ruler;
		cs::Ref<Org> Ruling;
		/// <summary>When the next election is held (NaN: none, it does not vote), and when one was called.</summary>
		double NextElectionJd = cs::Double::NaN;
		bool Called = false;
		/// <summary>A ruler died: who rules is in doubt until then (NaN: settled).</summary>
		double CrisisUntil = cs::Double::NaN;
		int Coups = 0, Elections = 0;
		/// <summary>Whether its last stance was hawkish (1), dovish (-1) or neither, for the shift news.</summary>
		int Leaning = 0;
	};

	/// <summary>A case before a court (US-T278).</summary>
	class SPACEGODCORE_API Trial final
	{
	public:
		int Id = 0;
		cs::Ref<Citizen> Accused;
		cs::Ref<Settlement> Where;
		SocialKind Charge{};
		double OpenedJd = 0, VerdictJd = 0;
		bool Decided = false, Guilty = false, Bought = false, Steered = false;
	};

	/// <summary>Two states' peace talks, for the cooldown (US-T278).</summary>
	class SPACEGODCORE_API Talks final
	{
	public:
		cs::Ref<Faction> A, B;
		double Jd = 0;
	};

	class SPACEGODCORE_API Politics final
	{
	public:
		static constexpr double TermDays = 4 * 365.25;
		static constexpr double TalksCooldownDays = 180;

		cs::List<cs::Ref<Regime>> Regimes = cs::List<cs::Ref<Regime>>();
		cs::List<cs::Ref<Trial>> Trials = cs::List<cs::Ref<Trial>>();
		cs::List<cs::Ref<Talks>> Summits = cs::List<cs::Ref<Talks>>();

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		cs::Ref<Regime> RegimeOf(const cs::Ref<Faction>& f) const;
		cs::Ref<Trial> TrialById(int id) const;
		/// <summary>Puts someone on trial (any module may: Underworld for a murder, Press for what a journalist found).</summary>
		cs::Ref<Trial> Charge(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& accused, SocialKind charge, double jd, bool steered = false);

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Elect(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& party, double jd);
		cs::String Overthrow(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& plotter, double jd);
		cs::String Acquit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Trial>& t, double jd);
		cs::String Summit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd);

	private:
		int seenEvent = 0;

		void Census(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Ideology(Civilization& civ, Galaxy& galaxy, double jd);
		void Parties(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Elections(Civilization& civ, Galaxy& galaxy, double jd);
		void Win(Civilization& civ, Galaxy& galaxy, const cs::Ref<Regime>& r, const cs::Ref<Org>& party, double share, double jd, bool steered);
		void Coups(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Seize(Civilization& civ, Galaxy& galaxy, const cs::Ref<Regime>& r, const cs::Ref<Citizen>& plotter, double jd, bool steered);
		void Courts(Civilization& civ, Galaxy& galaxy, double jd);
		void PeaceTalks(Civilization& civ, Galaxy& galaxy, double jd);
		bool Meet(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, bool force);
		void Polls(Civilization& civ, Galaxy& galaxy, double jd);
		void React(Civilization& civ, Galaxy& galaxy, double jd);
		cs::Ref<Citizen> Heir(Galaxy& galaxy, Society& society, const cs::Ref<Faction>& f, double jd) const;

		SG_SERIALIZABLE(Politics);
	};
}
