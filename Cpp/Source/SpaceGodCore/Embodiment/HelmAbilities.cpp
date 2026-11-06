// US-T106: a hull's own abilities at its helm (see HelmAbilities.h).

#include "Embodiment/HelmAbilities.h"

#include "Combat/Battle.h"
#include "Combat/BreacherBays.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/SquadOrders.h" // US-T140
#include "Embodiment/ShipHelm.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		struct LaunchKind
		{
			HelmAbility Ability;
			FighterRole Role;
			const char* Plural;
		};

		// The classes in the order a carrier's squadrons go up (FighterCraft::SquadronMix), the recon flight last.
		const LaunchKind Kinds[] = {
			{ HelmAbility::LaunchInterceptors, FighterRole::Interceptor, "interceptors" },
			{ HelmAbility::LaunchFighters, FighterRole::SpaceSuperiority, "fighters" },
			{ HelmAbility::LaunchBombers, FighterRole::Bomber, "bombers" },
			{ HelmAbility::LaunchHeavyFighters, FighterRole::HeavyFighter, "heavy fighters" },
			{ HelmAbility::LaunchRecon, FighterRole::Recon, "recon fighters" },
		};

		const char* CallLabel(int call)
		{
			switch (call)
			{
				case 1: return "onto my locked target";
				case 2: return "guard the carrier";
				default: return "their own fights";
			}
		}

		/// <summary>The unit flying <paramref name="ship"/> in its fight, or null out of one.</summary>
		cs::Ref<BattleUnit> UnitOf(const cs::Ref<Ship>& ship)
		{
			if (ship == nullptr || ship->Battle == nullptr || ship->Battle->Over) return nullptr;
			auto u = ship->Battle->UnitOf(ship);
			return u != nullptr && u->Present() ? u : nullptr;
		}

		int Aboard(const cs::Ref<ShipCombat>& c, FighterRole role)
		{
			int n = 0;
			for (const auto& s : CarrierWings::Aboard(c))
				if (s->Craft->Role == role && CarrierWings::Flyable(s)) n++;
			return n;
		}

		int Up(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, bool any, FighterRole role)
		{
			if (b == nullptr || carrier == nullptr) return 0;
			int n = 0;
			for (const auto& o : b->Units)
				if (o->Present() && o->Fighter != nullptr && o->Fighter->Carrier == carrier && o->Fighter->Role != FighterRole::AssaultShuttle
					&& (any || o->Fighter->Role == role)) n++;
			return n;
		}

		/// <summary>Whether the hangar's berths are laid out for <paramref name="role"/> at all.</summary>
		bool Berths(const cs::Ref<CombatSpec>& spec, FighterRole role)
		{
			for (int i = 0; i < spec->HangarCraft(); i++)
			{
				int q = 0, slot = 0;
				if (CarrierWings::Berth(spec, i, q, slot) == role) return true;
			}
			return false;
		}

		bool HasPods(const cs::Ref<ShipCombat>& c, int& rounds, bool& loaded)
		{
			bool any = false;
			rounds = 0;
			loaded = false;
			for (const auto& s : c->Subsystems)
			{
				if (s->Kind() != SubsystemKind::BoardingPods || s->Destroyed()) continue;
				any = true;
				rounds += s->Ammo;
				if (s->Ammo > 0 && s->Cooldown <= 0) loaded = true;
			}
			return any;
		}
	}

	HelmAbilities::Result& HelmAbilities::Mutable()
	{
		static Result r;
		return r;
	}

	const HelmAbilities::Result& HelmAbilities::Last() { return Mutable(); }

	cs::List<HelmAbilityEntry> HelmAbilities::Of(const cs::Ref<Ship>& ship, const cs::Ref<World>& world)
	{
		cs::List<HelmAbilityEntry> bar;
		if (ship == nullptr || ship->Combat == nullptr || ship->Combat->Spec == nullptr || ship->Destroyed) return bar;
		auto c = ship->Combat;
		auto spec = c->Spec;
		auto u = UnitOf(ship);
		auto b = u != nullptr ? ship->Battle : nullptr;
		bool fight = u != nullptr;
		if (spec->HangarCraft() > 0 || c->Fighters() > 0)
		{
			for (const auto& k : Kinds)
			{
				int aboard = Aboard(c, k.Role), up = Up(b, u, false, k.Role);
				if (aboard == 0 && up == 0 && !Berths(spec, k.Role)) continue;
				HelmAbilityEntry e;
				e.Ability = k.Ability;
				e.Label = cs::Cat("Launch ", k.Plural);
				e.State = cs::Cat(aboard, " aboard", up > 0 ? cs::Cat(", ", up, " up") : cs::String());
				e.Ready = fight && aboard > 0;
				bar.Add(e);
			}
			int up = Up(b, u, true, FighterRole::Interceptor);
			HelmAbilityEntry recall;
			recall.Ability = HelmAbility::RecallWing;
			recall.Label = "Recall the wing";
			recall.State = u != nullptr && u->WingRecalled ? cs::Cat("recalled, ", up, " still up") : cs::Cat(up, " up");
			recall.Ready = fight && up > 0 && !u->WingRecalled;
			bar.Add(recall);
			int call = u != nullptr ? u->WingCall : 0;
			int next = (call + 1) % 3;
			HelmAbilityEntry wing;
			wing.Ability = next == 1 ? HelmAbility::WingAttack : next == 2 ? HelmAbility::WingGuard : HelmAbility::WingFree;
			wing.Label = cs::Cat("Wing: ", CallLabel(next));
			wing.State = cs::Cat("now ", CallLabel(call));
			wing.Ready = fight;
			bar.Add(wing);
		}
		if (spec->Shuttles > 0)
		{
			int ready = 0;
			for (const auto& s : *c->ShuttleBay)
				if (CarrierWings::Flyable(s)) ready++;
			HelmAbilityEntry e;
			e.Ability = HelmAbility::SendShuttle;
			e.Label = "Boarding shuttle at the locked hull";
			e.State = u != nullptr && u->ShuttleLaunchIn > 0 ? cs::Cat("deck clearing ", cs::Fmt(u->ShuttleLaunchIn, "0"), " s") : cs::Cat(ready, " aboard");
			e.Ready = fight && ready > 0 && (u == nullptr || u->ShuttleLaunchIn <= 0);
			bar.Add(e);
		}
		if (BreacherBays::Count(ship) > 0)
		{
			bool team = c->Interior != nullptr && BreacherBays::TeamReady(c->Interior);
			HelmAbilityEntry e;
			e.Ability = HelmAbility::FireBreacher;
			e.Label = "Breacher at the locked hull";
			e.State = u != nullptr && u->BreacherBayReload > 0 ? cs::Cat("cycling ", cs::Fmt(u->BreacherBayReload, "0"), " s")
				: team ? cs::String("team ready") : cs::String("readying a team");
			e.Ready = fight && team && u->BreacherBayReload <= 0;
			bar.Add(e);
		}
		int rounds = 0;
		bool loaded = false;
		if (HasPods(c, rounds, loaded))
		{
			HelmAbilityEntry e;
			e.Ability = HelmAbility::FirePods;
			e.Label = "Boarding pods at the locked hull";
			e.State = rounds <= 0 ? cs::String("racks empty") : cs::Cat(rounds, loaded ? " ready" : " reloading");
			e.Ready = fight && loaded;
			bar.Add(e);
		}
		// US-T140: a hull with a squad of its band about it orders it through its captain, before any fight (last, so the
		// slots above keep their keys).
		if (world != nullptr && SquadOrders::SquadOf(world, ship).Count() > 0)
		{
			cs::String state, why;
			HelmAbilityEntry e;
			e.Ability = HelmAbility::StandDown;
			e.Label = "Squad: stand down";
			e.Ready = SquadOrders::CanOrder(world, ship, world->Jd, state, why);
			e.State = state;
			bar.Add(e);
		}
		return bar;
	}

	cs::String HelmAbilities::Heading(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return "";
		auto c = ship->Combat;
		cs::String kind = c != nullptr && c->Spec != nullptr && c->Spec->HangarCraft() > 0 ? cs::String("CARRIER")
			: cs::Strings::ToUpperInvariant(ship->Spec->DisplayName);
		return cs::Cat(kind, "  ", ship->Name);
	}

	HelmAbility HelmAbilities::AtSlot(const cs::Ref<Ship>& ship, int slot, const cs::Ref<World>& world)
	{
		auto bar = Of(ship, world);
		return slot >= 1 && slot <= bar.Count() ? bar[slot - 1].Ability : HelmAbility::None;
	}

	bool HelmAbilities::LaunchRole(HelmAbility a, FighterRole& role)
	{
		for (const auto& k : Kinds)
			if (k.Ability == a)
			{
				role = k.Role;
				return true;
			}
		return false;
	}

	void HelmAbilities::Command(const cs::Ref<IEmbodiable>& seat, int ability)
	{
		if (auto helm = cs::As<ShipHelm>(seat)) Use(helm->Ship, static_cast<HelmAbility>(ability), helm->WorldOf());
	}

	bool HelmAbilities::Use(const cs::Ref<Ship>& ship, HelmAbility a, const cs::Ref<World>& world)
	{
		if (ship == nullptr || a == HelmAbility::None) return false;
		cs::List<cs::Ref<BattleUnit>> launched;
		auto toast = Order(ship, a, launched, world);
		auto& r = Mutable();
		r.Seq++;
		r.Toast = toast;
		r.Carrier = ship;
		r.Launched = launched;
		return launched.Count() > 0 || !toast.empty();
	}

	cs::String HelmAbilities::Order(const cs::Ref<Ship>& ship, HelmAbility a, cs::List<cs::Ref<BattleUnit>>& launched, const cs::Ref<World>& world)
	{
		// US-T140: the squad's order is given before a fight, through the captain; the crews answer (SquadOrders).
		if (a == HelmAbility::StandDown)
		{
			if (world == nullptr) return "";
			return SquadOrders::StandDown(world, ship, world->Jd).Toast;
		}
		auto c = ship->Combat;
		if (c == nullptr) return "";
		auto u = UnitOf(ship);
		auto b = u != nullptr ? ship->Battle : nullptr;
		FighterRole role{};
		bool launch = LaunchRole(a, role);
		if (u == nullptr)
		{
			// Every helm order is for a fight: the craft and the teams go out into one, never into empty space.
			if (launch || a == HelmAbility::RecallWing) return "Not in a fight: the wing launches only in battle, and stays aboard until one starts";
			if (a == HelmAbility::WingAttack || a == HelmAbility::WingGuard || a == HelmAbility::WingFree) return "Not in a fight: there is no wing up to call";
			return "Not in a fight: boarding orders are for battle (lock a hull in a fight with T)";
		}
		if (launch)
		{
			cs::String plural = "craft";
			for (const auto& k : Kinds)
				if (k.Ability == a) plural = k.Plural;
			launched = FighterAI::LaunchOrdered(b, u, role);
			if (launched.Count() == 0)
			{
				bool bay = false;
				for (const auto& s : c->Subsystems)
					if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) bay = true;
				if (!bay) return "Can't launch: the hangars are shot away";
				int crippled = 0;
				for (const auto& s : CarrierWings::Aboard(c))
					if (s->Craft->Role == role && !CarrierWings::Flyable(s)) crippled++;
				return crippled > 0 ? cs::Cat("No ", plural, " aboard fit to fly (", crippled, " crippled, for a yard to mend)")
					: cs::Cat("No ", plural, " aboard: every one is up or lost");
			}
			// A launch countermands a recall: what goes up stays up.
			u->WingRecalled = false;
			auto f = launched[0]->Fighter;
			return cs::Cat("Launching ", launched.Count(), " ", plural, f != nullptr && role != FighterRole::Recon ? cs::Cat(" (squadron ", f->Squadron + 1, ")") : cs::String(),
				" from the ", ship->Name, "  --  Shift+/ jumps into one");
		}
		switch (a)
		{
			case HelmAbility::RecallWing:
			{
				u->WingRecalled = true;
				int up = Up(b, u, true, FighterRole::Interceptor);
				return up == 0 ? cs::String("Recall: nothing of the wing is up; it stays aboard until launched")
					: cs::Cat("Recall: ", up, " craft turning for the flight decks, to stay aboard until launched again");
			}
			case HelmAbility::WingAttack:
				u->WingCall = 1;
				return u->PlayerLockTarget != nullptr ? cs::Cat("Wing: onto the ", u->PlayerLockTarget->Name())
					: cs::String("Wing: onto the hull you lock (T); until then their own fights");
			case HelmAbility::WingGuard:
				u->WingCall = 2;
				return cs::Cat("Wing: guard the ", ship->Name, " -- anything armed within ", cs::Fmt(FighterAI::GuardKm, "0"), " km of it");
			case HelmAbility::WingFree:
				u->WingCall = 0;
				return "Wing: back to their own fights";
			case HelmAbility::SendShuttle:
			{
				cs::Ref<BattleUnit> sent;
				auto why = b->OrderShuttle(u, u->PlayerLockTarget, sent);
				if (!why.empty()) return cs::Cat("No shuttle: ", why);
				launched.Add(sent);
				return cs::Cat("Boarding shuttle away for the ", u->PlayerLockTarget->Name(), " with a team aboard  --  Shift+/ jumps into it");
			}
			case HelmAbility::FireBreacher:
			{
				auto target = u->PlayerLockTarget;
				auto why = b->OrderBreacher(u, target);
				return why.empty() ? cs::Cat("Breacher away: a marine team for the ", target->Name()) : cs::Cat("No Breacher: ", why);
			}
			case HelmAbility::FirePods:
			{
				auto target = u->PlayerLockTarget;
				auto why = b->OrderPods(u, target);
				return why.empty() ? cs::Cat("Boarding pods away for the ", target->Name()) : cs::Cat("No pods: ", why);
			}
			default:
				return "";
		}
	}

	cs::List<cs::Ref<BattleUnit>> HelmAbilities::Aloft(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& carrier, const cs::List<cs::Ref<BattleUnit>>& recent)
	{
		cs::List<cs::Ref<BattleUnit>> list;
		if (battle == nullptr || carrier == nullptr || battle->Over) return list;
		auto mine = [&](const cs::Ref<BattleUnit>& o)
		{
			return o != nullptr && o->Fighter != nullptr && o->Fighter->Carrier == carrier && o->Present() && !o->Captured && battle->Units.Contains(o);
		};
		for (const auto& o : recent)
			if (mine(o) && !list.Contains(o)) list.Add(o);
		for (int i = battle->Units.Count() - 1; i >= 0; i--)
		{
			auto o = battle->Units[i];
			if (mine(o) && !list.Contains(o)) list.Add(o);
		}
		return list;
	}
}
