// US-T178: the Watcher in the void. C++-only (no Unity.SpaceGod counterpart).
//
// It is never seen whole and never given a shape: what comes to the crews are its signs (phantoms on the sensors,
// whispers on dead channels, stars going out, time slipping, space at the wrong angles, minds giving way, and the Void
// Raiders sent ahead of it). Keep it that way: no shapes, no limbs, nothing to draw.

#include "Systems/VoidWatcher.h"

#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Systems/DeepSpace.h"
#include "Systems/VoidRaiders.h"

namespace SpaceGod::Core
{
	const char* ToString(WatcherSignKind v)
	{
		switch (v)
		{
		case WatcherSignKind::Phantom: return "Phantom";
		case WatcherSignKind::Whisper: return "Whisper";
		case WatcherSignKind::StarsOut: return "StarsOut";
		case WatcherSignKind::TimeSlip: return "TimeSlip";
		case WatcherSignKind::Geometry: return "Geometry";
		case WatcherSignKind::Madness: return "Madness";
		case WatcherSignKind::Herald: return "Herald";
		}
		return "?";
	}

	const cs::List<VoidWatcherLore>& VoidWatcher::Lore()
	{
		static const cs::List<VoidWatcherLore> lore
		{
			{ 1, "Void sickness",
			  "Crews long between the stars report bad dreams, contacts that are not there, and voices on dead channels. Navy "
			  "surgeons call it void sickness and prescribe shore leave in a system's light." },
			{ 4, "The same dream",
			  "Crews of powers that have never met, on routes light-years apart, describe the same dream: an attention vast and "
			  "patient turning toward them from somewhere past the stars, and the certainty of having been noticed." },
			{ 10, "The lights going out",
			  "Astronomers confirm what pilots swore to: stars seen from the void go dark for minutes at a time, and come back. "
			  "Only to the crews out there; from home, they never went out." },
			{ 20, "It hears the drives",
			  "Navy analysts: the signs come thickest to the hulls that jump the most and sit longest in the void, and fade in a "
			  "system's light. Every jump breaks the law the universe keeps. Something hears it break." },
			{ 35, "The heralds",
			  "Void Raider strikes cluster round the hulls that report the signs. Whatever the Raiders are, they come where it is "
			  "looking, ahead of it. Some survivors swear the ghosts were not hunting at all, but driven." },
			{ 60, "The Watcher",
			  "Fragments from the oldest survey archives, decoded at last: a thing that was old when the universe was young, "
			  "asleep in the gaps between its laws, and every faster-than-light drive is a hand on its door. It has no shape any "
			  "witness has kept their mind long enough to describe. The archives call it only the Watcher, and say it is patient." },
		};
		return lore;
	}

	int VoidWatcher::StageFor(int signs)
	{
		int stage = 0;
		for (const auto& l : Lore())
			if (signs >= l.Signs) stage++;
		return stage;
	}

	cs::String VoidWatcher::Called(int lore)
	{
		return lore >= 6 ? cs::String("the Watcher") : lore >= 2 ? cs::String("something in the void") : cs::String("void sickness");
	}

	int VoidWatcher::StageOf(double dread)
	{
		return dread >= Stage4 ? 4 : dread >= Stage3 ? 3 : dread >= Stage2 ? 2 : dread >= Stage1 ? 1 : 0;
	}

	cs::String VoidWatcher::StageName(int stage)
	{
		switch (stage)
		{
		case 1: return "UNEASY";
		case 2: return "WATCHED";
		case 3: return "HUNTED";
		case 4: return "SEEN";
		default: return "CALM";
		}
	}

	double VoidWatcher::DreadAt(const Ship& s, double jd)
	{
		if (cs::Double::IsNaN(s.DreadJd) || !(jd > s.DreadJd)) return s.Dread;
		return s.Dread * cs::Math::Exp(-(jd - s.DreadJd) / FadeDays);
	}

	double VoidWatcher::DreadOf(const Cruise& c)
	{
		auto l = c.Ships.Count() > 0 ? c.Leader() : nullptr;
		return l != nullptr ? l->Dread : 0;
	}

	double VoidWatcher::HeraldFactor(const Cruise& c)
	{
		if (!c.Piloted) return 1;
		return 1 + HeraldBoost * DreadOf(c);
	}

	void VoidWatcher::Gather(const cs::Ref<Ship>& s, double fromJd, double toJd, double gain, bool fading)
	{
		// What faded since it was last out here (in a system's light), then this step's.
		if (!cs::Double::IsNaN(s->DreadJd) && fromJd > s->DreadJd + 1e-9) s->Dread = DreadAt(*s, fromJd);
		if (fading) s->Dread *= cs::Math::Exp(-(toJd - fromJd) / FadeDays);
		if (gain > 0) s->Dread = 1 - (1 - s->Dread) * cs::Math::Exp(-gain);
		s->DreadJd = toJd;
	}

	void VoidWatcher::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng)
	{
		if (space.Cruises().Count() == 0 || !(toJd > fromJd)) return;
		double days = toJd - fromJd;
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Plan || c->Ships.Count() == 0) continue;
			// The jumps engaged this voyage: one for each void stop behind it, and the one it is on once under way.
			int jumps = c->Stops + (!c->Holding && !c->AtVoidStop(toJd) && toJd >= c->EngageJd() ? 1 : 0);
			int fresh = cs::Math::Max(0, jumps - c->DreadJumps);
			c->DreadJumps = cs::Math::Max(c->DreadJumps, jumps);
			// Crossing a star's well under the ordinary drive is in its light: the dread fades there.
			bool lit = c->Crossing;
			double gain = (lit ? 0 : VoidDreadPerDay * days) + JumpDread * fresh;
			for (const auto& s : c->Ships)
				if (s != nullptr && !s->Destroyed) Gather(s, fromJd, toJd, gain, lit);
			if (lit) continue;
			double d = DreadOf(*c);
			int stage = StageOf(d);
			if (stage == 0) continue;
			if (rng.NextDouble() >= 1 - cs::Math::Exp(-SignsPerDay * d * d * days)) continue;
			Sign(space, c, stage, toJd, rng);
		}
	}

	void VoidWatcher::Sign(DeepSpace& space, const cs::Ref<Cruise>& c, int stage, double jd, cs::Random& rng)
	{
		// The signs its attention has come far enough for; the heralds only to a hull stopped in the void.
		cs::List<WatcherSignKind> kinds { WatcherSignKind::Phantom, WatcherSignKind::Whisper };
		if (stage >= 2) { kinds.Add(WatcherSignKind::StarsOut); kinds.Add(WatcherSignKind::TimeSlip); }
		if (stage >= 3) { kinds.Add(WatcherSignKind::Geometry); kinds.Add(WatcherSignKind::Madness); }
		if (stage >= 4 && c->AtVoidStop(jd) && !c->RaidStruck && cs::Double::IsNaN(c->RaidStrikeJd)) kinds.Add(WatcherSignKind::Herald);
		auto kind = kinds[rng.Next(kinds.Count())];
		auto leader = c->Leader();
		cs::String name = leader->Name;
		bool hand = c->Piloted; // its hand falls on the player's hull alone (the traffic out here runs as it did)
		cs::String text;
		switch (kind)
		{
		case WatcherSignKind::Phantom:
		{
			static const char* lines[] = {
				"the sensors hold a contact off the bow for eleven minutes: a hull of the flagship's own class, under its own name, that is not there",
				"a contact closes from astern on every scope at once, and when the ping goes out there is nothing within a light-hour",
				"the passive array reports a mass ahead larger than a star, and then reports nothing, and the log of it is blank",
			};
			text = cs::Cat("On the ", name, ", ", lines[rng.Next(3)]);
			break;
		}
		case WatcherSignKind::Whisper:
		{
			static const char* lines[] = {
				"a voice on a channel nobody keeps, reading out the crew's names in an order nobody can explain",
				"whispering in the hull itself, under the hum of the reactor, in no language, that every watch swears it understood",
				"the comms pick up the ship's own distress call, sent from a long way ahead, dated a week from now",
			};
			text = cs::Cat("On the ", name, ", ", lines[rng.Next(3)]);
			break;
		}
		case WatcherSignKind::StarsOut:
		{
			int n = 3 + rng.Next(40);
			text = cs::Cat("From the ", name, ", ", n, " stars go out in a line across the sky, one after another, as if something passed in front of them; they come back in the same order");
			break;
		}
		case WatcherSignKind::TimeSlip:
		{
			double lost = 0.02 + 0.06 * rng.NextDouble();
			int minutes = static_cast<int>(cs::Math::Round(lost * 1440));
			text = cs::Cat("Aboard the ", name, ", the chronometers disagree: ", minutes, " minutes are gone that nobody lived");
			if (hand && c->AtVoidStop(jd) && jd < c->EngageJd())
			{
				c->DelayCharge(lost);
				text = cs::Cat(text, ", and the drive's charge with them");
			}
			break;
		}
		case WatcherSignKind::Geometry:
		{
			text = cs::Cat("The ", name, "'s navigators plot a course whose angles will not add up, then cannot find where the error is: the stars are where they should be, and the way between them is not");
			if (hand && leader->Propellant() > 0)
			{
				leader->SetPropellant(leader->Propellant() * 0.97);
				text = cs::Cat(text, "; reaction mass is burned correcting it");
			}
			break;
		}
		case WatcherSignKind::Madness:
		{
			text = cs::Cat("A crewman on the ", name, " seals himself in the reactor bay and will not come out, saying that it can see in through the walls now");
			if (hand && leader->Combat != nullptr && leader->Combat->Spec != nullptr)
			{
				leader->Combat->Hull = cs::Math::Max(0.1 * leader->Combat->Spec->Hull, leader->Combat->Hull - 0.04 * leader->Combat->Spec->Hull);
				text = cs::Cat(text, "; the bay is cut open to bring him out");
			}
			break;
		}
		case WatcherSignKind::Herald:
		{
			text = cs::Cat("Every scope on the ", name, " turns toward the same empty patch of the void at once; out of it, something comes");
			if (hand)
			{
				// The heralds: a Void Raider sent to the hull it is looking at (US-T124 takes it from here).
				c->RaidStrikeJd = jd + VoidRaiders::StalkDays;
				c->RaidSeen = false;
			}
			break;
		}
		}
		c->WatcherSigns++;
		c->WatcherSign = text;
		// The deep-space log keeps the player's every sign, and a crew's once it is well past the first unease.
		if (hand || stage >= 2) space.Log(jd, text, c->PositionAt(jd), DeepSpaceEvent::WatcherSign); // US-T262: on the map's danger mode
		Learn(space, c, jd);
	}

	void VoidWatcher::Learn(DeepSpace& space, const cs::Ref<Cruise>& c, double jd)
	{
		space.WatcherSigns++;
		int stage = StageFor(space.WatcherSigns);
		while (space.WatcherLore < stage)
		{
			const auto& l = Lore()[space.WatcherLore];
			space.WatcherLore++;
			space.Log(jd, cs::Cat("The void watches: ", l.Title, ". ", l.Text), c->PositionAt(jd));
		}
	}
}
