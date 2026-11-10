#pragma once
// US-T282: Underworld, the criminal side of the Civilization layer (Civilization.h).
//  - Crime: each world's crime level (Civics) follows poverty, unrest, lawlessness and the syndicates' grip, against its
//    garrison; past a point there is a crime wave, and a strongman government cracks down (crime and syndicates down,
//    unrest up).
//  - Syndicates: on crime-ridden worlds an organisation takes the back streets (Org Syndicate, in league with the pirate
//    clan of its system if there is one) and takes protection money from the businesses there; a brave or honest head
//    who refuses may be marked for death.
//  - Smuggling: embargoes, wars and quarantines open gaps that smugglers run (the syndicates' money; a little relief for
//    the world), seized now and then.
//  - Prisons: states send the convicted (Politics' verdicts) to a penal colony on one of their small worlds; breakouts
//    happen, likelier where crime runs high.
//  - Assassinations: hot feuds, deep enmities and syndicates' grudges hire killers; a hit may be foiled; a killing may
//    bring a trial. The murdered are mirrored as deaths, so heirs inherit and successions follow.
//  - Fraud: greedy heads of losing businesses cook the books; exposed (on its own, or by Press' journalists), the firm
//    crashes, its world's investors lose money and its head goes on trial.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Business;
	class Citizen;
	class Civilization;
	class Faction;
	class Galaxy;
	class Household;
	class Org;
	class Settlement;
	class Society;

	/// <summary>A killing paid for (US-T282).</summary>
	class SPACEGODCORE_API Assassination final
	{
	public:
		int Id = 0;
		cs::Ref<Citizen> Target;
		cs::Ref<Household> Client;
		cs::Ref<Org> Killers;
		double DueJd = 0;
		bool Steered = false;
	};

	/// <summary>A penal colony (US-T282).</summary>
	class SPACEGODCORE_API Prison final
	{
	public:
		cs::Ref<Settlement> Where;
		cs::Ref<SpaceGod::Core::Faction> Faction;
		cs::List<cs::Ref<Citizen>> Inmates = cs::List<cs::Ref<Citizen>>();
		cs::List<double> ReleaseJd = cs::List<double>();
		int Breakouts = 0;
	};

	/// <summary>A business cooking its books (US-T282).</summary>
	class SPACEGODCORE_API Fraud final
	{
	public:
		cs::Ref<SpaceGod::Core::Business> Business;
		/// <summary>Credits of profit it has claimed and never made.</summary>
		double Inflated = 0;
		double SinceJd = 0;
	};

	class SPACEGODCORE_API Underworld final
	{
	public:
		cs::List<cs::Ref<Assassination>> Contracts = cs::List<cs::Ref<Assassination>>();
		cs::List<cs::Ref<Prison>> Prisons = cs::List<cs::Ref<Prison>>();
		cs::List<cs::Ref<Fraud>> Frauds = cs::List<cs::Ref<Fraud>>();

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		/// <summary>Marks someone for death (any module may: a syndicate's grudge, a silenced journalist).</summary>
		cs::Ref<Assassination> Mark(Civilization& civ, const cs::Ref<Citizen>& target, const cs::Ref<Household>& client, const cs::Ref<Org>& killers, double days, double jd, bool steered = false);
		/// <summary>Brings a fraud to light (Press' journalists call it); false if the business is not cooking its books.</summary>
		bool Expose(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Business>& b, double jd, bool steered = false);
		cs::Ref<Fraud> FraudOf(const cs::Ref<Business>& b) const;
		bool Jailed(const cs::Ref<Citizen>& c) const;

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Hit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& target, double jd);
		cs::String Racket(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd);
		cs::String Crackdown(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);

	private:
		int seenEvent = 0;

		void Crime(Civilization& civ, Galaxy& galaxy, double jd);
		void Syndicates(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Smuggling(Civilization& civ, Galaxy& galaxy, double jd);
		void Books(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Grudges(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Carry(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Assassination>& k, double jd);
		void Jails(Civilization& civ, Galaxy& galaxy, double jd);
		void Imprison(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& c, double jd);
		void React(Civilization& civ, Galaxy& galaxy, double jd);

		SG_SERIALIZABLE(Underworld);
	};
}
