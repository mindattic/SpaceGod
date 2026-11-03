#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Charters.cs (US-T19).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Company;
	class Settlement;
	class OwnedShip;
	class StandingOrder;
	class TradeDesk;
	class World;
	class Holdings;
	enum class ShipClass : int;

	/// <summary>A trading company's offer to charter a hull: the class it wants, for how long, at what hire a day.</summary>
	class CharterOffer final
	{
	public:
		int Id = 0;
		cs::Ref<Company> Charterer;
		ShipClass Class = static_cast<ShipClass>(0);

		/// <summary>The hire a day, credits, and the term, days.</summary>
		double RatePerDay = 0, Days = 0;

		double PostedJd = 0, ExpiresJd = 0;

		/// <summary>The charterer's home port, where the offer is posted.</summary>
		cs::Ref<Settlement> Port;

		/// <summary>What the whole term pays.</summary>
		double Total() const { return RatePerDay * Days; }

		cs::String Title() const;

		cs::String ToString() const { return Title(); }
	};

	/// <summary>One of the house's hulls out on a time charter: whose, for how long, and what it has paid and made.</summary>
	class Charter final
	{
	public:
		cs::Ref<OwnedShip> Ship;
		cs::Ref<Company> Charterer;
		double RatePerDay = 0, StartJd = 0, EndJd = 0;

		/// <summary>Hire the house has been paid, and the trade result the charterer has had of the hull (its gain or loss).</summary>
		double HirePaid = 0, TradeResult = 0;

		/// <summary>Hire is paid for the time served only: this is the moment it has been paid up to.</summary>
		double PaidThroughJd = 0;

		bool Active = true;

		/// <summary>How it ended ("term served", "recalled", "the hull was lost", "the charterer defaulted"), when, and
		/// whether it ran its term.</summary>
		cs::String EndedHow;
		double EndedJd = cs::Double::NaN;
		bool Served = false;

		/// <summary>The orders the hull had before it went out on charter, given back when it returns.</summary>
		cs::Ref<StandingOrder> PreviousOrders;

		double DaysLeft(double jd) const { return cs::Math::Max(0.0, EndJd - jd); }

		cs::String ToString() const;
	};

	/// <summary>
	/// Time charters (U.SPC-T426): the trading companies hire hulls they cannot build fast enough. A company whose fleet
	/// earns well posts an offer for a class at a day rate (a share of what such a hull earns it trading,
	/// <see cref="TradeRules.CharterHire"/>, a little more for a trader of rank) for a term of weeks to months. The
	/// player charters a crewed hull of that class out to it: for the term the hull trades for the charterer, which keeps
	/// whatever it makes or loses (propellant and tolls included) and pays the house its hire every day; the house still
	/// pays the crew and captain. At the end of the term the hull goes back to its old orders. The house may recall it
	/// early for <see cref="RecallPenaltyDays"/> days' hire; a charterer that cannot pay (or goes under) ends the charter
	/// itself, and a hull lost on charter is the house's loss (see <see cref="TradeInsurance"/>).
	/// </summary>
	class Charters final
	{
		cs::Ref<TradeDesk> desk;
		cs::Random rng;

	public:
		cs::List<cs::Ref<CharterOffer>> Offers = cs::List<cs::Ref<CharterOffer>>();

		/// <summary>Every charter taken, running or ended (the latest <see cref="HistoryCap"/> ended ones kept).</summary>
		cs::List<cs::Ref<Charter>> All = cs::List<cs::Ref<Charter>>();

	private:
		int nextId = 1;
		double nextPostJd = cs::Double::NaN;

	public:
		/// <summary>How often companies post, days; how long an offer stays up; the most offers up at once.</summary>
		static constexpr double PostEveryDays = 5, OfferDays = 15;
		static constexpr int MaxOffers = 8, HistoryCap = 50;

		/// <summary>The shortest and longest term asked, days.</summary>
		static constexpr double MinDays = 30, MaxDays = 120;

		/// <summary>Days' hire the house pays to take a hull back early.</summary>
		static constexpr double RecallPenaltyDays = 10;

		/// <summary>The hulls a company charters: the merchant classes.</summary>
		static const cs::List<ShipClass>& Classes();

		Charters(cs::Ref<TradeDesk> desk, int seed);

	private:
		cs::Ref<::SpaceGod::Core::World> World() const;
		cs::Ref<::SpaceGod::Core::Holdings> Holdings() const;

	public:
		/// <summary>The charter <paramref name="o"/> is out on, or null.</summary>
		cs::Ref<Charter> Of(const cs::Ref<OwnedShip>& o) const;

		// IEnumerable + yield: built eagerly (see docs/MIGRATION.md).
		cs::List<cs::Ref<Charter>> Active() const;

		/// <summary>
		/// What <paramref name="c"/> would pay a day for a hull of <paramref name="cls"/>: the charter share of what such a
		/// hull earns it (its own hulls of the class, else the trade's, else twice the hull's running cost), never less
		/// than its running cost and a fifth again, and a twentieth more a rank of the trader's.
		/// </summary>
		double RateFor(const cs::Ref<Company>& c, ShipClass cls) const;

		/// <summary>Takes down lapsed offers and has the companies that can afford a hull's hire post new ones.</summary>
		void Post(double jd);

		enum class TakeOutcome : int
		{
			Ok,

			/// <summary>The offer is gone (taken, lapsed, or its company gone under).</summary>
			Gone,

			/// <summary>Not a house hull, lost, or flown by the player.</summary>
			NotYours,

			/// <summary>Not the class the company asked for.</summary>
			WrongClass,

			/// <summary>No captain: a hull is chartered with its crew.</summary>
			NoCaptain,

			/// <summary>Already out on a charter.</summary>
			Chartered,
		};
		friend const char* ToString(TakeOutcome v);

		/// <summary>Whether <paramref name="ship"/> can go out on <paramref name="offer"/>.</summary>
		TakeOutcome CanTake(const cs::Ref<CharterOffer>& offer, const cs::Ref<OwnedShip>& ship, double jd) const;

		/// <summary>The first of the house's hulls that could go out on <paramref name="offer"/>, or null.</summary>
		cs::Ref<OwnedShip> Candidate(const cs::Ref<CharterOffer>& offer, double jd) const;

		/// <summary>
		/// Charters <paramref name="ship"/> out on <paramref name="offer"/>: from now it free trades for the charterer, its
		/// old orders kept for its return. Returns the charter, or null (see <see cref="CanTake"/> for why).
		/// </summary>
		cs::Ref<Charter> Take(const cs::Ref<CharterOffer>& offer, const cs::Ref<OwnedShip>& ship, double jd);

		/// <summary>Takes a hull back before its term is out, paying the charterer <see cref="RecallPenaltyDays"/> days' hire.
		/// False if it is not on charter or the account cannot pay.</summary>
		bool Recall(const cs::Ref<Charter>& c, double jd);

		/// <summary>Once a day: every charter pays its hire, or ends (its term out, its hull lost, its charterer unable to pay).</summary>
		void Daily(double jd);

	private:
		void End(const cs::Ref<Charter>& c, double jd, const cs::String& how, bool served);
	};

	const char* ToString(Charters::TakeOutcome v);
}
