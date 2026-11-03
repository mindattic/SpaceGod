#include "Combat/CarrierWings.h"
#include "Combat/Dogfighting.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/Settlement.h"
#include "Ships.h"

// ShipCombat.Hangar and ShuttleBay are shared between a ShipCombat and its refit (ShipCombat.Refitted assigns
// c.Hangar = Hangar), so per docs/MIGRATION.md they are held as cs::Ref<cs::List<cs::Ref<Ship>>>.

namespace SpaceGod::Core
{
	FighterRole CarrierWings::Berth(const cs::Ref<CombatSpec>& spec, int berth, int& squadron, int& slot)
	{
		int recon = cs::Math::Max(0, spec->ReconCraft);
		if (berth < recon)
		{
			squadron = -1;
			slot = berth;
			return FighterRole::Recon;
		}
		int j = berth - recon;
		squadron = j / SquadronSize;
		slot = j % SquadronSize;
		return FighterCraft::RoleOfSquadron(squadron);
	}

	cs::Ref<Ship> CarrierWings::NewCraft(FighterRole role, int berth, const cs::Ref<Faction>& faction)
	{
		auto craft = FighterCraft::Of(role);
		auto ship = cs::New<Ship>();
		ship->Id = -1;
		ship->Name = cs::Cat(cs::Strings::ToUpperInvariant(cs::Strings::Substring(craft->Name, 0, 1)), cs::Strings::Substring(craft->Name, 1), " ", berth + 1);
		ship->Spec = ShipClassSpec::Of(craft->Class);
		ship->Faction = faction;
		ship->Role = ShipRole::Escort;
		ship->Phase = ShipPhase::Parked;
		ship->Combat = cs::New<ShipCombat>(craft->Spec, cs::Nullable<ShipClass>(craft->Class));
		auto wing = cs::New<WingCraft>();
		wing->Role = role;
		wing->Berth = berth;
		ship->Craft = wing;
		return ship;
	}

	bool CarrierWings::IsCraft(const cs::Ref<Ship>& s) { return (s ? s->Craft : nullptr) != nullptr; }

	const cs::List<cs::Ref<Ship>>& CarrierWings::Aboard(const cs::Ref<ShipCombat>& c)
	{
		static const cs::List<cs::Ref<Ship>> empty;
		return c != nullptr && c->Hangar != nullptr ? *c->Hangar : empty;
	}

	int CarrierWings::Aboard(const cs::Ref<ShipCombat>& c, FighterRole role)
	{
		int n = 0;
		if ((c ? c->Hangar : nullptr) != nullptr)
			for (const auto& s : *c->Hangar)
				if (s->Craft->Role == role) n++;
		return n;
	}

	void CarrierWings::Fill(const cs::Ref<ShipCombat>& c, int count)
	{
		if (count <= 0) return;
		int berths = c->Spec->HangarCraft();
		cs::HashSet<int> taken;
		for (const auto& s : *c->Hangar) taken.Add(s->Craft->Berth);
		for (int b = 0; b < berths && count > 0; b++)
		{
			if (taken.Contains(b)) continue;
			int discardSquadron = 0, discardSlot = 0;
			Insert(c, NewCraft(Berth(c->Spec, b, discardSquadron, discardSlot), b));
			count--;
		}
	}

	void CarrierWings::FillShuttles(const cs::Ref<ShipCombat>& c, int count)
	{
		if (count <= 0) return;
		int berths = cs::Math::Max(0, c->Spec->Shuttles);
		cs::HashSet<int> taken;
		for (const auto& s : *c->ShuttleBay) taken.Add(s->Craft->Berth);
		for (int b = 0; b < berths && count > 0; b++)
		{
			if (taken.Contains(b)) continue;
			Insert(*c->ShuttleBay, NewCraft(FighterRole::AssaultShuttle, b));
			count--;
		}
	}

	void CarrierWings::TrimShuttles(const cs::Ref<ShipCombat>& c, int count)
	{
		count = cs::Math::Max(0, count);
		while (c->ShuttleBay->Count() > count) c->ShuttleBay->RemoveAt(c->ShuttleBay->Count() - 1);
	}

	bool CarrierWings::IsShuttle(const cs::Ref<Ship>& craft)
	{
		return (craft ? craft->Craft : nullptr) != nullptr && craft->Craft->Role == FighterRole::AssaultShuttle;
	}

	cs::Ref<Ship> CarrierWings::ReadyShuttle(const cs::Ref<ShipCombat>& c)
	{
		if ((c ? c->ShuttleBay : nullptr) == nullptr) return nullptr;
		for (const auto& s : *c->ShuttleBay)
			if (Flyable(s)) return s;
		return nullptr;
	}

	bool CarrierWings::DockedIn(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft)
	{
		return c != nullptr && craft != nullptr && (c->Hangar->Contains(craft) || c->ShuttleBay->Contains(craft));
	}

	void CarrierWings::Trim(const cs::Ref<ShipCombat>& c, int count)
	{
		count = cs::Math::Max(0, count);
		while (c->Hangar->Count() > count) c->Hangar->RemoveAt(c->Hangar->Count() - 1);
	}

	bool CarrierWings::Dock(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft)
	{
		if (c == nullptr || (craft ? craft->Craft : nullptr) == nullptr || DockedIn(c, craft)) return false;
		if (IsShuttle(craft)) return DockShuttle(c, craft);
		int berths = c->Spec->HangarCraft();
		if (c->Hangar->Count() >= berths) return false;
		cs::HashSet<int> taken;
		for (const auto& s : *c->Hangar) taken.Add(s->Craft->Berth);
		int berth = craft->Craft->Berth;
		if (berth < 0 || berth >= berths || taken.Contains(berth))
		{
			berth = -1;
			for (int b = 0; b < berths; b++)
			{
				if (taken.Contains(b)) continue;
				if (berth < 0) berth = b;
				int discardSquadron = 0, discardSlot = 0;
				if (Berth(c->Spec, b, discardSquadron, discardSlot) == craft->Craft->Role)
				{
					berth = b;
					break;
				}
			}
		}
		craft->Craft->Berth = berth;
		Rearm(craft->Combat);
		Insert(c, craft);
		return true;
	}

	bool CarrierWings::DockShuttle(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft)
	{
		int berths = cs::Math::Max(0, c->Spec->Shuttles);
		if (c->ShuttleBay->Count() >= berths) return false;
		cs::HashSet<int> taken;
		for (const auto& s : *c->ShuttleBay) taken.Add(s->Craft->Berth);
		int berth = craft->Craft->Berth;
		if (berth < 0 || berth >= berths || taken.Contains(berth))
		{
			berth = 0;
			while (taken.Contains(berth)) berth++;
		}
		craft->Craft->Berth = berth;
		Rearm(craft->Combat);
		Insert(*c->ShuttleBay, craft);
		return true;
	}

	void CarrierWings::Strand(const cs::Ref<Settlement>& at, const cs::Ref<Ship>& craft)
	{
		if (at == nullptr || (craft ? craft->Craft : nullptr) == nullptr || at->Strays.Contains(craft)) return;
		at->Strays.Add(craft);
	}

	int CarrierWings::Rejoin(const cs::Ref<Settlement>& at, const cs::Ref<Ship>& carrier)
	{
		if (at == nullptr || (carrier ? carrier->Combat : nullptr) == nullptr || at->Strays.Count() == 0) return 0;
		int n = 0;
		for (int i = at->Strays.Count() - 1; i >= 0; i--)
		{
			auto craft = at->Strays[i];
			auto own = craft->Craft->Carrier;
			bool mine = own == carrier || ((own == nullptr || own->Destroyed) && craft->Faction == carrier->Faction);
			if (!mine || !Dock(carrier->Combat, craft)) continue;
			craft->Craft->Carrier = carrier;
			at->Strays.RemoveAt(i);
			n++;
		}
		return n;
	}

	bool CarrierWings::Undock(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft)
	{
		return c != nullptr && (c->Hangar->Remove(craft) || c->ShuttleBay->Remove(craft));
	}

	void CarrierWings::Insert(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft) { Insert(*c->Hangar, craft); }

	void CarrierWings::Insert(cs::List<cs::Ref<Ship>>& berths, const cs::Ref<Ship>& craft)
	{
		int at = berths.Count();
		while (at > 0 && berths[at - 1]->Craft->Berth > craft->Craft->Berth) at--;
		berths.Insert(at, craft);
	}

	bool CarrierWings::Flyable(const cs::Ref<Ship>& craft)
	{
		return (craft ? craft->Combat : nullptr) != nullptr && craft->Combat->Alive() && !craft->Combat->Disabled();
	}

	void CarrierWings::Rearm(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return;
		for (const auto& s : c->Subsystems)
		{
			if (s->Def->Weapon != nullptr) s->Ammo = s->Def->Weapon->Ammo;
			if (s->Kind() == SubsystemKind::ShieldGenerator && !s->Destroyed()) s->Shield = s->MaxShield;
			s->Cooldown = 0;
		}
		c->BoostFraction = 1;
	}

	int CarrierWings::MendAboard(const cs::Ref<ShipCombat>& c)
	{
		int n = 0;
		if ((c ? c->Hangar : nullptr) == nullptr) return 0;
		for (const auto& s : Docked(c))
		{
			if (s->Combat->Damage() <= 0.001) continue;
			s->Combat->Repair(1);
			Rearm(s->Combat);
			n++;
		}
		return n;
	}

	int CarrierWings::Damaged(const cs::Ref<ShipCombat>& c)
	{
		int n = 0;
		if ((c ? c->Hangar : nullptr) != nullptr)
			for (const auto& s : Docked(c))
				if (s->Combat->Damage() > 0.001) n++;
		return n;
	}

	int CarrierWings::DamagedIn(const cs::List<cs::Ref<Ship>>& berths)
	{
		int n = 0;
		for (const auto& s : berths)
			if (s->Combat->Damage() > 0.001) n++;
		return n;
	}

	cs::List<cs::Ref<Ship>> CarrierWings::Docked(const cs::Ref<ShipCombat>& c)
	{
		// C# yields lazily; the callers only read the craft (Repair and Rearm touch each craft, never the berth lists).
		cs::List<cs::Ref<Ship>> r;
		for (const auto& s : *c->Hangar) r.Add(s);
		if (c->ShuttleBay != nullptr)
			for (const auto& s : *c->ShuttleBay) r.Add(s);
		return r;
	}

	cs::String CarrierWings::Describe(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return "";
		int hurt = DamagedIn(*c->Hangar);
		return cs::Cat(c->Fighters(), " of ", c->Spec->HangarCraft(), " aboard", (hurt > 0 ? cs::Cat(", ", hurt, " damaged") : cs::String()));
	}

	cs::Ref<CraftPilot> CarrierWings::PilotOf(const cs::Ref<Ship>& craft, cs::Random& rng)
	{
		auto w = craft->Craft;
		if (w->Pilot != nullptr) return w->Pilot;
		auto pilot = cs::New<CraftPilot>();
		// Object initialiser order: Skill, Reaction, Discipline, Seed, each drawing from rng in turn.
		pilot->Skill = 0.4 + rng.NextDouble() * 0.6;
		pilot->Reaction = 0.4 + rng.NextDouble() * 1.4;
		pilot->Discipline = 0.45 + rng.NextDouble() * 0.5;
		pilot->Seed = rng.NextDouble() * 1000;
		w->Pilot = pilot;
		return w->Pilot;
	}
}
