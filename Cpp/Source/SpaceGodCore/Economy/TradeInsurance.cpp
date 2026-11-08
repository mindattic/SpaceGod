// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/TradeInsurance.cs (US-T19).

#include "Economy/TradeInsurance.h"

#include "Combat/CombatState.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	cs::String InsurancePolicy::Name() const
	{
		return Ship != nullptr ? cs::Cat("the ", Ship->Name()) : Property != nullptr ? cs::Cat("the ", Property->Name) : cs::String("?");
	}

	cs::Ref<::SpaceGod::Core::Holdings> TradeInsurance::Holdings() const { return desk->Holdings(); }

	cs::Ref<InsurancePolicy> TradeInsurance::Of(const cs::Ref<OwnedShip>& o) const
	{
		for (const auto& p : Policies)
			if (p->Active && p->Ship == o) return p;
		return nullptr;
	}

	cs::Ref<InsurancePolicy> TradeInsurance::Of(const cs::Ref<::SpaceGod::Core::Property>& prop) const
	{
		for (const auto& p : Policies)
			if (p->Active && p->Property == prop) return p;
		return nullptr;
	}

	// ---------------------------------------------------------------- what is at risk

	double TradeInsurance::ValueOf(const cs::Ref<OwnedShip>& o)
	{
		auto s = o->Ship;
		return Economy::HullPrice(s->Spec->Class) * cs::Math::Max(0.1, 1 - (s->Combat ? s->Combat->Damage() : 0)) + (s->HasCargo ? s->CargoCost : 0);
	}

	double TradeInsurance::ValueOf(const cs::Ref<::SpaceGod::Core::Property>& p)
	{
		double v = p->PricePaid;
		for (int g = 0; g < GoodInfo::Count; g++) v += p->Stock[g] * GoodInfo::Of(static_cast<Good>(g))->BasePrice;
		return v;
	}

	double TradeInsurance::Exposure(const cs::Ref<OwnedShip>& o) const
	{
		auto ship = o->Ship;
		double risk = 0;
		auto At = [&](int body)
		{
			if (body >= 0) risk = cs::Math::Max(risk, desk->RiskAt(body));
		};
		At(ship->Orbit ? ship->Orbit->Body : -1);
		At(ship->DestinationBody);
		if (ship->CargoDestination != nullptr) risk = cs::Math::Max(risk, desk->Assess(ship->CargoDestination).Total);
		auto orders = o->Orders;
		// (Stops: C# null is an empty list here.)
		for (int b : orders->Stops) At(b);
		if (orders->Claim != nullptr) At(orders->Claim->Body);
		if (orders->Charge != nullptr) At(orders->Charge->Orbit ? orders->Charge->Orbit->Body : -1);
		if (orders->Kind == StandingOrderKind::FreeTrade)
		{
			double sum = 0;
			int n = 0;
			for (const auto& s : desk->World()->Economy->Settlements)
			{
				if (!s->Discovered() || s->Kind == SettlementKind::Haven) continue;
				sum += desk->Assess(s).Total;
				n++;
			}
			if (n > 0) risk = cs::Math::Max(risk, sum / n);
		}
		return risk;
	}

	double TradeInsurance::Exposure(const cs::Ref<::SpaceGod::Core::Property>& p) const
	{
		return p->Settlement != nullptr ? desk->Assess(p->Settlement).Total : desk->RiskAt(p->Body);
	}

	double TradeInsurance::Premium(double cover, double risk) const
	{
		return cover * TradeRules::InsuranceRate()->Value / 365.25 * (1 + TradeRules::RiskLoading()->Value * cs::Math::Max(0.0, cs::Math::Min(1.0, risk))) * (1 - desk->InsuranceDiscount());
	}

	std::tuple<double, double, double> TradeInsurance::Quote(const cs::Ref<OwnedShip>& o) const
	{
		double cover = ValueOf(o) * TradeRules::InsuranceCover()->Value, risk = Exposure(o);
		return { cover, Premium(cover, risk), risk };
	}

	std::tuple<double, double, double> TradeInsurance::Quote(const cs::Ref<::SpaceGod::Core::Property>& p) const
	{
		double cover = ValueOf(p) * TradeRules::InsuranceCover()->Value, risk = Exposure(p);
		return { cover, Premium(cover, risk), risk };
	}

	// ---------------------------------------------------------------- policies

	cs::Ref<InsurancePolicy> TradeInsurance::Insure(const cs::Ref<OwnedShip>& o, double jd)
	{
		if (o == nullptr || o->Lost || !Holdings()->Owns(o->Ship) || Of(o) != nullptr) return nullptr;
		auto [cover, premium, risk] = Quote(o);
		if (risk >= Uninsurable) return nullptr;
		auto p = cs::New<InsurancePolicy>();
		p->Ship = o;
		p->Cover = cover;
		p->PremiumPerDay = premium;
		return Write(p, jd);
	}

	cs::Ref<InsurancePolicy> TradeInsurance::Insure(const cs::Ref<::SpaceGod::Core::Property>& p, double jd)
	{
		if (p == nullptr || p->Lost || (p->Kind != PropertyKind::Warehouse && p->Kind != PropertyKind::Plant) || !Holdings()->Properties.Contains(p) || Of(p) != nullptr) return nullptr;
		auto [cover, premium, risk] = Quote(p);
		if (risk >= Uninsurable) return nullptr;
		auto policy = cs::New<InsurancePolicy>();
		policy->Property = p;
		policy->Cover = cover;
		policy->PremiumPerDay = premium;
		return Write(policy, jd);
	}

	cs::Ref<InsurancePolicy> TradeInsurance::Write(const cs::Ref<InsurancePolicy>& p, double jd)
	{
		p->Id = nextId++;
		p->SinceJd = jd;
		Policies.Add(p);
		Holdings()->Transact(0, cs::Cat("Insured ", p->Name()), jd);
		return p;
	}

	bool TradeInsurance::Cancel(const cs::Ref<InsurancePolicy>& p, double jd)
	{
		if (p == nullptr || !p->Active) return false;
		Close(p, jd, "cancelled");
		return true;
	}

	void TradeInsurance::Daily(double jd)
	{
		for (const auto& p : cs::List<cs::Ref<InsurancePolicy>>(Policies))
		{
			if (!p->Active) continue;
			// What it covers is already gone (and paid out, or not covered): nothing more to insure.
			if ((p->Ship != nullptr && p->Ship->Lost) || (p->Property != nullptr && p->Property->Lost))
			{
				Close(p, jd, "nothing left to cover");
				continue;
			}
			auto [cover, premium, unused] = p->Ship != nullptr ? Quote(p->Ship) : Quote(p->Property);
			(void)unused;
			p->Cover = cover;
			p->PremiumPerDay = premium;
			if (!Holdings()->Transact(-premium, cs::Cat("Insurance on ", p->Name()), jd))
			{
				Close(p, jd, "lapsed: premium unpaid");
				desk->Notice(cs::Cat("Your policy on ", p->Name(), " lapses: the account cannot pay its premium"));
				continue;
			}
			p->PremiumsPaid += premium;
			desk->PremiumsPaid += premium;
		}
	}

	void TradeInsurance::OnShipLost(cs::Ref<OwnedShip> o, double value, double jd)
	{
		auto p = Of(o);
		if (p == nullptr) return;
		PayOut(p, value, jd);
	}

	void TradeInsurance::OnPropertyLost(cs::Ref<::SpaceGod::Core::Property> prop, double value, double jd)
	{
		auto p = Of(prop);
		if (p == nullptr) return;
		PayOut(p, value, jd);
	}

	void TradeInsurance::PayOut(const cs::Ref<InsurancePolicy>& p, double value, double jd)
	{
		double paid = cs::Math::Max(0.0, value) * TradeRules::InsuranceCover()->Value;
		p->Paid = paid;
		desk->ClaimsPaid += paid;
		Holdings()->Transact(paid, cs::Cat("Insurance pays out on ", p->Name()), jd);
		Close(p, jd, "claim paid");
		desk->Notice(cs::Cat("Underwriters pay ", TradeDesk::Cr(paid), " for ", p->Name()));
	}

	void TradeInsurance::Close(const cs::Ref<InsurancePolicy>& p, double jd, const cs::String& how)
	{
		p->Active = false;
		p->EndedJd = jd;
		p->EndedHow = how;
		int ended = 0;
		for (const auto& x : Policies)
			if (!x->Active) ended++;
		for (int i = 0; i < Policies.Count() && ended > HistoryCap; i++)
			if (!Policies[i]->Active)
			{
				Policies.RemoveAt(i--);
				ended--;
			}
	}
}
