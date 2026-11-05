// Port of Unity.SpaceGod Core/Embodiment/StationSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/StationSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/SelfDestruct.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Fighters.h"
#include "Combat/PilotFlight.h"
#include "Combat/War.h"
#include "Economy/Settlement.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	StationSeat::StationSeat(const cs::Ref<World>& world, const cs::Ref<Core::Settlement>& settlement, const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& combat, const cs::Ref<Battle>& battle)
	{
		this->world = world;
		Settlement = settlement;
		Gate = gate;
		Combat = combat;
		seen = battle;
	}

	cs::Ref<World> StationSeat::WorldOf() const
	{
		// C#: Gate?.Home?.World ?? world
		if (Gate != nullptr && Gate->Home != nullptr && Gate->Home->World != nullptr) return Gate->Home->World;
		return world;
	}

	int StationSeat::Index() const
	{
		return Settlement != nullptr ? Settlement->Platforms.IndexOf(Combat) : Gate != nullptr ? Gate->Forts.IndexOf(Combat) : -1;
	}

	int StationSeat::Count() const
	{
		return Settlement != nullptr ? Settlement->Platforms.Count() : Gate != nullptr ? Gate->Forts.Count() : 0;
	}

	cs::String StationSeat::Name() const
	{
		cs::String what = Settlement != nullptr ? cs::Cat(Settlement->Name, " defence platform") : Gate != nullptr ? cs::Cat(Gate->Name, " fort") : cs::String("Station");
		int i = Index();
		return Count() > 1 && i >= 0 ? cs::Cat(what, " ", i + 1) : what;
	}

	cs::Ref<Core::Faction> StationSeat::Faction() const
	{
		cs::Ref<Battle> unusedBattle;
		auto u = Unit(unusedBattle);
		if (u != nullptr) return u->Faction;
		return Settlement != nullptr ? Settlement->Owner : (Gate ? Gate->Owner : nullptr);
	}

	bool StationSeat::Embodied() const { return Combat != nullptr && Combat->StationHeld; }

	cs::String StationSeat::Blocker() const
	{
		if (Combat == nullptr || (Settlement == nullptr && Gate == nullptr)) return "nothing there";
		cs::Ref<Battle> unusedBattle;
		auto u = Unit(unusedBattle);
		if (u != nullptr)
		{
			if (u->Dead || !Combat->Alive()) return Combat->SelfDestructed ? "self-destructed" : "destroyed";
			if (u->Captured) return "taken as a prize";
			if (!u->Present()) return "gone";
			return cs::String();
		}
		if (!Combat->Alive() || Index() < 0) return Combat->SelfDestructed ? "self-destructed" : "destroyed";
		return cs::String();
	}

	cs::Ref<BattleUnit> StationSeat::Unit(cs::Ref<Battle>& battle) const
	{
		battle = nullptr;
		if (Combat == nullptr) return nullptr;
		auto u = UnitIn(seen);
		if (u != nullptr)
		{
			battle = seen;
			return u;
		}
		auto w = WorldOf();
		if (w == nullptr || w->War == nullptr) return nullptr;
		for (const auto& b : w->War->Battles)
		{
			if (b == seen) continue;
			u = UnitIn(b);
			if (u != nullptr)
			{
				battle = b;
				return u;
			}
		}
		return nullptr;
	}

	cs::Ref<BattleUnit> StationSeat::UnitIn(const cs::Ref<Battle>& b) const
	{
		if (b == nullptr || b->Over) return nullptr;
		for (const auto& u : b->Units)
			if (u->Combat == Combat && u->Kind == UnitKind::Platform) return u;
		return nullptr;
	}

	std::tuple<int, DVec3, DVec3> StationSeat::RestingPlace(const cs::Ref<World>& world, const cs::Ref<Core::Settlement>& settlement, const cs::Ref<JumpGate>& gate, int index, int count)
	{
		double a = Astro::TwoPi * cs::Math::Max(0, index) / cs::Math::Max(1, count);
		auto round = DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0);
		if (gate != nullptr)
		{
			// C#: gate.Home?.World ?? world
			auto w = gate->Home != nullptr && gate->Home->World != nullptr ? gate->Home->World : world;
			int root = w != nullptr ? w->Catalog()->Root()->Index : 0;
			return { root, gate->PositionKm + round * 6, round };
		}
		int body = settlement ? settlement->Body : 0;
		double r = world != nullptr && body >= 0 && body < world->Catalog()->Count() ? Battle::AnchorDistanceKm((*world->Catalog())[body]) : 6;
		return { body, round * r, round };
	}

	EmbodimentPose StationSeat::PoseAt(double /*jd*/)
	{
		cs::Ref<Battle> b;
		auto u = Unit(b);
		if (u != nullptr)
		{
			DVec3 unusedRight, up;
			u->Basis(unusedRight, up);
			return EmbodimentPose(b->Body, u->Pos + b->Anchor, u->Forward, up);
		}
		auto [body, offset, forward] = RestingPlace(world, Settlement, Gate, Index(), Count());
		return EmbodimentPose(body, offset, forward, PilotState::LevelUp(forward));
	}

	cs::List<cs::Ref<Subsystem>> StationSeat::Guns() const
	{
		cs::List<cs::Ref<Subsystem>> guns;
		for (const auto& s : Combat->Subsystems)
			if (MountedTurret::IsMannable(s->Def) && !s->Destroyed()) guns.Add(s);
		return guns;
	}

	void StationSeat::TakeCharge()
	{
		Combat->StationHeld = true;
		secondaryWas = false;
		cs::Ref<Battle> b;
		auto u = Unit(b);
		if (u != nullptr) Man(b, u);
	}

	cs::Ref<MountControl> StationSeat::Man(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<MountControl> hand = nullptr;
		for (const auto& s : Guns())
		{
			cs::Ref<MountControl> m;
			if (b->Manned.TryGetValue(s, m) && m->ByStation)
			{
				hand = m;
				break;
			}
		}
		if (!hand)
		{
			hand = cs::New<MountControl>();
			hand->ByStation = true;
		}
		for (const auto& s : Guns())
		{
			if (s->Aim.LengthSquared() <= 1e-12) s->Aim = MountedTurret::RestAim(u, s->Def);
			b->Manned[s] = hand;
		}
		return hand;
	}

	void StationSeat::Control(const EmbodimentInput& input, double dt)
	{
		if (!Embodied()) return;
		cs::Ref<Battle> b;
		auto u = Unit(b);
		// The self-destruct (US-T97): a station's scuttle, the longest fuse and the biggest blast. Armed or aborted here; the
		// sim counts it down (US-T114).
		if (SelfDestruct::AtControls(Combat, input.SelfDestruct, SelfDestruct::FuseSeconds(Combat, false, true)))
		{
			SetOff(b, u);
			return;
		}
		if (u != nullptr)
		{
			auto hand = Man(b, u);
			if (input.Aim.LengthSquared() > 1e-12) hand->Aim = input.Aim.Normalized();
			hand->Trigger = input.Primary;
			if (input.Secondary && !secondaryWas) LaunchCraft();
		}
		else if (input.Aim.LengthSquared() > 1e-12) TrainGuns(input.Aim.Normalized(), dt);
		secondaryWas = input.Secondary;
	}

	void StationSeat::SetOff(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		// In a fight it goes as a platform shot to pieces does (struck off its settlement's or gate's list when the fight is
		// settled); out of one it is struck off here, where it stood.
		if (u != nullptr && u->Present())
		{
			SelfDestruct::InBattle(b, u);
			return;
		}
		auto pose = PoseAt(0);
		auto name = Name();
		SelfDestruct::Outside(WorldOf(), Combat, name, Faction(), nullptr, pose.FrameBody, pose.OffsetKm, pose.Forward);
		if (Settlement != nullptr) Settlement->Platforms.Remove(Combat);
		if (Gate != nullptr) Gate->Forts.Remove(Combat);
	}

	void StationSeat::TrainGuns(DVec3 aim, double dt)
	{
		auto pose = PoseAt(0);
		auto standIn = cs::New<BattleUnit>();
		standIn->Kind = UnitKind::Platform;
		standIn->Combat = Combat;
		standIn->Faction = Faction();
		standIn->LengthKm = Combat->Spec->LengthKm;
		standIn->Forward = pose.Forward;
		standIn->Roof = pose.Up;
		for (const auto& s : Guns())
		{
			auto current = s->Aim.LengthSquared() > 1e-12 ? s->Aim.Normalized() : MountedTurret::RestAim(standIn, s->Def);
			auto want = MountedTurret::Bound(standIn, s->Def, aim);
			s->Aim = PilotFlight::ClampAim(want, current, MountedTurret::TraverseDegS(s->Kind()) * Astro::Deg2Rad * dt);
		}
	}

	int StationSeat::LaunchCraft()
	{
		cs::Ref<Battle> b;
		auto u = Unit(b);
		if (u == nullptr || Combat->Fighters() <= 0) return 0;
		int before = b->Units.Count();
		FighterAI::Launch(b, u);
		return b->Units.Count() - before;
	}

	void StationSeat::HandBack()
	{
		if (Combat == nullptr) return;
		SelfDestruct::Disarm(Combat); // US-T97: its crew carries no countdown on
		Combat->StationHeld = false;
		secondaryWas = false;
		cs::Ref<Battle> b;
		auto u = Unit(b);
		if (u == nullptr) return;
		for (const auto& s : Combat->Subsystems)
		{
			cs::Ref<MountControl> m;
			if (b->Manned.TryGetValue(s, m) && m->ByStation) b->Manned.Remove(s);
		}
	}

	bool StationSeat::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto s = cs::As<StationSeat>(obj);
		return s && s->Combat == Combat;
	}

	size_t StationSeat::GetHashCode() const { return Combat ? cs::Hash<cs::Ref<ShipCombat>>{}(Combat) : 0; }
}
