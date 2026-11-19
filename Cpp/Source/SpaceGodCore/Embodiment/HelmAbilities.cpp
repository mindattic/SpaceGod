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
		// US-T332: any hull in a fight can call its side's fire onto the hull it has locked.
		if (fight)
		{
			HelmAbilityEntry e;
			e.Ability = HelmAbility::CallFire;
			e.Label = "Call fire on the locked hull";
			bool standing = u->FireCallTarget != nullptr && b->Seconds < u->FireCallUntil && u->FireCallTarget->Present();
			e.State = standing ? cs::Cat("on the ", u->FireCallTarget->Name(), ", ", cs::Fmt(u->FireCallUntil - b->Seconds, "0"), " s")
				: u->PlayerLockTarget != nullptr ? cs::Cat("at the ", u->PlayerLockTarget->Name()) : cs::String("lock a hull (T)");
			e.Ready = u->PlayerLockTarget != nullptr;
			bar.Add(e);
		}
		// US-T333: and call its side's screening hulls (flak, interceptors) to guard it.
		if (fight)
		{
			HelmAbilityEntry e;
			e.Ability = HelmAbility::ScreenMe;
			e.Label = "Screen me";
			int screens = b->ScreensInReach(u);
			bool standing = b->Seconds < u->ScreenCallUntil;
			e.State = standing ? cs::Cat("screened, ", cs::Fmt(u->ScreenCallUntil - b->Seconds, "0"), " s")
				: screens > 0 ? cs::Cat(screens, screens == 1 ? " screen in reach" : " screens in reach") : cs::String("no screen in reach");
			e.Ready = screens > 0;
			bar.Add(e);
		}
		// US-T338: with recon of its side aloft, it can call them to paint the hull it has locked for the side's bombers.
		if (fight)
		{
			int recon = b->ReconAloftFor(u);
			bool standing = u->PaintCallTarget != nullptr && b->Seconds < u->PaintCallUntil && u->PaintCallTarget->Present();
			if (recon > 0 || standing)
			{
				HelmAbilityEntry e;
				e.Ability = HelmAbility::PaintTarget;
				e.Label = "Paint the locked hull";
				e.State = standing ? cs::Cat("painting the ", u->PaintCallTarget->Name(), ", ", cs::Fmt(u->PaintCallUntil - b->Seconds, "0"), " s")
					: cs::Cat(recon, " recon aloft", u->PlayerLockTarget != nullptr ? cs::String() : cs::String(", lock a hull (T)"));
				e.Ready = recon > 0 && u->PlayerLockTarget != nullptr;
				bar.Add(e);
			}
		}
		// US-T342: with badly damaged ships of its side in reach, it can call them to break off and close on it.
		if (fight)
		{
			int damaged = b->DamagedInReach(u);
			bool standing = b->Seconds < u->FallBackCallUntil;
			if (damaged > 0 || standing)
			{
				HelmAbilityEntry e;
				e.Ability = HelmAbility::FallBackOnMe;
				e.Label = "Fall back on me";
				e.State = standing ? cs::Cat("falling back, ", cs::Fmt(u->FallBackCallUntil - b->Seconds, "0"), " s")
					: cs::Cat(damaged, damaged == 1 ? " damaged ship in reach" : " damaged ships in reach");
				e.Ready = damaged > 0;
				bar.Add(e);
			}
		}
		// US-T347: a hull with damage control teams and a system they can mend can rush them to the repairs.
		if (fight && u->Broadside != nullptr)
		{
			cs::String worst;
			int mendable = b->MendableSystems(u, worst);
			bool standing = b->RushingRepairs(u);
			if (mendable > 0 || standing)
			{
				HelmAbilityEntry e;
				e.Ability = HelmAbility::RushRepairs;
				e.Label = "Damage control";
				e.State = standing ? cs::Cat("at the repairs, guns slow, ", cs::Fmt(u->RushRepairUntil - b->Seconds, "0"), " s")
					: cs::Cat(mendable, mendable == 1 ? " system to mend" : " systems to mend", ", worst: ", worst);
				e.Ready = mendable > 0 && !standing;
				bar.Add(e);
			}
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
			case HelmAbility::CallFire:
			{
				auto target = u->PlayerLockTarget;
				int answering = 0;
				auto why = b->OrderFireCall(u, target, answering);
				return why.empty() ? cs::Cat("Fire on the ", target->Name(), ": ", answering, answering == 1 ? " ship answers" : " ships answer",
					" for ", cs::Fmt(Battle::FireCallSeconds, "0"), " s") : cs::Cat("No call: ", why);
			}
			case HelmAbility::ScreenMe:
			{
				int answering = 0;
				auto why = b->OrderScreenCall(u, answering);
				return why.empty() ? cs::Cat("Screen on us: ", answering, answering == 1 ? " hull closes in" : " hulls close in",
					" for ", cs::Fmt(Battle::ScreenCallSeconds, "0"), " s") : cs::Cat("No screen: ", why);
			}
			case HelmAbility::PaintTarget:
			{
				auto target = u->PlayerLockTarget;
				int answering = 0;
				auto why = b->OrderPaintCall(u, target, answering);
				return why.empty() ? cs::Cat("Paint the ", target->Name(), ": ", answering, answering == 1 ? " recon craft turns" : " recon craft turn",
					" onto it for ", cs::Fmt(Battle::PaintCallSeconds, "0"), " s; our bombers' torpedoes ride the paint") : cs::Cat("No paint: ", why);
			}
			case HelmAbility::FallBackOnMe:
			{
				int answering = 0;
				auto why = b->OrderFallBackCall(u, answering);
				return why.empty() ? cs::Cat("Fall back on us: ", answering, answering == 1 ? " damaged ship breaks off" : " damaged ships break off",
					" for ", cs::Fmt(Battle::FallBackCallSeconds, "0"), " s") : cs::Cat("No fall back: ", why);
			}
			case HelmAbility::RushRepairs:
			{
				cs::String worst;
				auto why = b->OrderRushRepair(u, worst);
				return why.empty() ? cs::Cat("Damage control: every team to the repairs for ", cs::Fmt(Battle::RushRepairSeconds, "0"),
					" s, the ", worst, " first; the guns reload at half pace") : cs::Cat("No damage control rush: ", why);
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
