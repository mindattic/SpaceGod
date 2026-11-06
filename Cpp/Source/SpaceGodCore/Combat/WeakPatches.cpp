// Port of Assets/UnitySpace/Core/Combat/WeakPatches.cs (see docs/MIGRATION.md).

#include "Combat/WeakPatches.h"

#include "Astro.h"
#include "Combat/ArmourAngling.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/Choreography.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	bool HullPatch::Weak() const { return Holed || Weakness >= WeakPatches::WeakAt; }

	// ------------------------------------------------------------ settings

	const cs::Ref<ToggleSetting>& WeakPatches::AiRolls()
	{
		static const cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("broadside.aiRollPatches", SettingGroups::Broadsides,
			"AI captains roll weak patches away", true,
			"AI capital ships roll and turn their hulls to keep damaged, unshielded or holed patches of hull away from the heaviest incoming fire, presenting fresh shields instead.",
			"roll weak patch shield emitter holed ai doctrine capital 3d");
		return s;
	}

	const cs::Ref<RangeSetting>& WeakPatches::Protection()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("broadside.patchProtection", SettingGroups::Broadsides,
			"Weak patch protection", 1, 0, 3, 0.1,
			"How much of its batteries' bearing a captain gives up to keep its weak patches out of the heaviest fire: at 1 it would trade its whole broadside to turn a fully holed face away, at 0 it ignores its patches and lays its guns as before.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.#") + "x"; }), "roll weak patch shield emitter holed protect trade batteries bearing capital");
		return s;
	}

	// ------------------------------------------------------------ who rolls

	bool WeakPatches::HasPatches(const cs::Ref<ShipCombat>& c)
	{
		int n = 0;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::ShieldGenerator && ++n >= 2) return true;
		return false;
	}

	bool WeakPatches::Steers(const cs::Ref<BattleUnit>& u)
	{
		return AiRolls()->Value && Protection()->Value > 0 && Broadsides::Commands(u) && !u->PlayerControlled() && HasPatches(u->Combat);
	}

	// ------------------------------------------------------------ the patches

	double WeakPatches::Weakness(const cs::Ref<Subsystem>& emitter)
	{
		if (emitter->Destroyed()) return 1;
		return cs::Math::Min(1.0, 1 - emitter->ShieldFraction() + EmitterDamageWeight * (1 - emitter->Health()));
	}

	cs::String WeakPatches::NameOf(const cs::Ref<CombatSpec>& spec, const cs::Ref<SubsystemDef>& d)
	{
		// An empty string stands for C#'s null here: every real name is non-empty.
		double r = spec->BeamFraction * 0.5, core = cs::Math::Max(0.05, 0.5 - r);
		cs::String along = d->Z > core * 0.4 ? "bow" : d->Z < -core * 0.4 ? "stern" : "";
		double ax = cs::Math::Abs(d->X), ay = cs::Math::Abs(d->Y);
		cs::String side = cs::Math::Max(ax, ay) > r * 0.5 ? (ax >= ay ? (d->X > 0 ? "starboard" : "port") : (d->Y > 0 ? "dorsal" : "ventral")) : "";
		if (side.empty()) return !along.empty() ? along : cs::String("keel");
		bool beam = side == "starboard" || side == "port";
		if (along.empty()) return beam ? side + " beam" : side;
		return beam ? side + (along == "bow" ? " bow" : " quarter") : side + " " + along;
	}

	// ------------------------------------------------------------ the fire

	double WeakPatches::ReachKm(const cs::Ref<ShipCombat>& c)
	{
		double reach = 0;
		for (const auto& s : c->Subsystems)
		{
			const auto& w = s->Def->Weapon;
			if (w == nullptr || s->Destroyed() || w->Supporting()) continue;
			reach = cs::Math::Max(reach, w->RangeKm);
		}
		return reach;
	}

	cs::List<IncomingFire> WeakPatches::IncomingFireOn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::List<IncomingFire> list;
		for (const auto& o : b->Units)
		{
			if (o == u || !o->Present() || !b->Hostile(u, o) || o->Captured || o->Surrendering || o->Stunned > 0) continue;
			auto c = o->Combat;
			if (c->Disabled() || !c->Armed()) continue;
			auto to = o->Pos - u->Pos;
			double d = to.Length();
			if (d < 1e-9) continue;
			double w = c->Strength();
			if (w <= 0) continue;
			double reach = ReachKm(c);
			if (reach > 0 && d > reach) w *= reach / d * (reach / d);
			auto cmd = o->Broadside;
			if (cmd != nullptr && cmd->ReadyingOn == u) w *= 1 + cmd->ReadyingLoaded;
			list.Add(IncomingFire(to / d, w));
		}
		return list;
	}

	// ------------------------------------------------------------ exposure

	cs::List<WeakPatches::Sample> WeakPatches::SamplesOf(const cs::Ref<CombatSpec>& spec)
	{
		constexpr int around = 8, stations = 4;
		double r = spec->BeamFraction * 0.5, core = cs::Math::Max(0.0, 0.5 - r);
		cs::List<Sample> list; // capacity around * stations + 2 * (around + 1)
		double side = 2 * cs::Math::PI * r * 2 * core / (around * stations);
		if (side > 0)
			for (int i = 0; i < stations; i++)
			{
				double z = -core + (i + 0.5) * 2 * core / stations;
				for (int k = 0; k < around; k++)
				{
					double a = 2 * cs::Math::PI * k / around;
					auto n = DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0);
					Sample smp;
					smp.Point = DVec3(0, 0, z) + n * r;
					smp.Normal = n;
					smp.Area = side;
					list.Add(smp);
				}
			}
		double cap = 2 * cs::Math::PI * r * r / (around + 1);
		for (int sign = -1; sign <= 1; sign += 2)
		{
			auto end = DVec3(0, 0, sign * core);
			auto tip = DVec3(0, 0, sign);
			Sample first;
			first.Point = end + tip * r;
			first.Normal = tip;
			first.Area = cap;
			list.Add(first);
			double h = cs::Math::Sqrt(0.5);
			for (int k = 0; k < around; k++)
			{
				double a = 2 * cs::Math::PI * k / around;
				auto n = DVec3(cs::Math::Cos(a) * h, cs::Math::Sin(a) * h, sign * h);
				Sample smp;
				smp.Point = end + n * r;
				smp.Normal = n;
				smp.Area = cap;
				list.Add(smp);
			}
		}
		return list.ToArray();
	}

	cs::List<int> WeakPatches::PatchesOf(const cs::Ref<ShipCombat>& c, const cs::List<Sample>& samples)
	{
		auto patch = cs::NewArray<int>(samples.Count());
		for (int i = 0; i < samples.Count(); i++)
		{
			auto at = c->PatchAt(samples[i].Point);
			patch[i] = at ? at->Index : -1;
		}
		return patch;
	}

	cs::List<double> WeakPatches::WeaknessOf(const cs::Ref<ShipCombat>& c, const cs::List<int>& patch)
	{
		auto weak = cs::NewArray<double>(patch.Count());
		for (int i = 0; i < patch.Count(); i++) weak[i] = patch[i] < 0 ? 0 : Weakness(c->Subsystems[patch[i]]);
		return weak;
	}

	double WeakPatches::Exposure(const cs::List<Sample>& samples, const cs::List<double>& weak, const cs::List<IncomingFire>& fire, DVec3 right, DVec3 up,
		DVec3 forward)
	{
		double total = 0, weights = 0;
		for (const auto& f : fire)
		{
			auto d = DVec3(DVec3::Dot(f.Dir, right), DVec3::Dot(f.Dir, up), DVec3::Dot(f.Dir, forward));
			double face = 0, bad = 0;
			for (int i = 0; i < samples.Count(); i++)
			{
				double k = DVec3::Dot(samples[i].Normal, d);
				if (k <= 0) continue;
				k *= samples[i].Area;
				face += k;
				bad += k * weak[i];
			}
			if (face <= 0) continue;
			total += f.Weight * bad / face;
			weights += f.Weight;
		}
		return weights > 0 ? total / weights : 0;
	}

	double WeakPatches::Exposure(const cs::Ref<BattleUnit>& u, const cs::List<IncomingFire>& fire)
	{
		auto samples = SamplesOf(u->Combat->Spec);
		auto weak = WeaknessOf(u->Combat, PatchesOf(u->Combat, samples));
		DVec3 right, up;
		u->Basis(right, up);
		return Exposure(samples, weak, fire, right, up, u->Forward);
	}

	cs::List<HullPatch> WeakPatches::Readout(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto c = u->Combat;
		cs::List<HullPatch> list;
		if (!HasPatches(c)) return list;
		auto samples = SamplesOf(c->Spec);
		auto patch = PatchesOf(c, samples);
		auto fire = IncomingFireOn(b, u);
		DVec3 right, up;
		u->Basis(right, up);
		auto share = cs::NewArray<double>(c->Subsystems.Count());
		double all = 0;
		for (const auto& f : fire)
		{
			auto d = DVec3(DVec3::Dot(f.Dir, right), DVec3::Dot(f.Dir, up), DVec3::Dot(f.Dir, u->Forward));
			for (int i = 0; i < samples.Count(); i++)
			{
				double k = DVec3::Dot(samples[i].Normal, d);
				if (k <= 0 || patch[i] < 0) continue;
				k *= samples[i].Area * f.Weight;
				share[patch[i]] += k;
				all += k;
			}
		}
		cs::Dictionary<cs::String, int> names;
		for (const auto& s : c->Subsystems)
		{
			if (s->Kind() != SubsystemKind::ShieldGenerator) continue;
			cs::String name = NameOf(c->Spec, s->Def);
			int seen = 0;
			names.TryGetValue(name, seen);
			names[name] = seen + 1;
			if (seen > 0) name += cs::Cat(" ", seen + 1);
			list.Add(HullPatch(s, name, s->ShieldFraction(), s->Destroyed(), Weakness(s), all > 0 ? share[s->Index] / all : 0));
		}
		return list;
	}

	// ------------------------------------------------------------ the attitude

	DVec3 WeakPatches::LevelRoof(DVec3 forward)
	{
		if (forward.LengthSquared() < 1e-18) return DVec3::UnitZ;
		auto f = forward.Normalized();
		// Continuous up to the pole (U.SPC-T562); X only when the nose is exactly along it.
		auto upRef = DVec3::Cross(f, DVec3::UnitZ).LengthSquared() > 1e-12 ? DVec3::UnitZ : DVec3::UnitX;
		auto right = DVec3::Cross(f, upRef).Normalized();
		return DVec3::Cross(right, f);
	}

	void WeakPatches::Frame(DVec3 face, cs::Nullable<DVec3> toTarget, int bearing, double rollDeg, DVec3& forward, DVec3& roof)
	{
		DVec3 f0, axis;
		if (bearing != NoBearing && toTarget.has_value())
		{
			DVec3 t = *toTarget;
			axis = t.Normalized();
			f0 = Battle::BowFor(axis, bearing).Normalized();
		}
		else
		{
			f0 = face.Normalized();
			axis = f0;
		}
		double a = rollDeg * Astro::Deg2Rad;
		forward = PilotFlight::RotateAbout(f0, axis, a).Normalized();
		roof = PilotFlight::RotateAbout(LevelRoof(f0), axis, a).Normalized();
	}

	void WeakPatches::Orient(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 face, const cs::Ref<cs::Dictionary<int, double>>& bearings,
		cs::Nullable<DVec3> toTarget, cs::Nullable<DVec3> rest, double dt, DVec3& forward, DVec3& roof, cs::Nullable<DVec3> actual)
	{
		if (face.LengthSquared() < 1e-18) face = u->Forward;
		auto cmd = Broadsides::Of(u);
		bool aiming = bearings != nullptr && bearings->Count() > 0 && toTarget.has_value() && toTarget->LengthSquared() > 1e-18;
		if (cmd == nullptr || !HasPatches(u->Combat))
		{
			forward = face.Normalized();
			roof = LevelRoof(forward);
			return;
		}
		cmd->RollReconsider -= dt;
		bool valid = cmd->RollChosen && (aiming ? bearings->ContainsKey(cmd->RollBearing) : cmd->RollBearing == NoBearing);
		if (!valid || cmd->RollReconsider <= 0)
		{
			cmd->RollReconsider = ReconsiderSeconds;
			Choose(b, u, cmd, face, aiming ? bearings.get() : nullptr, aiming ? toTarget : cs::Nullable<DVec3>(), rest, actual);
		}
		Frame(face, aiming ? toTarget : cs::Nullable<DVec3>(), aiming ? cmd->RollBearing : NoBearing, cmd->RollDeg, forward, roof);
		// The roof is carried over to the heading the hull actually lies on, to roll it there; the bow is still turned
		// toward the heading it wants, or a hull still coming round would never get there (U.SPC-T534).
		auto wanted = forward;
		Transport(actual, forward, roof);
		forward = wanted;
	}

	void WeakPatches::Transport(cs::Nullable<DVec3> actual, DVec3& forward, DVec3& roof)
	{
		if (!actual.has_value() || actual->LengthSquared() < 1e-18) return;
		DVec3 a = *actual;
		a = a.Normalized();
		auto axis = DVec3::Cross(forward, a);
		double sin = axis.Length(), cos = DVec3::Dot(forward, a);
		if (sin > 1e-9)
		{
			double ang = cs::Math::Atan2(sin, cos);
			roof = PilotFlight::RotateAbout(roof, axis / sin, ang).Normalized();
		}
		else if (cos < 0) return;
		forward = a;
	}

	double WeakPatches::Cost(const cs::Ref<ShipCombat>& c, const cs::List<Sample>& samples, const cs::List<double>& weak, const cs::List<IncomingFire>& fire,
		double protect, double slant, DVec3 f, DVec3 up)
	{
		auto right = DVec3::Cross(f, up).Normalized();
		double cost = 0;
		if (protect > 0) cost += protect * Exposure(samples, weak, fire, right, up, f);
		if (slant > 0) cost += slant * ArmourAngling::Harm(c->Spec, fire, right, up, f);
		return cost;
	}

	void WeakPatches::Choose(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BroadsideCommand>& cmd, DVec3 face,
		const cs::Dictionary<int, double>* bearings, cs::Nullable<DVec3> toTarget, cs::Nullable<DVec3> rest, cs::Nullable<DVec3> actual)
	{
		auto c = u->Combat;
		auto fire = IncomingFireOn(b, u);
		auto samples = SamplesOf(c->Spec);
		auto weak = WeaknessOf(c, PatchesOf(c, samples));
		double protect = Protection()->Value;
		// A hull slanted to the fire turns part of every hit aside on its armour (U.SPC-T534): the captain weighs that too.
		double slant = ArmourAngling::Strength()->Value > 0 ? ArmourAngling::AiWeight()->Value : 0;
		double most = 0;
		if (bearings != nullptr)
			for (auto [key, value] : *bearings)
				most = cs::Math::Max(most, value);
		cs::List<int> keys;
		if (bearings != nullptr) keys.AddRange(bearings->Keys());
		else keys.Add(NoBearing);
		double best = cs::Double::NegativeInfinity;
		int bestBearing = keys[0];
		double bestDeg = 0;
		double step = 360.0 / RollSteps;
		// US-T164: a crossing capital would rather lay its bow along its course (Choreography::Crossing), by a little more
		// than the hysteresis, so when it comes about it does turn its other broadside to the enemy.
		auto course = Choreography::Course(u);
		bool steaming = bearings != nullptr && course.LengthSquared() > 1e-12;
		for (int bearing : keys)
		{
			double battery = bearings == nullptr ? 1 : most > 0 ? (*bearings)[bearing] / most : 1;
			// The heading is judged in its own frame (U.SPC-T561), by the best roll it allows there; the roll itself is
			// then ordered for the heading the hull lies on, which a hull still turning has not yet left (U.SPC-T535).
			double ownBest = cs::Double::NegativeInfinity, rollBest = cs::Double::NegativeInfinity;
			double rollDeg = 0;
			for (int k = 0; k < RollSteps; k++)
			{
				double deg = k * step;
				DVec3 f, up;
				Frame(face, toTarget, bearing, deg, f, up);
				double own = battery;
				if (fire.Count() > 0) own -= Cost(c, samples, weak, fire, protect, slant, f, up);
				double at0 = 0;
				auto at = rest.has_value() ? *rest : LevelRoof(f);
				auto restUp = at - f * DVec3::Dot(at, f);
				if (restUp.LengthSquared() > 1e-18) at0 = RestBias * (1 - DVec3::Dot(up, restUp.Normalized())) * 0.5;
				double bonus = cmd->RollChosen && bearing == cmd->RollBearing && cs::Math::Abs(Astro::WrapDegrees360(deg - cmd->RollDeg + 180) - 180) < step * 0.5 ? Hysteresis : 0;
				double carried = own;
				if (actual.has_value() && fire.Count() > 0)
				{
					auto f2 = f;
					auto up2 = up;
					Transport(actual, f2, up2);
					carried = battery - Cost(c, samples, weak, fire, protect, slant, f2, up2);
				}
				double along = steaming ? Choreography::CourseBias * DVec3::Dot(f, course) : 0;
				own += bonus - at0 + along;
				carried += bonus - at0 + along;
				if (own > ownBest) ownBest = own;
				if (carried > rollBest + 1e-12)
				{
					rollBest = carried;
					rollDeg = deg;
				}
			}
			if (ownBest > best + 1e-12)
			{
				best = ownBest;
				bestBearing = bearing;
				bestDeg = rollDeg;
			}
		}
		cmd->RollBearing = bestBearing;
		cmd->RollDeg = bestDeg;
		cmd->RollChosen = true;
	}

	double WeakPatches::RollAngle(DVec3 from, DVec3 to, DVec3 axis)
	{
		auto k = axis.Normalized();
		auto a = from - k * DVec3::Dot(from, k);
		auto c = to - k * DVec3::Dot(to, k);
		if (a.LengthSquared() < 1e-18 || c.LengthSquared() < 1e-18) return 0;
		return cs::Math::Atan2(DVec3::Dot(DVec3::Cross(a, c), k), DVec3::Dot(a, c));
	}

	void WeakPatches::RollToward(const cs::Ref<BattleUnit>& u, DVec3 roof, double degPerSec, double dt)
	{
		auto f = u->Forward;
		auto cur = u->Roof.LengthSquared() > 1e-12 ? u->Roof : LevelRoof(f);
		cur -= f * DVec3::Dot(cur, f);
		auto want = roof - f * DVec3::Dot(roof, f);
		if (want.LengthSquared() < 1e-18) return;
		want = want.Normalized();
		if (cur.LengthSquared() < 1e-18)
		{
			u->Roof = want;
			return;
		}
		cur = cur.Normalized();
		double angle = RollAngle(cur, want, f);
		double max = cs::Math::Max(0.0, degPerSec) * Astro::Deg2Rad * dt;
		u->Roof = cs::Math::Abs(angle) <= max ? want : PilotFlight::RotateAbout(cur, f.Normalized(), cs::Math::Sign(angle) * max).Normalized();
	}

	double WeakPatches::HelmRoll(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 forward, DVec3 up, double rollRateDegS, double rollAlpha,
		double dt)
	{
		if (rollRateDegS <= 0 || forward.LengthSquared() < 1e-18) return 0;
		DVec3 unused, roof;
		Orient(b, u, forward, nullptr, std::nullopt, up, dt, unused, roof);
		double angle = RollAngle(up, roof, forward);
		if (cs::Math::Abs(angle) < 0.5 * Astro::Deg2Rad) return 0;
		double top = rollRateDegS * Astro::Deg2Rad;
		double rate = rollAlpha > 0 ? cs::Math::Min(top, cs::Math::Sqrt(2 * rollAlpha * cs::Math::Abs(angle))) : top;
		return cs::Math::Sign(angle) * rate / top;
	}

	bool WeakPatches::ToggleOrder(const cs::Ref<BattleUnit>& u)
	{
		auto cmd = Broadsides::Of(u);
		if (cmd == nullptr) return false;
		cmd->RollWeakPatches = !cmd->RollWeakPatches;
		cmd->RollChosen = false;
		return cmd->RollWeakPatches;
	}
}
