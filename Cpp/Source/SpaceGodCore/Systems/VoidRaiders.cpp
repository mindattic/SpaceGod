// US-T124: the Void Raiders. C++-only (no Unity.SpaceGod counterpart).

#include "Systems/VoidRaiders.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/VoidWatcher.h"

namespace SpaceGod::Core
{
	const cs::List<VoidRaiderLore>& VoidRaiders::Lore()
	{
		static const cs::List<VoidRaiderLore> lore
		{
			{ 1, "Sensor ghosts",
			  "Crews stranded in the void between jumps report unidentified sensor ghosts: contacts that come and go while a drive cools, "
			  "and are gone before anyone can say what they were." },
			{ 3, "Ghost ships",
			  "The ghosts are ships. They have struck capital hulls waiting out their cooldown in the void, and survivors describe hulls "
			  "that read as nothing on passive sensors until the moment they fire." },
			{ 6, "No known yard",
			  "Wrecks hit in the void carry weapon burns that match no yard's guns, and the few ghost hulls brought down are of no known "
			  "design. Whoever the ghosts are, nobody sells them their arms." },
			{ 10, "They hunt the stranded",
			  "Navy analysts: the ghosts only strike hulls stopped between jumps, and the longer a ship sits in the void the likelier "
			  "they come. Ships that spool fast, and ships that run cold and dark, are seldom found. They never come near a star or a void station." },
			{ 15, "The Void Raiders",
			  "The newsfeeds have a name for them now: the Void Raiders. Every power's navy has lost hulls to them, and none has found where they put in." },
			{ 25, "What they are",
			  "A ghost hull recovered nearly whole: its drive is a jump drive that needs no cooldown, built round the reactor of a "
			  "generation ship that left Sol before the gates. The Void Raiders are the descendants of crews that never arrived, "
			  "living out their lives in the dark between the stars, and they take from the stranded what the void never gave them." },
		};
		return lore;
	}

	int VoidRaiders::StageFor(int raids)
	{
		int stage = 0;
		for (const auto& l : Lore())
			if (raids >= l.Raids) stage++;
		return stage;
	}

	cs::String VoidRaiders::Called(int stage)
	{
		return stage >= 5 ? cs::String("Void Raider") : stage >= 2 ? cs::String("ghost ship") : cs::String("unidentified sensor ghost");
	}

	bool VoidRaiders::Lit(const Cruise& c, double jd)
	{
		return !cs::Double::IsNaN(c.PingJd) && jd < c.PingJd + PingLitDays;
	}

	bool VoidRaiders::Sheltered(DeepSpace& space, const cs::Ref<Cruise>& c, double jd)
	{
		auto g = space.Galaxy();
		if (g == nullptr) return false;
		auto at = c->PositionAt(jd);
		for (const auto& s : g->Systems)
			if (s->Origin.DistanceLy(at) < ShelterLy) return true;
		for (const auto& n : g->NearbyStars)
			if (n.Origin().DistanceLy(at) < ShelterLy) return true;
		return false;
	}

	double VoidRaiders::Hazard(const Cruise& c, double t0, double t1)
	{
		if (!(t1 > t0) || cs::Double::IsNaN(c.StopJd)) return 0;
		double a = cs::Math::Max(0.0, t0 - c.StopJd), b = cs::Math::Max(0.0, t1 - c.StopJd);
		double h = HazardPerDay2 * (b * b - a * a) / 2;
		if (c.RunDark) h *= DarkHazard;
		if (Lit(c, t0)) h *= LitHazard;
		// The Watcher's heralds (US-T178): they come where it is looking (the player's hull, by its dread).
		h *= VoidWatcher::HeraldFactor(c);
		return h;
	}

	void VoidRaiders::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng)
	{
		if (space.Cruises().Count() == 0) return;
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Ships.Count() == 0 || c->Holding || c->Crossing || cs::Double::IsNaN(c->StopJd)) continue;
			// Running dark holds the charge (the player's alone): the drive is cold while it lasts.
			if (c->RunDark && c->Piloted && fromJd < c->EngageJd())
			{
				double held = cs::Math::Min(toJd, c->EngageJd()) - cs::Math::Max(fromJd, c->ChargeJd);
				if (held > 0) c->DelayCharge(held);
			}
			double engage = c->EngageJd();
			if (!cs::Double::IsNaN(c->RaidStrikeJd))
			{
				int stage = space.VoidRaidLore;
				if (c->RaidStrikeJd < engage && c->RaidStrikeJd <= toJd)
				{
					Strike(space, c, c->RaidStrikeJd, rng);
					continue;
				}
				if (engage <= toJd)
				{
					// Charged and gone before it closed.
					c->RaidStrikeJd = cs::Double::NaN;
					space.Log(engage, cs::Cat("The ", c->Leader()->Name, " jumps out of the void before the ", Called(stage), " closing on it can strike"), c->Start);
					continue;
				}
				// Closing: a ping shows it at once and may scare it off; sensors at rest may pick it out; a dark hull may lose it.
				bool pinged = !cs::Double::IsNaN(c->PingJd) && c->PingJd >= fromJd - 1e-9 && c->PingJd < toJd;
				if (pinged)
				{
					c->RaidSeen = true;
					if (rng.NextDouble() < PingBreakOff)
					{
						c->RaidStrikeJd = cs::Double::NaN;
						space.Log(toJd, cs::Cat("The ", Called(stage), " closing on the ", c->Leader()->Name, " breaks off, caught in its ping"), c->Start);
						continue;
					}
				}
				else if (!c->RaidSeen && rng.NextDouble() < PassiveSpotPerStep * (c->RunDark ? 0.5 : 1))
					c->RaidSeen = true;
				if (c->RunDark && rng.NextDouble() < DarkLosePerStep)
				{
					c->RaidStrikeJd = cs::Double::NaN;
					space.Log(toJd, cs::Cat("The ", Called(stage), " closing on the ", c->Leader()->Name, " loses it in the dark"), c->Start);
				}
				continue;
			}
			if (c->RaidStruck) continue;
			double t0 = cs::Math::Max(fromJd, c->StopJd), t1 = cs::Math::Min(toJd, engage);
			if (!(t1 > t0)) continue;
			double h = Hazard(*c, t0, t1);
			if (!(h > 0) || Sheltered(space, c, t1)) continue;
			if (rng.NextDouble() >= 1 - cs::Math::Exp(-h)) continue;
			c->RaidStrikeJd = t1 + StalkDays;
			c->RaidSeen = Lit(*c, t1);
		}
	}

	void VoidRaiders::Strike(DeepSpace& space, const cs::Ref<Cruise>& c, double jd, cs::Random& rng)
	{
		c->RaidStrikeJd = cs::Double::NaN;
		c->RaidStruck = true;
		bool met = c->RaidSeen;
		int stage = space.VoidRaidLore;
		auto leader = c->Leader();
		cs::String leaderName = leader->Name;
		int ghosts = 1 + rng.Next(3);
		int lost = 0, downed = 0, hits = 0;
		for (int g = 0; g < ghosts; g++)
		{
			if (rng.NextDouble() < (met ? 0.35 : 0.15))
			{
				downed++;
				continue;
			}
			cs::List<cs::Ref<Ship>> escorts;
			for (const auto& s : c->Ships)
				if (s != c->Leader() && !s->Destroyed) escorts.Add(s);
			if (escorts.Count() > 0 && rng.NextDouble() < (met ? 0.3 : 0.55))
			{
				auto victim = escorts[rng.Next(escorts.Count())];
				space.Destroy(c, victim, jd, cs::Cat("by a ", Called(stage), " out of the dark"));
				lost++;
				if (c->Ships.Count() == 0) break;
				continue;
			}
			// The flagship hit: hull, tanks, and its drive scrambled, putting the charge back.
			auto f = c->Leader();
			if (f->Combat != nullptr && f->Combat->Spec != nullptr)
				f->Combat->Hull = cs::Math::Max(0.1 * f->Combat->Spec->Hull, f->Combat->Hull - (met ? 0.08 : 0.18) * f->Combat->Spec->Hull);
			f->SetPropellant(f->Propellant() * (met ? 0.95 : 0.88));
			c->DelayCharge(met ? ScrambleDays * 0.4 : ScrambleDays);
			hits++;
		}
		auto where = c->PositionAt(jd);
		for (int i = 0; i < downed; i++)
		{
			auto o = cs::New<DeepSpaceObject>();
			o->Kind = DeepSpaceObjectKind::Wreck; o->Name = cs::Cat("an unidentified wreck (", Called(stage), ")"); o->Position = where.Offset(DVec3(50 + i * 7, 0, 0));
			o->SalvageCredits = GhostWreckCredits; o->SinceJd = jd;
			space.Add(o);
		}
		cs::String what = ghosts == 1 ? cs::Cat("a ", Called(stage)) : cs::Cat(ghosts, " ", Called(stage), "s");
		cs::String text = cs::Cat(leaderName, " is ambushed in the void between jumps by ", what, met ? " it saw coming" : " out of nowhere",
			lost > 0 ? cs::Cat(": ", lost, " escort", lost == 1 ? "" : "s", " lost") : cs::String(),
			hits > 0 ? cs::Cat(lost > 0 ? ", " : ": ", "the flagship hit ", hits, " time", hits == 1 ? "" : "s", " and its drive scrambled") : cs::String(),
			downed > 0 ? cs::Cat(lost + hits > 0 ? "; " : ": ", downed, " of them wrecked") : cs::String());
		space.Log(jd, text, where);
		Report(c, text, jd);
		// A raid on record, and a ghost wreck counts twice: there is something to look at.
		Learn(space, c, 1 + downed, jd);
	}

	void VoidRaiders::Learn(DeepSpace& space, const cs::Ref<Cruise>& c, int found, double jd)
	{
		space.VoidRaids += found;
		int stage = StageFor(space.VoidRaids);
		while (space.VoidRaidLore < stage)
		{
			const auto& l = Lore()[space.VoidRaidLore];
			space.VoidRaidLore++;
			cs::String text = cs::Cat("Out of the void: ", l.Title, ". ", l.Text);
			space.Log(jd, text, c->Start);
			Report(c, text, jd);
		}
	}

	void VoidRaiders::Report(const cs::Ref<Cruise>& c, const cs::String& text, double jd)
	{
		// The word goes out from the system the group was bound for (or came from).
		auto at = c->To != nullptr ? c->To : c->From;
		if (at == nullptr || at->World == nullptr || at->Catalog == nullptr) return;
		at->World->Log(jd, text, WorldEventKind::Loss, at->Catalog->Root()->Index);
	}

	cs::String VoidRaiders::Ping(const cs::Ref<Cruise>& c, double jd)
	{
		if (c == nullptr || !c->Piloted || !c->AtVoidStop(jd)) return "Pings only at a void stop between jumps";
		if (c->RunDark) return "Dark: Shift+F3 powers up first";
		if (!cs::Double::IsNaN(c->PingJd) && jd < c->PingJd + PingCooldownDays) return "The emitters are still recharging";
		c->PingJd = jd;
		return cs::String();
	}

	cs::String VoidRaiders::ToggleDark(const cs::Ref<Cruise>& c, double jd, bool& changed)
	{
		changed = false;
		if (c == nullptr || !c->Piloted || !c->AtVoidStop(jd)) return "Only at a void stop between jumps";
		c->RunDark = !c->RunDark;
		changed = true;
		return c->RunDark ? cs::String("RUNNING DARK: reactor down, drive cold, a quarter as easy to find; no charge builds until Shift+F3 powers up")
			: cs::String("POWERED UP: the drive charges again");
	}
}
