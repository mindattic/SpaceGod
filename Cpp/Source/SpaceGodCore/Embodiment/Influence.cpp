// US-T140: the SpaceGod's Influence (see Embodiment/Influence.h).

#include "Embodiment/Influence.h"

#include "Combat/Battle.h"
#include "Comms/Pilots.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/RepairWorker.h"
#include "Embodiment/SatelliteSeat.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Polities.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(InfluenceSource v)
	{
		switch (v)
		{
		case InfluenceSource::Intervention: return "Intervention";
		case InfluenceSource::Mission: return "Mission";
		case InfluenceSource::Career: return "Career";
		case InfluenceSource::Dev: return "Dev";
		}
		return "?";
	}

	double SpaceGodInfluence::XpFor(int level)
	{
		int l = cs::Math::Max(1, cs::Math::Min(level, MaxLevel));
		return 50.0 * l * (l - 1);
	}

	int SpaceGodInfluence::Level() const
	{
		int l = 1;
		while (l < MaxLevel && Xp + 1e-9 >= XpFor(l + 1)) l++;
		return l;
	}

	double SpaceGodInfluence::Progress() const
	{
		int l = Level();
		if (l >= MaxLevel) return 1;
		double from = XpFor(l), to = XpFor(l + 1);
		return cs::Math::Max(0.0, cs::Math::Min(1.0, (Xp - from) / (to - from)));
	}

	cs::String SpaceGodInfluence::Title(int level)
	{
		static const char* titles[] = { "Whisper", "Murmur", "Voice", "Presence", "Hand", "Will", "Dominion", "Sovereign", "Ascendant", "Godhead" };
		return titles[cs::Math::Max(1, cs::Math::Min(level, MaxLevel)) - 1];
	}

	cs::Ref<SpaceGodInfluence> SpaceGodInfluence::Find(const cs::Ref<World>& world)
	{
		return world != nullptr && world->Polities != nullptr ? world->Polities->Influence : nullptr;
	}

	cs::Ref<SpaceGodInfluence> SpaceGodInfluence::Of(const cs::Ref<World>& world)
	{
		if (world == nullptr || world->Polities == nullptr) return nullptr;
		if (world->Polities->Influence == nullptr) world->Polities->Influence = cs::New<SpaceGodInfluence>();
		return world->Polities->Influence;
	}

	bool SpaceGodInfluence::Grant(const cs::Ref<World>& world, InfluenceSource source, double xp, const cs::String& why)
	{
		auto inf = Of(world);
		if (inf == nullptr || xp <= 0) return false;
		int was = inf->Level();
		inf->Xp += xp;
		switch (source)
		{
		case InfluenceSource::Intervention: inf->Interventions++; break;
		case InfluenceSource::Mission: inf->Missions++; break;
		case InfluenceSource::Career: inf->CareersSteered++; break;
		default: break;
		}
		inf->LastGain = why;
		inf->LastGainXp = xp;
		inf->GainSeq++;
		return inf->Level() > was;
	}

	int SpaceGodInfluence::TierOf(ShipClass c)
	{
		switch (c)
		{
		case ShipClass::Gunship:
		case ShipClass::Corvette: return 2;
		case ShipClass::Frigate:
		case ShipClass::Destroyer: return 3;
		case ShipClass::Cruiser: return 4;
		case ShipClass::Carrier:
		case ShipClass::Battleship: return 5;
		case ShipClass::CommandCruiser: return 6;
		case ShipClass::GateBreaker: return 7;
		default: return 1; // strike craft, shuttles, freighters, miners, couriers, explorers, tugs, tenders, pickets
		}
	}

	namespace
	{
		cs::String Lower(const cs::Ref<Ship>& s)
		{
			return s != nullptr && s->Spec != nullptr ? cs::Strings::ToLowerInvariant(s->Spec->DisplayName) : cs::String("hull");
		}

		/// The tier of the hull itself, and its name for the reason.
		int HullTier(const cs::Ref<Ship>& ship, cs::String& why)
		{
			if (ship == nullptr || ship->Spec == nullptr)
			{
				why = "a small craft";
				return 1;
			}
			why = cs::Cat("a ", Lower(ship));
			return SpaceGodInfluence::TierOf(ship->Spec->Class);
		}

		/// The named Epic entity <paramref name="ship"/> carries, if any, and what it adds to <paramref name="need"/>.
		void Epic(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, int& need, cs::String& why)
		{
			if (world == nullptr || ship == nullptr) return;
			// A feared outlaw's flagship (Outlaws: survived long at large with a long record).
			if (world->Outlaws != nullptr)
			{
				auto o = world->Outlaws->Of(ship);
				if (o != nullptr && o->Feared && o->Aboard() && o->Ship == ship)
				{
					need = cs::Math::Max(need + 3, 6);
					why = cs::Cat("the flagship of ", o->Title(), ", an Epic name");
					return;
				}
			}
			// A named pilot at the top of their ladder (US-T138): read only, never met here (the gate must not change the world).
			auto chars = world->Law != nullptr ? world->Law->Pilots : nullptr;
			auto p = chars != nullptr ? chars->Find(ship) : nullptr;
			if (p == nullptr) return;
			int top = Careers::TopRank(p->Track);
			bool epic = p->Track == CareerTrack::Pirate ? p->Rank >= top - 1 : p->Track != CareerTrack::Civilian && p->Rank >= top;
			if (!epic) return;
			need = cs::Math::Max(need + 2, 5);
			why = cs::Cat(p->Styled(), "'s ", Lower(ship), ", an Epic name");
		}
	}

	int SpaceGodInfluence::Needed(const cs::Ref<World>& world, const cs::Ref<IEmbodiable>& seat, cs::String& why)
	{
		why = cs::String();
		if (seat == nullptr) return 1;
		auto sub = seat->Subject();
		auto ship = sub.Ship != nullptr ? sub.Ship : sub.Unit != nullptr ? sub.Unit->Ship : nullptr;
		if (auto st = cs::As<StationSeat>(seat))
		{
			// A gate's fort is a capital's worth of guns; a settlement's defence platform a destroyer's.
			bool fort = st->Gate != nullptr;
			why = fort ? "a jump gate's fort" : "a defence platform";
			return fort ? 5 : 3;
		}
		if (cs::As<SatelliteSeat>(seat) != nullptr)
		{
			why = "a satellite";
			return 1;
		}
		int need = HullTier(ship, why);
		if (cs::As<MountedTurret>(seat) != nullptr || cs::As<RepairWorker>(seat) != nullptr)
		{
			// One gun, or one pair of hands, on the hull: less than the whole of it.
			need = cs::Math::Max(1, need - 2);
			why = cs::Cat(cs::As<MountedTurret>(seat) != nullptr ? "a gun on " : "a repair crew on ", why);
			return need;
		}
		if (cs::As<FighterSeat>(seat) != nullptr && ship == nullptr)
		{
			why = "a launched craft";
			return 1;
		}
		Epic(world, ship, need, why);
		return cs::Math::Min(need, MaxLevel);
	}

	// ================================================================ ObjectGraph registration (docs/PERSISTENCE.md)

	SG_SERIALIZE(SpaceGodInfluence, "SpaceGod.Core.SpaceGodInfluence",
		SG_F(Xp), SG_F(Interventions), SG_F(Missions), SG_F(CareersSteered))
}
