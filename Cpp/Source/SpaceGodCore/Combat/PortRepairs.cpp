// US-T266: the port's Repair & Rearm service (see Combat/PortRepairs.h).

#include "Combat/PortRepairs.h"

#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>The yard's labour on a repair, as a share of the parts' base value (as a refit's, Outfitting::LabourShare).</summary>
		constexpr double LabourShare = 0.25;

		constexpr int AlloysI = static_cast<int>(Good::Alloys);
		constexpr int ElectronicsI = static_cast<int>(Good::Electronics);
		constexpr int MunitionsI = static_cast<int>(Good::Munitions);

		/// <summary>"LaserTurret" as "laser turret".</summary>
		cs::String Spaced(const char* name)
		{
			cs::String out;
			for (const char* c = name; *c; ++c)
			{
				if (*c >= 'A' && *c <= 'Z')
				{
					if (!out.empty()) out += ' ';
					out += static_cast<char>(*c - 'A' + 'a');
				}
				else out += *c;
			}
			return out;
		}

		cs::String Pct(double f) { return cs::Cat(cs::Fmt(cs::Math::Max(0.0, f) * 100, "0"), "%"); }

		double Shelf(const cs::Ref<Settlement>& port, int g, double need)
		{
			return need > 1e-9 ? cs::Math::Min(1.0, port->Stock[g] / need) : 1.0;
		}
	}

	bool PortRepairs::DoesStructure(const cs::Ref<Settlement>& yard)
	{
		return yard != nullptr && !yard->Fallen && (yard->HasShipyard() || yard->Kind == SettlementKind::Haven);
	}

	double PortRepairs::MaterialsCost(const cs::Ref<Settlement>& port, double alloys, double electronics, double munitions, double priceFactor)
	{
		double parts = 0, labour = 0;
		if (alloys > 0)
		{
			parts += port->PurchaseCost(Good::Alloys, alloys);
			labour += alloys * GoodInfo::Of(Good::Alloys)->BasePrice;
		}
		if (electronics > 0)
		{
			parts += port->PurchaseCost(Good::Electronics, electronics);
			labour += electronics * GoodInfo::Of(Good::Electronics)->BasePrice;
		}
		if (munitions > 0)
		{
			parts += port->PurchaseCost(Good::Munitions, munitions);
			labour += munitions * GoodInfo::Of(Good::Munitions)->BasePrice;
		}
		return parts * (1 + Economy::PortTax) * priceFactor + labour * LabourShare;
	}

	bool PortRepairs::Line(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double priceFactor,
		RepairItemKind kind, int index, RepairItem& item)
	{
		item = RepairItem();
		item.Kind = kind;
		auto c = ship->Combat;
		double scale = ship->LengthKm();
		switch (kind)
		{
			case RepairItemKind::Hull:
			{
				if (c == nullptr) return false;
				double missing = 1 - c->HullFraction();
				if (missing < 1e-4) return false;
				item.Alloys = HullAlloysPerKm * scale * missing;
				item.Electronics = HullElectronicsPerKm * scale * missing;
				item.Label = cs::Cat("Hull and structure  ·  ", Pct(c->HullFraction()), " whole");
				if (!DoesStructure(port)) item.Blocked = "needs a shipyard";
				break;
			}
			case RepairItemKind::Subsystem:
			{
				if (c == nullptr || index < 0 || index >= c->Subsystems.Count()) return false;
				auto sub = c->Subsystems[index];
				double missing = 1 - sub->Health();
				if (missing < 1e-4) return false;
				double rebuild = sub->Destroyed() ? 1.5 : 1.0;
				item.Index = index;
				item.Alloys = cs::Math::Max(MountMinTonnes, MountAlloysPerKm * scale) * missing * rebuild;
				item.Electronics = cs::Math::Max(MountMinTonnes, MountElectronicsPerKm * scale) * missing * rebuild;
				item.Label = cs::Cat(Spaced(ToString(sub->Kind())), " #", index + 1, "  ·  ",
					sub->Destroyed() ? cs::String("shot away") : cs::Cat(Pct(sub->Health()), " whole"));
				if (!DoesStructure(port)) item.Blocked = "needs a shipyard";
				break;
			}
			case RepairItemKind::Shields:
			{
				if (c == nullptr) return false;
				double cap = 0, charge = 0;
				for (const auto& s : c->Subsystems)
				{
					if (s->Kind() != SubsystemKind::ShieldGenerator || s->Destroyed() || s->MaxShield <= 0) continue;
					cap += s->MaxShield;
					charge += cs::Math::Min(s->Shield, s->MaxShield);
				}
				if (cap <= 0) return false;
				double missing = 1 - charge / cap;
				if (missing < 1e-3) return false;
				item.Electronics = cs::Math::Max(ShieldMinTonnes, ShieldElectronicsPerKm * scale) * missing;
				item.Label = cs::Cat("Shield fields  ·  ", Pct(charge / cap), " charged");
				break;
			}
			case RepairItemKind::Munitions:
			{
				if (c == nullptr) return false;
				int missiles = 0, rounds = 0;
				for (const auto& s : c->Subsystems)
				{
					const auto& w = s->Def->Weapon;
					if (w == nullptr || w->Ammo <= 0 || s->Ammo >= w->Ammo) continue;
					if (w->Missile) missiles += w->Ammo - s->Ammo;
					else rounds += w->Ammo - s->Ammo;
				}
				if (missiles + rounds == 0) return false;
				item.Munitions = missiles * MissileTonnes + rounds * RoundTonnes;
				item.Label = missiles > 0 && rounds > 0 ? cs::Cat("Magazines  ·  ", missiles, " missiles, ", rounds, " rounds short")
					: missiles > 0 ? cs::Cat("Magazines  ·  ", missiles, missiles == 1 ? " missile short" : " missiles short")
					: cs::Cat("Magazines  ·  ", rounds, rounds == 1 ? " round short" : " rounds short");
				break;
			}
			case RepairItemKind::Propellant:
			{
				double room = ship->PropellantCapacity() - ship->Propellant();
				if (room < 0.01) return false;
				item.Cost = Economy::PropellantPrice(port) * room;
				item.Label = cs::Cat("Propellant  ·  tanks ", Pct(ship->PropellantFraction()));
				return true;
			}
		}
		item.InStock = cs::Math::Min(Shelf(port, AlloysI, item.Alloys), cs::Math::Min(Shelf(port, ElectronicsI, item.Electronics), Shelf(port, MunitionsI, item.Munitions)));
		item.Cost = MaterialsCost(port, item.Alloys, item.Electronics, item.Munitions, priceFactor);
		(void)economy;
		return true;
	}

	cs::List<RepairItem> PortRepairs::Quote(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double priceFactor)
	{
		cs::List<RepairItem> items;
		if (ship == nullptr || port == nullptr) return items;
		RepairItem item;
		if (Line(economy, ship, port, priceFactor, RepairItemKind::Hull, -1, item)) items.Add(item);
		if (ship->Combat != nullptr)
			for (int i = 0; i < ship->Combat->Subsystems.Count(); i++)
				if (Line(economy, ship, port, priceFactor, RepairItemKind::Subsystem, i, item)) items.Add(item);
		if (Line(economy, ship, port, priceFactor, RepairItemKind::Shields, -1, item)) items.Add(item);
		if (Line(economy, ship, port, priceFactor, RepairItemKind::Munitions, -1, item)) items.Add(item);
		if (Line(economy, ship, port, priceFactor, RepairItemKind::Propellant, -1, item)) items.Add(item);
		return items;
	}

	void PortRepairs::Charge(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double alloys, double electronics, double munitions, double cost)
	{
		double parts = 0;
		const int goods[] = { AlloysI, ElectronicsI, MunitionsI };
		const double tonnes[] = { alloys, electronics, munitions };
		for (int k = 0; k < 3; k++)
		{
			double t = tonnes[k];
			if (t <= 0) continue;
			int g = goods[k];
			double price = port->PurchaseCost(static_cast<Good>(g), t);
			parts += price;
			port->Stock[g] = cs::Math::Max(0.0, port->Stock[g] - t);
			port->ExtraDemand[g] += t;
			port->TradeVolumeCredits += price;
		}
		// The port's owner takes its tax on the parts and the yard's labour (and what the player's standing adds).
		port->Owner->Treasury += parts * Economy::PortTax + cs::Math::Max(0.0, cost - parts * (1 + Economy::PortTax));
		ship->Faction->Treasury -= cost;
		port->RefreshPrices();
	}

	RepairOutcome PortRepairs::Apply(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double priceFactor,
		RepairItemKind kind, int index, double& cost)
	{
		cost = 0;
		if (ship == nullptr || port == nullptr || economy == nullptr) return RepairOutcome::CannotDock;
		if (!economy->CanTrade(ship, port)) return RepairOutcome::CannotDock;
		RepairItem item;
		if (!Line(economy, ship, port, priceFactor, kind, index, item)) return RepairOutcome::NothingToDo;
		if (!item.Blocked.empty()) return RepairOutcome::NotAYard;
		if (kind == RepairItemKind::Propellant)
		{
			double tonnes = 0;
			switch (economy->RefuelAt(ship, port, tonnes, cost))
			{
				case Economy::TradeOutcome::Ok: return ship->PropellantFraction() < 0.99 ? RepairOutcome::Partial : RepairOutcome::Ok;
				case Economy::TradeOutcome::NoRoom: return RepairOutcome::NothingToDo;
				case Economy::TradeOutcome::NoStock: return RepairOutcome::NoStock;
				case Economy::TradeOutcome::CantAfford: return RepairOutcome::CantAfford;
				default: return RepairOutcome::CannotDock;
			}
		}
		double share = item.InStock;
		if (share <= 1e-6) return RepairOutcome::NoStock;
		double purse = ship->Faction->Treasury;
		if (item.Cost > 0 && purse < item.Cost * share) share = cs::Math::Max(0.0, purse / item.Cost);
		if (share <= 1e-6) return RepairOutcome::CantAfford;
		bool whole = share >= 1 - 1e-9;
		auto c = ship->Combat;
		switch (kind)
		{
			case RepairItemKind::Hull:
				c->RepairHull((1 - c->HullFraction()) * cs::Math::Min(1.0, share));
				break;
			case RepairItemKind::Subsystem:
			{
				// A hardpoint is mended whole or not at all.
				if (!whole) return item.InStock < 1 - 1e-9 ? RepairOutcome::NoStock : RepairOutcome::CantAfford;
				auto sub = c->Subsystems[index];
				sub->Hp = sub->Def->MaxHp;
				sub->Restoring = 0;
				if (sub->Kind() == SubsystemKind::ShieldGenerator) sub->Shield = sub->MaxShield;
				break;
			}
			case RepairItemKind::Shields:
				if (!whole) return item.InStock < 1 - 1e-9 ? RepairOutcome::NoStock : RepairOutcome::CantAfford;
				for (const auto& s : c->Subsystems)
					if (s->Kind() == SubsystemKind::ShieldGenerator && !s->Destroyed()) s->Shield = s->MaxShield;
				break;
			case RepairItemKind::Munitions:
				for (const auto& s : c->Subsystems)
				{
					const auto& w = s->Def->Weapon;
					if (w == nullptr || w->Ammo <= 0 || s->Ammo >= w->Ammo) continue;
					int missing = w->Ammo - s->Ammo;
					s->Ammo += whole ? missing : static_cast<int>(cs::Math::Floor(missing * share + 1e-9));
				}
				break;
			default:
				return RepairOutcome::NothingToDo;
		}
		share = cs::Math::Min(1.0, share);
		cost = item.Cost * share;
		Charge(ship, port, item.Alloys * share, item.Electronics * share, item.Munitions * share, cost);
		return whole ? RepairOutcome::Ok : RepairOutcome::Partial;
	}

	RepairOutcome PortRepairs::RepairAll(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double priceFactor,
		int& done, double& cost)
	{
		done = 0;
		cost = 0;
		auto quote = Quote(economy, ship, port, priceFactor);
		if (quote.Count() == 0) return RepairOutcome::NothingToDo;
		bool short_ = false;
		cs::Nullable<RepairOutcome> firstFailure;
		for (const auto& item : quote)
		{
			if (!item.Blocked.empty())
			{
				short_ = true;
				if (!firstFailure.has_value()) firstFailure = RepairOutcome::NotAYard;
				continue;
			}
			double paid = 0;
			auto outcome = Apply(economy, ship, port, priceFactor, item.Kind, item.Index, paid);
			cost += paid;
			if (outcome == RepairOutcome::Ok || outcome == RepairOutcome::Partial) done++;
			if (outcome != RepairOutcome::Ok)
			{
				short_ = true;
				if (outcome != RepairOutcome::Partial && !firstFailure.has_value()) firstFailure = outcome;
			}
		}
		if (done == 0) return firstFailure.has_value() ? *firstFailure : RepairOutcome::NothingToDo;
		return short_ ? RepairOutcome::Partial : RepairOutcome::Ok;
	}

	const char* ToString(RepairOutcome v)
	{
		switch (v)
		{
			case RepairOutcome::Ok: return "Ok";
			case RepairOutcome::Partial: return "Partial";
			case RepairOutcome::NothingToDo: return "NothingToDo";
			case RepairOutcome::NotAYard: return "NotAYard";
			case RepairOutcome::NoStock: return "NoStock";
			case RepairOutcome::CantAfford: return "CantAfford";
			case RepairOutcome::CannotDock: return "CannotDock";
		}
		return "?";
	}
}
