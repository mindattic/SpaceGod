#pragma once
// US-T283: Commerce, the money side of the Civilization layer (Civilization.h), on top of Society's businesses.
//  - Unions: where wages lag the cost of living, the workers organise (Org Union); they win pay rises or call strikes
//    (Strife::CallStrike), the more the wider the gap.
//  - Refugees: war, famine and plague drive people off a world to the best one their flag holds (populations move, the
//    receiving world grows restless).
//  - Shares: rich, greedy or ambitious houses buy stakes in other businesses on their worlds, take dividends, and past
//    half own them (a hostile takeover, which Kinship may answer with a feud). A collapsing firm's shares crash.
//  - Banks: each big world keeps a bank (Org Bank) that takes the rich houses' deposits and lends to struggling firms;
//    defaults drain it, and a bank that runs dry fails, taking its depositors' money and the world's calm with it.
//  - Insurance: piracy around a system raises shipping premiums (its shippers pay).
//  - Ad wars: rival heads of firms in one trade fight with commercials; the abler wins custom.
//  - Tourism: some great worlds become famous for a landmark; their hotels prosper and advertise.
//  - Patents: able heads patent inventions (their firms and their states' research gain); greedy rivals steal them.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Business;
	class Civilization;
	class Galaxy;
	class Household;
	class Org;
	class Settlement;
	class Society;

	/// <summary>A house's share of a business (US-T283).</summary>
	class SPACEGODCORE_API Stake final
	{
	public:
		cs::Ref<Household> Holder;
		cs::Ref<SpaceGod::Core::Business> Business;
		double Share = 0;
	};

	/// <summary>Money a bank lent a business (US-T283).</summary>
	class SPACEGODCORE_API Loan final
	{
	public:
		cs::Ref<Org> Bank;
		cs::Ref<SpaceGod::Core::Business> Business;
		double Principal = 0, DueJd = 0;
	};

	class SPACEGODCORE_API Commerce final
	{
	public:
		/// <summary>A month's interest on a loan.</summary>
		static constexpr double MonthlyRate = 0.01;

		cs::List<cs::Ref<Stake>> Stakes = cs::List<cs::Ref<Stake>>();
		cs::List<cs::Ref<Loan>> Loans = cs::List<cs::Ref<Loan>>();
		int Patents = 0;

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		double ShareOf(const cs::Ref<Household>& h, const cs::Ref<Business>& b) const;

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Takeover(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& raider, const cs::Ref<Business>& b, double jd);
		cs::String Run(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& bank, double jd);
		cs::String Landmark(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);

	private:
		int seenEvent = 0;

		void Unions(Civilization& civ, Galaxy& galaxy, double jd);
		void Refugees(Civilization& civ, Galaxy& galaxy, double jd);
		void Shares(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Seize(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& raider, const cs::Ref<Business>& b, double jd, bool steered);
		void Banks(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Fail(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& bank, double jd, bool steered);
		void Insurance(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Adverts(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Tourism(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Inventions(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Crash(Civilization& civ, Galaxy& galaxy, const cs::Ref<Business>& b, double jd);
		void React(Civilization& civ, Galaxy& galaxy, double jd);

		SG_SERIALIZABLE(Commerce);
	};
}
