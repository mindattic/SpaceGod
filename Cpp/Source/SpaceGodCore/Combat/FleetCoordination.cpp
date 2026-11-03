// Port of Unity.SpaceGod Core/Combat/FleetCoordination.cs (US-T10).

#include "Combat/FleetCoordination.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	void FleetCoordination::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		ReachKm();
		AccuracyPerComputer();
		RetargetCut();
		Focus();
		PicturePerArray();
		DisruptionPenalty();
		DisruptionSeconds();
		Priority();
	}

	cs::String FleetCoordination::Km(double v) { return cs::Cat(cs::Fmt(v, "0"), " km"); }

	cs::String FleetCoordination::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	const cs::Ref<RangeSetting>& FleetCoordination::ReachKm()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.reachKm", SettingGroups::Broadsides,
			"Command reach", 20, 0, 60, 1,
			cs::Cat("How far (km) a command ship's communications suite reaches in a battle: every ship on its side within it fights ",
				"coordinated. Each further suite fitted adds half again."), Km, "command cruiser battlecruiser coordination reach range comms flagship");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::AccuracyPerComputer()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.accuracy", SettingGroups::Broadsides,
			"Coordinated accuracy", 0.08, 0, 0.3, 0.01,
			cs::Cat("How much surer every coordinated ship's guns are for each targeting computer its command ship carries (a command ",
				"cruiser carries two)."), Percent, "command cruiser targeting computers accuracy coordination fire control");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::RetargetCut()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.retarget", SettingGroups::Broadsides,
			"Coordinated targeting", 0.5, 0, 0.9, 0.05,
			"How much sooner a coordinated ship's gunners take up a new target, the command ship's staff having already picked it out.",
			Percent, "command cruiser targeting retarget coordination fleet");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::Focus()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.focus", SettingGroups::Broadsides,
			"Coordinated focus fire", 10, 0, 40, 1,
			cs::Cat("How strongly coordinated ships concentrate on the hull their command ship is fighting (as if it were that many km ",
				"nearer than it is)."), Km, "command cruiser focus fire concentrate coordination target");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::PicturePerArray()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.picture", SettingGroups::Broadsides,
			"Shared sensor picture", 0.4, 0, 1, 0.05,
			cs::Cat("How much of a target's tracking error and range falloff a coordinated ship's gunners lose for each sensor array its ",
				"command ship carries: they lay their guns on the shared picture, not their own."), Percent,
			"command cruiser sensor array picture tracking coordination sensors");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::DisruptionPenalty()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.disruption", SettingGroups::Broadsides,
			"Loss of command", 0.2, 0, 0.6, 0.05,
			cs::Cat("How much worse the ships a command ship was coordinating shoot, and how much slower they find their targets, for a ",
				"while after it is lost."), Percent, "command cruiser loss disruption confusion flagship lost coordination");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::DisruptionSeconds()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.disruptionSeconds", SettingGroups::Broadsides,
			"Loss of command lasts", 90, 0, 600, 10,
			"How long (seconds) the ships a lost command ship was coordinating stay in confusion, unless another command ship takes them over.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }, "command cruiser loss disruption confusion time");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetCoordination::Priority()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("command.priority", SettingGroups::Broadsides,
			"Command ship targeting", 15, 0, 60, 1,
			cs::Cat("How strongly hostile gunners favour a command ship over other targets (as if it were that many km nearer than it is): ",
				"sink it and its fleet fights blind."), Km, "command cruiser priority target targeting focus battlecruiser");
		return value;
	}

	// ---------------------------------------------------------------- what a hull carries

	int FleetCoordination::Count(const cs::Ref<Ship>& s, const cs::String& id)
	{
		if (s == nullptr || s->Spec == nullptr) return 0;
		if (s->Loadout != nullptr) return s->Loadout->CountFacility(id);
		int n = 0;
		for (const auto& f : FacilityLayouts::For(s->Spec->Class))
			if (cs::Strings::EqualsOrdinalIgnoreCase(f->Kind, id)) n++;
		return n;
	}

	bool FleetCoordination::IsCommandShip(const cs::Ref<Ship>& s) { return Count(s, FacilityKinds::CommsSuite) > 0; }

	bool FleetCoordination::IsCommandClass(ShipClass cls)
	{
		for (const auto& f : FacilityLayouts::For(cls))
			if (cs::Strings::EqualsOrdinalIgnoreCase(f->Kind, FacilityKinds::CommsSuite)) return true;
		return false;
	}

	// ---------------------------------------------------------------- in battle

	bool FleetCoordination::Coordinates(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Present() && u->Kind == UnitKind::Ship && u->Ship != nullptr && !u->Captured && !u->Surrendering && u->Stunned <= 0
			&& u->Combat != nullptr && !u->Combat->Disabled() && u->Combat->BridgeIntact() && IsCommandShip(u->Ship);
	}

	double FleetCoordination::ReachOf(const cs::Ref<BattleUnit>& commander)
	{
		int suites = commander == nullptr || commander->Ship == nullptr ? 0 : Count(commander->Ship, FacilityKinds::CommsSuite);
		return suites <= 0 ? 0 : ReachKm()->Value * (1 + 0.5 * (suites - 1));
	}

	void FleetCoordination::Step(const cs::Ref<Battle>& b, double dt)
	{
		// C# holds null until the first commander; an empty list stands in for it.
		cs::List<cs::Ref<BattleUnit>> commanders;
		bool anyCommanders = false;
		for (const auto& u : b->Units)
			if (Coordinates(u)) { anyCommanders = true; commanders.Add(u); }
		for (const auto& u : b->Units)
		{
			if (!u->Present()) continue;
			cs::Ref<BattleUnit> best = nullptr;
			double bestKm = cs::Double::MaxValue;
			if (anyCommanders)
				for (const auto& c : commanders)
				{
					if (c->Side != u->Side) continue;
					double d = (c->Pos - u->Pos).Length();
					if (d <= ReachOf(c) && d < bestKm)
					{
						bestKm = d;
						best = c;
					}
				}
			if (best != nullptr) u->Disrupted = 0;
			else if (u->CommandedBy != nullptr && !Coordinates(u->CommandedBy)) u->Disrupted = DisruptionSeconds()->Value;
			else if (u->Disrupted > 0) u->Disrupted = cs::Math::Max(0.0, u->Disrupted - dt);
			u->CommandedBy = best;
		}
	}

	double FleetCoordination::AccuracyFactor(const cs::Ref<BattleUnit>& u)
	{
		if (u != nullptr && u->CommandedBy != nullptr && u->CommandedBy->Ship != nullptr) return 1 + AccuracyPerComputer()->Value * Count(u->CommandedBy->Ship, FacilityKinds::TargetingComputers);
		return u != nullptr && u->Disrupted > 0 ? cs::Math::Max(0.0, 1 - DisruptionPenalty()->Value) : 1;
	}

	double FleetCoordination::RetargetFactor(const cs::Ref<BattleUnit>& u)
	{
		if (u != nullptr && u->CommandedBy != nullptr) return cs::Math::Max(0.05, 1 - RetargetCut()->Value);
		return u != nullptr && u->Disrupted > 0 ? 1 + DisruptionPenalty()->Value : 1;
	}

	double FleetCoordination::SharedPicture(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr || u->CommandedBy == nullptr || u->CommandedBy->Ship == nullptr) return 0;
		return cs::Math::Min(MaxPicture, PicturePerArray()->Value * Count(u->CommandedBy->Ship, FacilityKinds::SensorArray));
	}

	cs::Ref<BattleUnit> FleetCoordination::FocusTarget(const cs::Ref<BattleUnit>& u)
	{
		auto c = u ? u->CommandedBy : nullptr;
		return c == nullptr || c == u ? nullptr : c->Target;
	}
}
