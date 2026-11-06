#pragma once
// US-T190: the real-time economy behind faction territory. Production and trade fill (or empty) the treasuries; this is
// where those treasuries move borders, live, every day: rich states pay colonists out to new ground, deeply indebted
// ones sell their smallest stations to a solvent state, and every change of who holds a system is logged with its date
// and sent out as news, so the galaxy map shows it (a pulsing ring, the "territory lately" list) only once word of it
// has come through the gates.
//
// Everything here is galaxy state changed only by the simulation, with no random draws: who buys a station is picked by
// standing (fleet and faction order breaking ties), and the daily review runs on a fixed grid of whole days, so every
// lockstep peer and every replay reaches the same borders. Nothing reads a distance, so it is the same at the gameplay
// and the realistic scale.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <memory>
#include <unordered_map>

namespace SpaceGod::Core
{
	class Economy;
	class Faction;
	class Galaxy;
	class Settlement;
	class Ship;
	class StarSystem;
	enum class Good : int;

	/// <summary>A system changing hands (US-T190): who held it, who holds it now, when, and how it came about.</summary>
	class SPACEGODCORE_API TerritoryChange final
	{
	public:
		cs::Ref<StarSystem> System;

		/// <summary>The old holder and the new; either null for nobody (a system first settled, or abandoned).</summary>
		cs::Ref<Faction> From, To;

		double Jd = 0;

		/// <summary>How it came about, a few words for the map ("taken in war", "bought", "first settled", ...).</summary>
		cs::String How;
	};

	/// <summary>One polity's strength as the daily review last measured it (US-T190), and its recent history.</summary>
	class SPACEGODCORE_API FactionStanding final
	{
	public:
		cs::Ref<SpaceGod::Core::Faction> Faction;

		/// <summary>Systems it holds, its people in every system, its treasury, at the last review.</summary>
		int Systems = 0;
		double Population = 0, Treasury = 0;

		/// <summary>The value of its ships' cargo lost on the way (robbed, see <see cref="Economy.LoseCargo"/>), fading with a
		/// half-life of <see cref="Territory.LossHalfLifeDays"/>.</summary>
		double CargoLost = 0;

		/// <summary>The one figure the map trends (see <see cref="Territory.StrengthOf"/>).</summary>
		double Strength = 0;

		/// <summary>Its strength at each daily review, oldest first, the last <see cref="Territory.HistoryDays"/>.</summary>
		cs::List<double> History;

		/// <summary>Since when it has been deeply in debt (<see cref="Territory.DebtLimit"/>); NaN while solvent.</summary>
		double InDebtSinceJd = cs::Double::NaN;

		/// <summary>When it last paid colonists out (see <see cref="Territory.RecruitEveryDays"/>); NaN for never.</summary>
		double RecruitedJd = cs::Double::NaN;

		/// <summary>Strength gained (or lost, negative) over the last <see cref="Territory.TrendDays"/> days.</summary>
		double Trend() const;
	};

	/// <summary>Who held a system at the last daily review (US-T190), to see it change.</summary>
	class SPACEGODCORE_API SystemHolder final
	{
	public:
		cs::Ref<StarSystem> System;
		cs::Ref<SpaceGod::Core::Faction> Holder;
	};

	/// <summary>
	/// Faction territory driven by the economy (US-T190), held on <see cref="Galaxy.Territory"/> and reviewed once a day from
	/// <see cref="Galaxy.Update"/>. Each review it:
	///  - measures every polity's strength (systems held, people, treasury, cargo lost to raiders) for the map's trend;
	///  - pays colonists out for a rich state (<see cref="RichAt"/>) whose people have no crowding of their own to send:
	///    a party is recruited at its capital system's biggest port for <see cref="RecruitCostPerPerson"/> each and handed to
	///    that system's own <see cref="Colonisation"/>, which sends its colony ship where it always would (at home or through
	///    the gates) and charges its own founding cost. No site worth going to, no money back needed: nothing is spent;
	///  - sells the smallest non-capital station of a state deep in debt (<see cref="DebtLimit"/> for <see cref="DebtDays"/>)
	///    to the richest solvent state at peace with it, preferring one already in that system, for what it is worth;
	///  - logs every change of who holds a system (<see cref="Changes"/>) and sends it out as news from that system, so the
	///    map learns of it only as the reports bring it.
	/// </summary>
	class SPACEGODCORE_API Territory final
	{
	public:
		/// <summary>Every polity's standing at the last review, in the polities' order.</summary>
		cs::List<cs::Ref<FactionStanding>> Standings;

		/// <summary>The systems that changed hands, oldest first, the last <see cref="MaxChanges"/>.</summary>
		cs::List<cs::Ref<TerritoryChange>> Changes;

		/// <summary>How many changes are kept.</summary>
		static constexpr int MaxChanges = 96;

		/// <summary>Daily strength samples kept, and how far back the trend looks, days.</summary>
		static constexpr int HistoryDays = 60;
		static constexpr int TrendDays = 30;

		/// <summary>A state this rich pays colonists out, at most once every <see cref="RecruitEveryDays"/> (credits).</summary>
		static constexpr double RichAt = 1.5e9;
		static constexpr double RecruitEveryDays = 60;

		/// <summary>What recruiting a colonist costs the state paying them out, credits (passage, kit and bounty; assumed).</summary>
		static constexpr double RecruitCostPerPerson = 2000;

		/// <summary>A treasury this far below zero is deep in debt; after <see cref="DebtDays"/> of it a station is sold.</summary>
		static constexpr double DebtLimit = 2e8;
		static constexpr double DebtDays = 90;

		/// <summary>What a station sells for, credits per resident, and at least <see cref="MinStationPrice"/>.</summary>
		static constexpr double StationPricePerPerson = 1500;
		static constexpr double MinStationPrice = 5e7;

		/// <summary>The half-life of the cargo losses a standing remembers, days.</summary>
		static constexpr double LossHalfLifeDays = 30;

		/// <summary>The map rings a system that changed hands this recently (by the news), days.</summary>
		static constexpr double RecentDays = 90;

		/// <summary>The galaxy's territory (made on first use: older saves have none).</summary>
		static cs::Ref<Territory> Of(Galaxy& galaxy);

		/// <summary>The standing of <paramref name="f"/> at the last review, or null.</summary>
		cs::Ref<FactionStanding> StandingOf(const cs::Ref<Faction>& f) const;

		/// <summary>The newest change in <paramref name="s"/> at or before <paramref name="asOfJd"/>, or null.</summary>
		cs::Ref<TerritoryChange> LastChange(const cs::Ref<StarSystem>& s, double asOfJd) const;

		/// <summary>One figure for a polity's strength: a point per million people, five per system held, one per hundred
		/// million credits banked (or owed), less one per ten million of cargo lately lost.</summary>
		static double StrengthOf(int systems, double population, double treasury, double cargoLost);

		/// <summary>Runs the daily review when its day has come (see the class summary).</summary>
		void Update(Galaxy& galaxy, double jd);

	private:
		/// <summary>Who held each system at the last review.</summary>
		cs::List<cs::Ref<SystemHolder>> holders;

		/// <summary>When the next review is due (NaN: on the first update, which only takes stock).</summary>
		double nextDailyJd = cs::Double::NaN;

		/// <summary>The economies whose lost cargo is counted (not saved: wired again on the first update after a load).</summary>
		std::unordered_map<const Economy*, std::weak_ptr<Economy>> wired;

		/// <summary>The stations sold at this review, so their system's change of hands reads "bought" (not saved: cleared each review).</summary>
		cs::List<cs::Ref<Settlement>> sold;

		void Wire(Galaxy& galaxy);
		void OnCargoLost(const cs::Ref<Ship>& ship, Good good, double tonnes);
		cs::Ref<FactionStanding> Standing(const cs::Ref<Faction>& f);

		void Measure(Galaxy& galaxy, double jd);
		void Recruit(Galaxy& galaxy, double jd);
		void Sell(Galaxy& galaxy, double jd);
		void Watch(Galaxy& galaxy, double jd, bool quiet);

		SG_SERIALIZABLE(Territory); // ObjectGraph registration (Territory.cpp)
	};
}
