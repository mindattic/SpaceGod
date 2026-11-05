// US-T44: Hud.Coop.cs (US-T43) in C++: the HUD's actions that write the Core, carried out through the co-op frames.
// Declarations in Hud.Coop.inl. In single-player every hook is false and the original goes on.

#include "UI/Hud.h"
#include "UI/GalaxyMapScreen.h" // US-T47

#include "UnitySpaceApp.h"
#include "Coop/CoopPeer.h"
#include "Coop/CoopWire.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Economy/Charters.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/MarketIntel.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Economy/TradeInsurance.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"

#include <optional>

using namespace SpaceGod::Core;
using SpaceGod::Coop::CoopPeer;
namespace Wire = SpaceGod::Coop::Wire;

namespace SpaceGod::UI
{
	GalaxyMapScreen* Hud::CoopMap() // US-T47
	{
		if (!galaxyMap) galaxyMap = cs::New<GalaxyMapScreen>(app);
		return galaxyMap.get();
	}

	bool Hud::CoopSendsText(const cs::String& verb, const cs::String& args)
	{
		if (CoopPeer::Current() == nullptr) return false;
		return app->CoopSends(verb, args);
	}

	cs::String Hud::CoopArg(int v) { return Wire::I(v); }
	cs::String Hud::CoopArg(const cs::String& s) { return s; }

	int Hud::K(const cs::Ref<Settlement>& s)
	{
		if (s == nullptr) return -1;
		const auto& all = app->Economy()->Settlements;
		for (int i = 0; i < all.Count(); i++)
			if (all[i] == s) return i;
		return -1;
	}

	cs::Ref<Settlement> Hud::Port(const cs::String& k)
	{
		int i = Wire::ParseI(k);
		const auto& all = app->Economy()->Settlements;
		return i >= 0 && i < all.Count() ? all[i] : nullptr;
	}

	template <class T>
	cs::Ref<T> Hud::At(const cs::List<cs::Ref<T>>& list, const cs::String& k)
	{
		int want = Wire::ParseI(k);
		return want >= 0 && want < list.Count() ? list[want] : nullptr;
	}

	bool Hud::CoopObserve(const cs::Ref<Settlement>& market)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return false;
		if (observedFrame == peer->Frame() && observedAt == market) return true;
		observedFrame = peer->Frame();
		observedAt = market;
		return CoopSends("observe", K(market));
	}

	namespace
	{
		int FacilityIndex(const cs::Ref<FacilityKind>& k)
		{
			if (k == nullptr) return -1;
			const auto& all = FacilityKinds::Catalogue();
			for (int i = 0; i < all.Count(); i++)
				if (all[i] == k) return i;
			return -1;
		}
	}

	bool Hud::CoopSendsOutfit(const cs::Ref<Settlement>& yard, const OutfitChoice& option)
	{
		return CoopSends("outfit", K(yard), outfitSlot, option.module.has_value() ? static_cast<int>(*option.module) : -1, FacilityIndex(option.facility));
	}

	cs::String Hud::CoopKeyText(const Target& sel)
	{
		cs::String key = app->CoopTargetKey(sel);
		for (auto& c : key) if (c == ' ') c = '|';
		return key;
	}

	bool Hud::CoopExecute(const cs::String& verb, const std::vector<cs::String>& a)
	{
		auto world = app->World();
		double jd = app->Clock->Jd;
		// ---- the market board
		if (verb == "buy" || verb == "sell")
		{
			auto market = Port(a.at(0));
			if (market == nullptr || world->PilotedShip == nullptr) return true;
			int was = tradeCrates;
			tradeCrates = Wire::ParseI(a.at(2));
			try
			{
				if (verb == "buy") ClickBuy(market, static_cast<Good>(Wire::ParseI(a.at(1))));
				else ClickSell(market, static_cast<Good>(Wire::ParseI(a.at(1))));
			}
			catch (...)
			{
				tradeCrates = was;
				throw;
			}
			tradeCrates = was;
			return true;
		}
		if (verb == "refuel")
		{
			auto market = Port(a.at(0));
			if (market != nullptr && world->PilotedShip != nullptr) ClickRefuel(market);
			return true;
		}
		if (verb == "observe")
		{
			auto market = Port(a.at(0));
			if (market != nullptr && world->Trade != nullptr) world->Trade->Intel->Observe(market, jd, "seen in port");
			return true;
		}
		if (verb == "bounty")
		{
			auto market = Port(a.at(0));
			if (market != nullptr && world->Law != nullptr) PayBounty(market);
			return true;
		}
		// ---- outfitting
		if (verb == "prisoners")
		{
			auto port = Port(a.at(0));
			auto ship = world->PilotedShip;
			if (port != nullptr && ship != nullptr) HandOverPrisoners(ship, port);
			return true;
		}
		if (verb == "outfit")
		{
			auto yard = Port(a.at(0));
			auto ship = world->PilotedShip;
			if (yard == nullptr || ship == nullptr || ship->Combat == nullptr) return true;
			auto loadout = Outfitting::LoadoutOf(ship);
			int slot = Wire::ParseI(a.at(1)), module = Wire::ParseI(a.at(2)), facility = Wire::ParseI(a.at(3));
			std::optional<OutfitChoice> chosen;
			for (const auto& o : OutfitOptions(loadout, slot))
				if ((o.module.has_value() ? static_cast<int>(*o.module) : -1) == module && FacilityIndex(o.facility) == facility)
				{
					chosen = o;
					break;
				}
			if (!chosen.has_value())
			{
				app->ShowToast("Can't fit that here");
				return true;
			}
			int was = outfitSlot;
			outfitSlot = slot;
			try { OutfitApply(ship, yard, loadout, *chosen); }
			catch (...)
			{
				if (!app->CoopActingHere()) outfitSlot = was;
				throw;
			}
			if (!app->CoopActingHere()) outfitSlot = was; // the row picked is the asker's panel's
			return true;
		}
		// ---- the bounty board's brig
		if (verb == "deliver" || verb == "ransom")
		{
			auto port = Port(a.at(0));
			auto outlaws = world->Outlaws;
			auto ship = world->PilotedShip;
			if (port == nullptr || outlaws == nullptr || ship == nullptr) return true;
			auto o = At(outlaws->HeldAboard(ship), a.at(1));
			if (o == nullptr) return true;
			if (verb == "deliver") DeliverOutlaw(outlaws, o, port);
			else RansomOutlaw(outlaws, o, port);
			return true;
		}
		// ---- contracts
		if (verb == "ctake")
		{
			auto port = Port(a.at(0));
			auto contracts = world->Contracts;
			auto ship = world->PilotedShip;
			if (port == nullptr || contracts == nullptr || ship == nullptr) return true;
			auto c = At(contracts->BoardAt(port), a.at(1));
			if (c != nullptr) TakeContract(contracts, c, ship, jd);
			return true;
		}
		if (verb == "cabandon")
		{
			auto contracts = world->Contracts;
			auto c = contracts != nullptr ? At(contracts->PlayerActive(), a.at(0)) : nullptr;
			if (c != nullptr && contracts->Abandon(c, jd)) app->ShowToast(cs::Cat("Contract abandoned: ", c->Title()));
			return true;
		}
		// ---- the player's fleet
		if (verb == "deposit" || verb == "withdraw" || verb == "enlist")
		{
			auto h = world->Holdings;
			auto flown = world->PilotedShip;
			cs::Ref<Faction> purse = flown != nullptr && h != nullptr && flown->Faction != h->House ? flown->Faction : nullptr;
			if (purse == nullptr) return true;
			if (verb == "deposit") app->ShowToast(h->Deposit(purse, 1e7, jd) ? cs::String("10 M cr into the house account") : cs::String("The purse cannot spare it"));
			else if (verb == "withdraw") app->ShowToast(h->Withdraw(purse, 1e7, jd) ? cs::Cat("10 M cr out to the ", purse->Name) : cs::String("Not that much in the account"));
			else if (!h->Owns(flown)) app->ShowToast(h->Enlist(flown, jd) != nullptr ? cs::Cat(flown->Name, " flies your flag now") : cs::String("It cannot join the fleet"));
			return true;
		}
		if (verb == "hull")
		{
			auto h = world->Holdings;
			auto port = Port(a.at(0));
			if (h == nullptr || port == nullptr || !h->CanBuyAt(port)) return true;
			auto bought = h->Buy(port, static_cast<ShipClass>(Wire::ParseI(a.at(1))), cs::String(), jd);
			app->ShowToast(bought != nullptr ? cs::Cat("Bought the ", bought->Name(), " at ", port->Name) : cs::String("Cannot afford it here"));
			return true;
		}
		if (verb == "warehouse")
		{
			auto h = world->Holdings;
			auto port = Port(a.at(0));
			if (h != nullptr && port != nullptr)
				app->ShowToast(h->BuyWarehouse(port, jd) != nullptr ? cs::Cat("A warehouse at ", port->Name, " for ", Credits(Holdings::WarehousePrice)) : cs::String("Not for sale"));
			return true;
		}
		if (verb == "share")
		{
			auto h = world->Holdings;
			auto port = Port(a.at(0));
			if (h != nullptr && port != nullptr) app->ShowToast(h->BuyShare(port, 0.05, jd) != nullptr ? cs::Cat("5% of ", port->Name, " is yours") : cs::String("Not for sale"));
			return true;
		}
		if (verb == "claim")
		{
			auto h = world->Holdings;
			int body = Wire::ParseI(a.at(0));
			if (h != nullptr && body >= 0 && body < world->Catalog()->Count())
				app->ShowToast(h->StakeClaim(body, static_cast<Good>(Wire::ParseI(a.at(1))), jd) != nullptr ? cs::Cat("Claim staked on ", (*world->Catalog())[body]->Name)
					: cs::String("Cannot stake that claim"));
			return true;
		}
		if (verb == "captain")
		{
			auto h = world->Holdings;
			auto o = h != nullptr ? At(h->Ships, a.at(0)) : nullptr;
			if (o == nullptr) return true;
			if (o->Captain == nullptr)
			{
				auto hired = h->Hire(o, jd);
				app->ShowToast(cs::Cat("Captain ", hired->Name, " takes the ", o->Name()));
			}
			else h->Dismiss(o, jd);
			return true;
		}
		if (verb == "orders")
		{
			auto h = world->Holdings;
			auto o = h != nullptr ? At(h->Ships, a.at(0)) : nullptr;
			if (o == nullptr || o->OnCharter()) return true;
			cs::String key = a.size() > 1 ? a[1] : cs::String("n");
			for (auto& c : key) if (c == '|') c = ' ';
			auto next = NextOrders(o, app->CoopTarget(key));
			app->ShowToast(cs::Cat(o->Name(), ": ", next->Describe(world->Catalog())));
			return true;
		}
		// ---- the trade desk
		if (verb == "route")
		{
			auto h = world->Holdings;
			auto idle = h != nullptr ? At(h->Ships, a.at(0)) : nullptr;
			if (idle == nullptr) return true;
			int from = Wire::ParseI(a.at(1)), to = Wire::ParseI(a.at(2));
			app->ShowToast(h->Order(idle, StandingOrder::TradeRoute(cs::List<int>{ from, to }))
				? cs::Cat("The ", idle->Name(), " runs ", (*world->Catalog())[from]->Name, " - ", (*world->Catalog())[to]->Name)
				: cs::Cat("The ", idle->Name(), " cannot take that route"));
			return true;
		}
		if (verb == "charter")
		{
			auto charters = world->Trade != nullptr ? world->Trade->Charters : nullptr;
			auto offer = charters != nullptr ? At(cs::List<cs::Ref<CharterOffer>>(charters->Offers), a.at(0)) : nullptr;
			auto candidate = offer != nullptr ? charters->Candidate(offer, jd) : nullptr;
			if (candidate != nullptr)
				app->ShowToast(charters->Take(offer, candidate, jd) != nullptr ? cs::Cat("The ", candidate->Name(), " goes out on charter to ", offer->Charterer->Name)
					: cs::String("No longer on offer"));
			return true;
		}
		if (verb == "recall")
		{
			auto charters = world->Trade != nullptr ? world->Trade->Charters : nullptr;
			auto c = charters != nullptr ? At(charters->Active(), a.at(0)) : nullptr;
			if (c != nullptr) app->ShowToast(charters->Recall(c, jd) ? cs::Cat("The ", c->Ship->Name(), " is recalled") : cs::String("The account cannot pay the recall"));
			return true;
		}
		if (verb == "insure" || verb == "uninsure")
		{
			auto ins = world->Trade != nullptr ? world->Trade->Insurance : nullptr;
			auto h = world->Holdings;
			if (ins == nullptr || h == nullptr) return true;
			bool ship = a.at(0) == "s";
			auto o = ship ? At(h->Ships, a.at(1)) : nullptr;
			auto p = ship ? nullptr : At(h->Properties, a.at(1));
			if (o == nullptr && p == nullptr) return true;
			auto policy = o != nullptr ? ins->Of(o) : ins->Of(p);
			cs::String name = cs::Cat("The ", o != nullptr ? o->Name() : p->Name);
			if (verb == "uninsure") { if (policy != nullptr) ins->Cancel(policy, jd); }
			else if (policy == nullptr)
				app->ShowToast((o != nullptr ? ins->Insure(o, jd) : ins->Insure(p, jd)) != nullptr ? cs::Cat(name, " is insured") : cs::String("The underwriters will not write it"));
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- the original's actions, as their buttons do them

	void Hud::PayBounty(const cs::Ref<Settlement>& market)
	{
		auto law = app->World()->Law;
		double paid = 0;
		app->ShowToast(law->PayFine(app->World()->PilotedShip, market->Owner, app->Clock->Jd, paid)
			? cs::Cat("Paid ", Credits(paid), " to ", market->Owner->Name, ": the bounty is lifted")
			: cs::String("Not enough credits to pay off the bounty"));
	}

	void Hud::HandOverPrisoners(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port)
	{
		double paid = Outfitting::HandOverPrisoners(app->Economy(), ship, port);
		app->ShowToast(paid > 0 ? cs::Cat("Prisoners handed over at ", port->Name, " for ", Credits(paid)) : cs::String("Not welcome here"));
	}

	void Hud::DeliverOutlaw(const cs::Ref<Outlaws>& outlaws, const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& port)
	{
		double paid = outlaws->Deliver(o, port, app->Clock->Jd);
		app->ShowToast(o->Fate == OutlawFate::Captured
			? cs::Cat(o->Name(), " handed over at ", port->Name, paid > 0 ? cs::Cat(" for ", Credits(paid)) : cs::String())
			: cs::String("Not welcome here"));
	}

	void Hud::RansomOutlaw(const cs::Ref<Outlaws>& outlaws, const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& port)
	{
		double paid = outlaws->Ransom(o, port, app->Clock->Jd);
		app->ShowToast(paid > 0 ? cs::Cat(o->Name(), " ransomed back for ", Credits(paid)) : cs::Cat("The ", port->Owner->Name, " will not pay"));
	}

	void Hud::TakeContract(const cs::Ref<Contracts>& contracts, const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, double jd)
	{
		auto outcome = contracts->Take(c, ship, true, jd);
		cs::String message;
		switch (outcome)
		{
		case Contracts::TakeOutcome::Ok: message = cs::Cat("Contract taken: ", c->Title(), ", due in ", cs::Fmt(c->DeadlineDays, "0"), " d"); break;
		case Contracts::TakeOutcome::NotOffered: message = cs::Cat(c->Patron->Name, " offers you no contracts"); break;
		case Contracts::TakeOutcome::NoRoom: message = "No room aboard for that"; break;
		case Contracts::TakeOutcome::Unarmed: message = "That job wants an armed ship"; break;
		case Contracts::TakeOutcome::NotHere: message = cs::Cat("Fly alongside ", c->Board->Name, " to sign"); break;
		default: message = "No longer on offer"; break;
		}
		app->ShowToast(message);
	}
}
