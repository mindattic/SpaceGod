#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/TradeInsurance.cs (US-T19).

#include "Cs/Cs.h"

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class OwnedShip;
	class Property;
	class TradeDesk;
	class Holdings;

	/// <summary>An insurance policy on one of the house's hulls (and its cargo) or warehouses (and its stock).</summary>
	class SPACEGODCORE_API InsurancePolicy final
	{
	public:
		int Id = 0;

		/// <summary>What it covers: a hull, or a property (a warehouse).</summary>
		cs::Ref<OwnedShip> Ship;
		cs::Ref<::SpaceGod::Core::Property> Property;

		/// <summary>What it would pay out now, credits, and its premium a day (both reckoned daily).</summary>
		double Cover = 0, PremiumPerDay = 0;

		double SinceJd = 0;

		/// <summary>Premiums paid on it, and what it paid out.</summary>
		double PremiumsPaid = 0, Paid = 0;

		bool Active = true;
		double EndedJd = cs::Double::NaN;

		/// <summary>"claim paid", "cancelled", "lapsed: premium unpaid".</summary>
		cs::String EndedHow;

		cs::String Name() const;

		cs::String ToString() const { return cs::Cat("Policy on ", Name()); }
	};

	/// <summary>
	/// Underwriters (U.SPC-T426). The house can insure a hull (and whatever it carries) or a warehouse (and what is stored
	/// in it). A policy costs a premium a day: a share a year of what it covers (<see cref="TradeRules.InsuranceRate"/>),
	/// climbing with the risk where the hull trades or the warehouse stands (<see cref="TradeRules.RiskLoading"/>: raiders,
	/// a blockade, an embargo, a port near collapse, see <see cref="TradeDesk.Assess"/>), less for a trader of rank. Past
	/// <see cref="Uninsurable"/> risk nobody will write a new policy. A hull destroyed, taken or cast adrift, or a warehouse
	/// seized or lost with its port, is paid out at <see cref="TradeRules.InsuranceCover"/> of what it was worth. A policy
	/// whose premium the account cannot pay lapses.
	/// </summary>
	class SPACEGODCORE_API TradeInsurance final
	{
		cs::Ref<TradeDesk> desk;

	public:
		cs::List<cs::Ref<InsurancePolicy>> Policies = cs::List<cs::Ref<InsurancePolicy>>();

	private:
		int nextId = 1;

	public:
		/// <summary>The risk (0..1) past which underwriters will not write a new policy.</summary>
		static constexpr double Uninsurable = 0.85;

		/// <summary>The most ended policies kept for the record.</summary>
		static constexpr int HistoryCap = 50;

		TradeInsurance(cs::Ref<TradeDesk> desk) : desk(desk) {}

	private:
		cs::Ref<::SpaceGod::Core::Holdings> Holdings() const;

	public:
		/// <summary>The policy in force on <paramref name="o"/>, or null.</summary>
		cs::Ref<InsurancePolicy> Of(const cs::Ref<OwnedShip>& o) const;

		/// <summary>The policy in force on <paramref name="prop"/>, or null.</summary>
		cs::Ref<InsurancePolicy> Of(const cs::Ref<::SpaceGod::Core::Property>& prop) const;

		// ---------------------------------------------------------------- what is at risk

		/// <summary>What a hull is worth to an underwriter: its price, less damage, and its cargo at cost.</summary>
		static double ValueOf(const cs::Ref<OwnedShip>& o);

		/// <summary>What a warehouse is worth to an underwriter: what it cost, and its stock at base prices.</summary>
		static double ValueOf(const cs::Ref<::SpaceGod::Core::Property>& p);

		/// <summary>
		/// The risk a hull runs, 0..1: the worst of where it is, where it is bound, and every stop its orders take it to
		/// (a route's or patrol's stops, its claim, its charge's whereabouts); a free trader trades anywhere, so the
		/// system's average danger counts too.
		/// </summary>
		double Exposure(const cs::Ref<OwnedShip>& o) const;

		/// <summary>The risk a warehouse runs, 0..1: its port's.</summary>
		double Exposure(const cs::Ref<::SpaceGod::Core::Property>& p) const;

		/// <summary>The premium a day to cover <paramref name="cover"/> credits at <paramref name="risk"/>.</summary>
		double Premium(double cover, double risk) const;

		/// <summary>What a policy on <paramref name="o"/> would cover and cost a day now.</summary>
		std::tuple<double, double, double> Quote(const cs::Ref<OwnedShip>& o) const; // (cover, premium, risk)

		/// <summary>What a policy on <paramref name="p"/> would cover and cost a day now.</summary>
		std::tuple<double, double, double> Quote(const cs::Ref<::SpaceGod::Core::Property>& p) const; // (cover, premium, risk)

		// ---------------------------------------------------------------- policies

		/// <summary>Insures a hull (and its cargo); null if it is not the house's, already covered, or too great a risk to write.</summary>
		cs::Ref<InsurancePolicy> Insure(const cs::Ref<OwnedShip>& o, double jd);

		/// <summary>Insures a warehouse (and its stock) or a plant (US-T265); null if it is not a standing one of the house's, already
		/// covered, or too great a risk to write.</summary>
		cs::Ref<InsurancePolicy> Insure(const cs::Ref<::SpaceGod::Core::Property>& p, double jd);

	private:
		cs::Ref<InsurancePolicy> Write(const cs::Ref<InsurancePolicy>& p, double jd);

	public:
		/// <summary>Cancels a policy (no refund: premiums are paid day by day).</summary>
		bool Cancel(const cs::Ref<InsurancePolicy>& p, double jd);

		/// <summary>Once a day: every policy's cover and premium follow what it covers and the risk it runs, and the premium is paid (or it lapses).</summary>
		void Daily(double jd);

		/// <summary>A hull is lost: its policy pays out.</summary>
		void OnShipLost(cs::Ref<OwnedShip> o, double value, double jd); // internal

		/// <summary>A warehouse is seized or lost with its port: its policy pays out.</summary>
		void OnPropertyLost(cs::Ref<::SpaceGod::Core::Property> prop, double value, double jd); // internal

	private:
		void PayOut(const cs::Ref<InsurancePolicy>& p, double value, double jd);

		void Close(const cs::Ref<InsurancePolicy>& p, double jd, const cs::String& how);
	private:
		TradeInsurance() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(TradeInsurance); // ObjectGraph registration (Persistence/Registrations)
	};
}
