// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/TradeDesk.cs (US-T19).

#include "Economy/TradeDesk.h"

#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Economy/Charters.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/MarketIntel.h"
#include "Economy/MarketShocks.h"
#include "Economy/Settlement.h"
#include "Economy/Smuggling.h"
#include "Economy/TradeInsurance.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	// ---------------------------------------------------------------- TradeRules

	cs::String TradeRules::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	void TradeRules::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		CharterHire();
		InsuranceRate();
		InsuranceCover();
		RiskLoading();
		IntelHalfLife();
		BrokerFee();
		CustomsChance();
		BackDoorPremium();
	}

	const cs::Ref<RangeSetting>& TradeRules::CharterHire()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.charterHire", SettingGroups::Economy,
			"Charter hire", 0.6, 0.2, 1.2, 0.05,
			"What a trading company offers a day to charter one of your hulls, as a share of what a hull of that class earns it trading.",
			&Percent, "charter hire freighter company trading");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::InsuranceRate()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.insuranceRate", SettingGroups::Economy,
			"Insurance premium", 0.05, 0, 0.25, 0.01,
			"What underwriters charge a year to cover a hull or a warehouse somewhere safe, as a share of what they cover (more where it is dangerous).",
			&Percent, "insurance premium underwriter hull cargo warehouse");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::InsuranceCover()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.insuranceCover", SettingGroups::Economy,
			"Insurance cover", 0.8, 0.2, 1, 0.05,
			"The share of a lost hull, cargo or warehouse an insurance policy pays out.",
			&Percent, "insurance cover claim payout");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::RiskLoading()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.riskLoading", SettingGroups::Economy,
			"Risk loading", 4, 0, 10, 0.5,
			"How steeply premiums climb with the danger where a hull trades or a warehouse stands: at full risk, this many times the base premium on top.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), "x"); }), "insurance risk piracy blockade collapse premium");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::IntelHalfLife()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.intelHalfLife", SettingGroups::Economy,
			"Price intelligence half-life", 8, 1, 60, 1,
			"Days after which a price you saw or bought is trusted only half as much when the trade desk ranks runs.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " d"); }), "intel prices stale age broker news");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::BrokerFee()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.brokerFee", SettingGroups::Economy,
			"Broker's fee", 40000, 0, 500000, 5000,
			"What a port's broker charges for a price sheet of every known market in the system (less for a trader of higher rank).",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "N0"), " cr"); }), "broker intel price report fee");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::CustomsChance()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.customs", SettingGroups::Economy,
			"Customs vigilance", 0.2, 0, 1, 0.05,
			"The chance a back-door sale of contraband at a lawful port is caught (a blockade's pickets catch more, a fence of stolen goods fewer).",
			&Percent, "smuggling customs contraband black market blockade catch");
		return value;
	}

	const cs::Ref<RangeSetting>& TradeRules::BackDoorPremium()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("trade.backDoorPremium", SettingGroups::Economy,
			"Smuggling premium", 1.5, 1, 4, 0.1,
			"How much over the port's own price its back door pays for contraband, a blockade run or an embargo broken.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.0"), "x"); }), "smuggling black market premium blockade embargo");
		return value;
	}

	// ---------------------------------------------------------------- PortRisk

	PortRisk::PortRisk(double piracy, double collapse, cs::Ref<Faction> blockadedBy, bool embargoed)
	{
		Piracy = Clamp01(piracy);
		Collapse = Clamp01(collapse);
		BlockadedBy = blockadedBy;
		Embargoed = embargoed;
		double t = 1 - (1 - Piracy) * (1 - 0.6 * Collapse);
		if (blockadedBy != nullptr || embargoed) t = cs::Math::Max(t, 0.5);
		Total = Clamp01(t);
	}

	cs::String PortRisk::Summary() const
	{
		cs::List<cs::String> parts;
		if (BlockadedBy != nullptr) parts.Add(cs::Cat("blockaded by the ", BlockadedBy->Name));
		if (Embargoed) parts.Add("embargoed");
		if (Piracy >= 0.5) parts.Add("raiders active");
		else if (Piracy >= 0.15) parts.Add("raids lately");
		if (Collapse >= 0.99) parts.Add("fallen");
		else if (Collapse >= 0.5) parts.Add("near collapse");
		else if (Collapse >= 0.25) parts.Add("unsettled");
		return parts.Count() == 0 ? cs::String("quiet") : cs::Strings::Join(", ", parts);
	}

	double PortRisk::Clamp01(double v) { return cs::Double::IsNaN(v) ? 0 : cs::Math::Max(0.0, cs::Math::Min(1.0, v)); }

	const char* ToString(TraderRank v)
	{
		switch (v)
		{
			case TraderRank::Peddler: return "Peddler";
			case TraderRank::Hauler: return "Hauler";
			case TraderRank::Merchant: return "Merchant";
			case TraderRank::Shipowner: return "Shipowner";
			case TraderRank::Magnate: return "Magnate";
			case TraderRank::MerchantPrince: return "MerchantPrince";
		}
		return "";
	}

	// ---------------------------------------------------------------- TradeDesk

	const cs::List<double>& TradeDesk::RankWorth()
	{
		static const cs::List<double> value = { 0, 2e7, 1e8, 4e8, 1.5e9, 5e9 };
		return value;
	}

	cs::List<cs::Func<cs::List<cs::Ref<TradeTip>>, cs::Ref<TradeDesk>, double>>& TradeDesk::TipSources()
	{
		static cs::List<cs::Func<cs::List<cs::Ref<TradeTip>>, cs::Ref<TradeDesk>, double>> value =
		{
			&TradeDesk::ShockTips,
			&TradeDesk::ShortageTips,
			&TradeDesk::RiskTips,
		};
		return value;
	}

	TradeDesk::TradeDesk(cs::Ref<::SpaceGod::Core::World> world, int seed)
		: world(world), seed(seed)
	{
	}

	void TradeDesk::Construct()
	{
		auto self = shared_from_this();
		Intel = cs::New<MarketIntel>(self);
		Charters = cs::New<::SpaceGod::Core::Charters>(self, seed + 1);
		Insurance = cs::New<TradeInsurance>(self);
		Smuggling = cs::New<::SpaceGod::Core::Smuggling>(self, seed + 2);
		auto insurance = Insurance;
		world->Holdings->ShipLost += [insurance](cs::Ref<OwnedShip> o, double value, double jd) { insurance->OnShipLost(o, value, jd); };
		world->Holdings->PropertyLost += [insurance](cs::Ref<Property> prop, double value, double jd) { insurance->OnPropertyLost(prop, value, jd); };
	}

	cs::Ref<::SpaceGod::Core::Holdings> TradeDesk::Holdings() const { return world->Holdings; }

	cs::Ref<Faction> TradeDesk::Flag() const { return world->Holdings->House; }

	// ---------------------------------------------------------------- the clock

	void TradeDesk::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDailyJd))
		{
			nextDailyJd = jd + 1;
			Charters->Post(jd);
			return;
		}
		for (int guard = 0; guard < 4000 && jd >= nextDailyJd; guard++)
		{
			double day = nextDailyJd;
			Intel->Daily(day);
			Charters->Daily(day);
			Insurance->Daily(day);
			Reckon(day);
			nextDailyJd += 1;
		}
	}

	// ---------------------------------------------------------------- risk

	PortRisk TradeDesk::Assess(const cs::Ref<Settlement>& s, cs::Ref<Faction> flag) const
	{
		if (s == nullptr) return PortRisk();
		if (!flag) flag = Flag();
		double piracy = world->Contracts != nullptr ? world->Contracts->DangerAt(s->Body) : 0;
		bool embargoed = s->Owner != nullptr && flag != nullptr && world->Diplomacy()->IsEmbargoed(s->Owner, flag);
		return PortRisk(piracy, CollapseOf(s), world->Diplomacy()->BlockadedBy(s), embargoed);
	}

	double TradeDesk::RiskAt(int body) const
	{
		double worst = -1;
		for (const auto& s : world->Economy->At(body))
			if (s->Discovered()) worst = cs::Math::Max(worst, Assess(s).Total);
		if (worst >= 0) return worst;
		return world->Contracts != nullptr ? cs::Math::Max(0.0, cs::Math::Min(1.0, world->Contracts->DangerAt(body))) : 0;
	}

	double TradeDesk::CollapseOf(const cs::Ref<Settlement>& s)
	{
		if (s == nullptr) return 0;
		if (s->Fallen) return 1;
		double unrest = cs::Math::Max(0.0, cs::Math::Min(1.0, s->Unrest));
		double living = cs::Math::Min(s->Wellbeing, 0.5 + 0.5 * s->HealthWellbeing);
		double want = cs::Math::Max(0.0, cs::Math::Min(1.0, (0.85 - living) / 0.85));
		double shrink = s->InitialPopulation > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, (0.9 - s->Population / s->InitialPopulation) / 0.6)) : 0;
		return cs::Math::Max(0.0, cs::Math::Min(1.0, 0.45 * unrest + 0.6 * want + 0.3 * shrink));
	}

	// ---------------------------------------------------------------- the career

	double TradeDesk::NetWorth(const cs::Ref<Faction>& purse) const
	{
		auto h = Holdings();
		double v = h->Balance() + h->CargoAfloat();
		for (const auto& o : h->Ships)
		{
			if (o->Lost) continue;
			v += Companies::ResaleShare * Economy::HullPrice(o->Ship->Spec->Class) * cs::Math::Max(0.1, 1 - (o->Ship->Combat ? o->Ship->Combat->Damage() : 0));
		}
		for (const auto& p : h->Properties)
		{
			if (p->Lost) continue;
			switch (p->Kind)
			{
				case PropertyKind::Warehouse:
					v += p->PricePaid;
					for (int g = 0; g < GoodInfo::Count; g++) v += p->Stock[g] * GoodInfo::Of(static_cast<Good>(g))->BasePrice;
					break;
				case PropertyKind::StationShare:
					v += ::SpaceGod::Core::Holdings::SharePrice(p->Settlement, p->Share);
					break;
				default:
					v += 0.5 * p->PricePaid;
					break;
			}
		}
		if (purse != nullptr && purse != h->House) v += purse->Treasury;
		return v;
	}

	TraderRank TradeDesk::RankFor(double worth)
	{
		auto r = TraderRank::Peddler;
		for (int i = 0; i < RankWorth().Count(); i++)
			if (worth >= RankWorth()[i]) r = static_cast<TraderRank>(i);
		return r;
	}

	cs::String TradeDesk::RankName(TraderRank r)
	{
		switch (r)
		{
			case TraderRank::Hauler: return "Hauler";
			case TraderRank::Merchant: return "Merchant";
			case TraderRank::Shipowner: return "Shipowner";
			case TraderRank::Magnate: return "Shipping magnate";
			case TraderRank::MerchantPrince: return "Merchant prince";
			default: return "Peddler";
		}
	}

	double TradeDesk::NextRankWorth() const { return static_cast<int>(Rank) + 1 < RankWorth().Count() ? RankWorth()[static_cast<int>(Rank) + 1] : cs::Double::NaN; }

	double TradeDesk::BrokerDiscount() const { return cs::Math::Min(0.5, 0.1 * static_cast<int>(Rank)); }

	double TradeDesk::InsuranceDiscount() const { return cs::Math::Min(0.25, 0.05 * static_cast<int>(Rank)); }

	void TradeDesk::Reckon(double jd)
	{
		auto r = RankFor(NetWorth(world->PilotedShip != nullptr && world->PilotedShip->Faction != Flag() ? world->PilotedShip->Faction : nullptr));
		Rank = r;
		if (r <= BestRank) return;
		BestRank = r;
		cs::String text = cs::Cat("Your house is now counted a ", cs::Strings::ToLowerInvariant(RankName(r)), "'s");
		Notice(text);
		world->Log(jd, text, WorldEventKind::Economy);
	}

	// ---------------------------------------------------------------- trade news

	cs::List<cs::Ref<TradeTip>> TradeDesk::Tips(double jd, int max)
	{
		cs::List<cs::Ref<TradeTip>> list;
		auto self = shared_from_this();
		for (const auto& source : TipSources())
		{
			auto tips = source(self, jd);
			list.AddRange(tips); // (C# skips a null sequence; an empty list adds nothing)
		}
		list.Sort([](const cs::Ref<TradeTip>& a, const cs::Ref<TradeTip>& b) { return cs::Compare(b->Weight, a->Weight); });
		if (list.Count() > max) list.RemoveRange(max, list.Count() - max);
		return list;
	}

	cs::List<cs::Ref<TradeTip>> TradeDesk::ShockTips(const cs::Ref<TradeDesk>& desk, double /*jd*/)
	{
		cs::List<cs::Ref<TradeTip>> result;
		for (const auto& e : desk->world->Shocks->Active)
		{
			if (e->Settlement != nullptr && !e->Settlement->Discovered()) continue;
			cs::String where = e->Settlement != nullptr ? e->Settlement->Name : e->Faction != nullptr ? cs::Cat("the ", e->Faction->Name) : cs::String("the system");
			// (Empty for C#'s null.)
			cs::String good = e->Good.has_value() ? cs::Strings::ToLowerInvariant(GoodInfo::Of(*e->Good)->Name) : cs::String();
			cs::String text;
			double weight = 2;
			switch (e->Kind)
			{
				case ShockKind::MineAccident:
				case ShockKind::Depletion:
					text = cs::Cat(e->Label, " at ", where, ": ", (!good.empty() ? good : cs::String("ore")), " will run short there");
					break;
				case ShockKind::Blight:
					text = cs::Cat("Blight at ", where, ": food will be dear there");
					weight = 3;
					break;
				case ShockKind::Strike:
					text = cs::Cat("Strike at ", where, ": its industry is idle, its goods will run short");
					break;
				case ShockKind::Rationing:
					text = cs::Cat(where, " rations its people: essentials fetch a premium in its ports");
					weight = 3;
					break;
				case ShockKind::Blockade:
					text = cs::Cat(where, " is blockaded", (e->Faction != nullptr ? cs::Cat(" by the ", e->Faction->Name) : cs::String()), ": closed to trade, but its back door pays well for a run");
					weight = 4;
					break;
				case ShockKind::Embargo:
					text = cs::Cat(e->Label, " declared by ", where);
					weight = 3;
					break;
				default:
					text = cs::Cat(e->Label, " at ", where);
					break;
			}
			auto tip = cs::New<TradeTip>();
			tip->Jd = e->StartJd; tip->Text = text; tip->Market = e->Settlement; tip->Good = e->Good; tip->Weight = weight;
			result.Add(tip);
		}
		return result;
	}

	cs::List<cs::Ref<TradeTip>> TradeDesk::ShortageTips(const cs::Ref<TradeDesk>& desk, double jd)
	{
		cs::List<cs::Ref<TradeTip>> result;
		for (const auto& sheet : desk->Intel->Sheets())
		{
			if (sheet->Market->Fallen) continue;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				if (!sheet->Buys[static_cast<size_t>(g)]) continue;
				double ratio = sheet->Price[g] / GoodInfo::Of(static_cast<Good>(g))->BasePrice;
				if (ratio < 2) continue;
				double age = sheet->AgeDays(jd);
				auto tip = cs::New<TradeTip>();
				tip->Jd = sheet->Jd; tip->Market = sheet->Market; tip->Good = static_cast<Good>(g);
				tip->Text = cs::Cat(sheet->Market->Name, " pays ", cs::Fmt(ratio, "0.0"), "x for ", cs::Strings::ToLowerInvariant(GoodInfo::Of(static_cast<Good>(g))->Name), " (", MarketIntel::AgeText(age), ")");
				tip->Weight = 1 + ratio * desk->Intel->Confidence(sheet, jd);
				result.Add(tip);
			}
		}
		return result;
	}

	cs::List<cs::Ref<TradeTip>> TradeDesk::RiskTips(const cs::Ref<TradeDesk>& desk, double jd)
	{
		cs::List<cs::Ref<TradeTip>> result;
		for (const auto& p : desk->Holdings()->Properties)
		{
			if (p->Lost || p->Settlement == nullptr) continue;
			auto risk = desk->Assess(p->Settlement);
			if (risk.Total < 0.35) continue;
			bool insured = desk->Insurance->Of(p) != nullptr;
			auto tip = cs::New<TradeTip>();
			tip->Jd = jd; tip->Market = p->Settlement;
			tip->Text = cs::Cat("Your ", p->Name, ": ", risk.Summary(), (insured ? "" : ", uninsured"));
			tip->Weight = 3 + 3 * risk.Total;
			result.Add(tip);
		}
		return result;
	}

	// ---------------------------------------------------------------- messages

	void TradeDesk::Notice(const cs::String& text)
	{
		notices.Add(text);
		if (notices.Count() > 20) notices.RemoveAt(0);
	}

	cs::List<cs::String> TradeDesk::TakeNotices()
	{
		cs::List<cs::String> list = notices;
		notices.Clear();
		return list;
	}

	cs::String TradeDesk::Cr(double v) { return cs::Cat(cs::Fmt(v, "N0"), " cr"); }
}
