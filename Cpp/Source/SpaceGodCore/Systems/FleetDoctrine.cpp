// Port of Assets/UnitySpace/Core/Systems/FleetDoctrine.cs (see docs/MIGRATION.md).

#include "Systems/FleetDoctrine.h"

#include "DVec3.h"
#include "Fleet.h"
#include "GalacticPosition.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/GatePickets.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	const char* ToString(ForceKind v)
	{
		switch (v)
		{
		case ForceKind::GateFleet: return "GateFleet";
		case ForceKind::JumpStrikeGroup: return "JumpStrikeGroup";
		}
		return "";
	}

	const char* ToString(CampaignMission v)
	{
		switch (v)
		{
		case CampaignMission::Invade: return "Invade";
		case CampaignMission::SecondFront: return "SecondFront";
		case CampaignMission::Relieve: return "Relieve";
		case CampaignMission::Raid: return "Raid";
		}
		return "";
	}

	const cs::Ref<ToggleSetting>& FleetDoctrine::StrikeGroups()
	{
		static const cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("doctrine.strikeGroups", SettingGroups::Ai,
			"Jump strike groups", true,
			cs::Cat("Navies at war send their jump strike groups (a carrier, FTL capitals and escorts clamped to the carrier) under their own drives ",
				"to relieve their own systems, open a second front or raid, besides marching their gate fleets through the gates."),
			"doctrine strike group carrier ftl jump raid relieve second front");
		return s;
	}

	const cs::Ref<RangeSetting>& FleetDoctrine::ReserveShare()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("doctrine.reserveShare", SettingGroups::Ai,
			"Reserve against strike groups", 1, 0, 3, 0.25,
			cs::Cat("How much warship strength a navy keeps at home in every system it holds, against an enemy with jump strike groups, as a multiple ",
				"of the strongest such group: no campaign is sent that would leave less. 0 keeps no reserve."),
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }), "doctrine reserve defence strike group home");
		return s;
	}

	const cs::Ref<RangeSetting>& FleetDoctrine::StrikeRangeLy()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("doctrine.strikeRange", SettingGroups::Ai,
			"Strike group range", 20, 1, 200, 1,
			"How far (light-years) a navy sends a jump strike group under its own drive on a raid, a relief or a second front.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " ly"); }), "doctrine strike group range ftl");
		return s;
	}

	const cs::Ref<RangeSetting>& FleetDoctrine::StrikeDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("doctrine.strikeDays", SettingGroups::Ai,
			"Strike group mission length", 10, 1, 60, 1,
			"How many days a jump strike group falls on an enemy system's ports, or fights to relieve one of its own, before it jumps out for home.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " days"); }), "doctrine strike group raid relieve");
		return s;
	}

	const cs::List<double>& FleetDoctrine::flanks()
	{
		static const cs::List<double> f{ 0, 30, -30, 60, -60, 90, -90 };
		return f;
	}

	// ---------------------------------------------------------------- what a group is

	bool FleetDoctrine::IsCarrier(cs::Ref<Ship> ship) { return FtlCarriage::ClampsOf(ship) > 0; }

	ForceKind FleetDoctrine::KindOf(cs::Ref<World> world, cs::Ref<Ship> leader)
	{
		if (leader == nullptr || CruiseDrives::Of(leader) == nullptr) return ForceKind::GateFleet;
		cs::List<cs::Ref<Ship>> drives{ leader };
		cs::List<cs::Ref<Ship>> craft;
		bool carrier = IsCarrier(leader);
		if (world != nullptr)
			for (const auto& e : world->Fleet->Ships())
			{
				if (e->Leader != leader || e == leader || e->Destroyed || e->Derelict || !FtlRoster::LeavesHome(e)) continue;
				if (CruiseDrives::Of(e) != nullptr)
				{
					drives.Add(e);
					carrier |= IsCarrier(e);
				}
				else craft.Add(e);
			}
		if (!carrier) return ForceKind::GateFleet;
		auto aboard = FtlCarriage::Stow(drives, craft);
		for (const auto& c : craft)
			if (!aboard.ContainsKey(c)) return ForceKind::GateFleet;
		return ForceKind::JumpStrikeGroup;
	}

	bool FleetDoctrine::IsStrikeGroup(cs::Ref<World> world, cs::Ref<Ship> leader) { return KindOf(world, leader) == ForceKind::JumpStrikeGroup; }

	bool FleetDoctrine::Leads(cs::Ref<World> w, cs::Ref<Ship> s)
	{
		return !s->Destroyed && !s->IsEscort() && !s->Derelict && s->Spec->IsMilitary && !s->PatrolWing && s->PicketGate == nullptr && !s->Convoy &&
			s != w->PilotedShip && w->Fleet->IsPresent(s);
	}

	// ---------------------------------------------------------------- reserves

	double FleetDoctrine::StrikeThreat(cs::Ref<Galaxy> galaxy, cs::Ref<Faction> f)
	{
		if (galaxy == nullptr || f == nullptr) return 0;
		auto dip = galaxy->Polities->Diplomacy;
		double worst = 0;
		for (const auto& s : galaxy->Systems)
		{
			auto w = s->World;
			for (const auto& ship : w->Fleet->Ships())
			{
				if (ship->Faction == f || ship->Faction == nullptr || ship->Faction->IsPirate || dip->Get(f, ship->Faction) != Relation::War) continue;
				if (!Leads(w, ship) || CruiseDrives::Of(ship) == nullptr || !IsStrikeGroup(w, ship)) continue;
				worst = cs::Math::Max(worst, w->War->GroupStrength(ship));
			}
		}
		for (const auto& c : galaxy->DeepSpace->Cruises())
		{
			auto lead = c->Leader();
			if (lead == nullptr || lead->Faction == f || lead->Faction == nullptr || lead->Faction->IsPirate || !lead->Spec->IsMilitary) continue;
			if (dip->Get(f, lead->Faction) != Relation::War) continue;
			bool carrier = false;
			double strength = 0;
			for (const auto& s : c->Ships)
				if (!s->Destroyed && s->Combat != nullptr)
				{
					strength += s->Combat->Strength();
					carrier |= IsCarrier(s);
				}
			if (carrier) worst = cs::Math::Max(worst, strength);
		}
		return worst;
	}

	double FleetDoctrine::ReserveWanted(cs::Ref<World> world, cs::Ref<Faction> f, double threat)
	{
		if (threat <= 0 || ReserveShare()->Value <= 0 || world == nullptr) return 0;
		for (const auto& s : world->Economy->Settlements)
			if (s->Owner == f && !s->Fallen && s->Kind != SettlementKind::Haven) return threat * ReserveShare()->Value;
		return 0;
	}

	bool FleetDoctrine::LeavesReserve(cs::Ref<World> world, cs::Ref<Ship> leader, cs::Ref<Faction> f, double threat)
	{
		double wanted = ReserveWanted(world, f, threat);
		if (wanted <= 0) return true;
		double left = 0;
		for (const auto& s : world->Fleet->Ships())
			if (s != leader && s->Faction == f && s->Voyage == nullptr && Leads(world, s)) left += world->War->GroupStrength(s);
		return left >= wanted;
	}

	// ---------------------------------------------------------------- jumping in

	DVec3 FleetDoctrine::DropDirection(cs::Ref<StarSystem> to, GalacticPosition from, cs::Ref<Faction> f)
	{
		auto back = from.KmFrom(to->Origin);
		auto a = back.LengthSquared() > 0 ? back.Normalized() : DVec3::UnitX;
		auto watch = Watchers(to, f);
		if (watch.Count() == 0) return a;
		auto axis = cs::Math::Abs(a.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto p = DVec3::Cross(axis, a).Normalized();
		double wellKm = CruiseDrives::WellLimitKm(to);
		auto best = a;
		double bestClear = -1;
		for (double deg : flanks())
		{
			if (cs::Math::Abs(deg) > MaxFlankDegrees) continue;
			double r = deg * cs::Math::PI / 180;
			auto dir = (a * cs::Math::Cos(r) + p * cs::Math::Sin(r)).Normalized();
			auto at = dir * wellKm;
			double clear = cs::Double::MaxValue;
			for (const auto& w : watch) clear = cs::Math::Min(clear, (w - at).Length());
			// Strictly better only: the straight way in wins a tie.
			if (clear > bestClear + 1)
			{
				bestClear = clear;
				best = dir;
			}
		}
		return best;
	}

	cs::List<DVec3> FleetDoctrine::Watchers(cs::Ref<StarSystem> to, cs::Ref<Faction> f)
	{
		cs::List<DVec3> list;
		auto w = to->World;
		for (const auto& g : to->Gates())
		{
			if (g->Owner != nullptr && g->Owner != f && w->Diplomacy()->Hostile(f, g->Owner)) list.Add(g->PositionKm);
			for (const auto& p : GatePickets::Of(g))
				if (p->Faction != f && w->Diplomacy()->Hostile(f, p->Faction)) list.Add(p->PicketPostKm);
		}
		return list;
	}

	cs::String FleetDoctrine::Describe(cs::Ref<Campaign> c)
	{
		if (c == nullptr) return "";
		if (c->Force == ForceKind::JumpStrikeGroup)
		{
			switch (c->Mission)
			{
			case CampaignMission::Relieve: return "jump strike group sent to relieve";
			case CampaignMission::Raid: return "jump strike group raiding";
			case CampaignMission::SecondFront: return "jump strike group opening a second front against";
			default: return "jump strike group sent against";
			}
		}
		return "war fleet sent against";
	}
}
