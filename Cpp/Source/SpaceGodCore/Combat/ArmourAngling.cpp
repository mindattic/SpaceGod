// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ArmourAngling.cs (see docs/MIGRATION.md).

#include "Combat/ArmourAngling.h"

#include <mutex>
#include <unordered_map>

#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/FireCones.h"
#include "Combat/WeakPatches.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	namespace
	{
		class Table final
		{
		public:
			double Beam = cs::Double::NaN, Strength = cs::Double::NaN, Cap = cs::Double::NaN;
			cs::Ref<cs::List<double>> Harm;
			std::mutex Lock; // C#'s lock (t)
		};

		// C#'s ConditionalWeakTable<CombatSpec, Table>: one table per design, keyed by the design's identity. A table only
		// caches what its own Beam/Strength/Cap stamp says it was built for, so a table outliving its design (and its
		// address being reused) is still read correctly; the map just keeps a few stale entries.
		std::unordered_map<const CombatSpec*, cs::Ref<Table>>& Tables()
		{
			static std::unordered_map<const CombatSpec*, cs::Ref<Table>> value;
			return value;
		}

		std::mutex& TablesLock()
		{
			static std::mutex value;
			return value;
		}
	}

	void ArmourAngling::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		Strength();
		MaxObliquity();
		AiWeight();
		Tables();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& ArmourAngling::Strength()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.armourAngling", SettingGroups::Broadsides,
			"Armour angling", 0.4, 0, 1, 0.05,
			"How much a slanted strike on the hull is turned aside: the share of a hit lost to a fully glancing strike on the hull's surface where it lands, and how much thicker the plate reads the more slanted the strike (0 turns angling off).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "armor armour angle angling glance oblique slant belt hull surface broadside capital");
		return value;
	}

	const cs::Ref<RangeSetting>& ArmourAngling::MaxObliquity()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.maxObliquity", SettingGroups::Broadsides,
			"Armour obliquity cap", 3, 1, 6, 0.25,
			"The thickest a slanted strike can find the armour: plate crossed at a slant reads one over the cosine of the strike thicker, but never more than this many times.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "armor armour angle obliquity cap slant thickness glance");
		return value;
	}

	const cs::Ref<RangeSetting>& ArmourAngling::AiWeight()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.aiAngleHull", SettingGroups::Broadsides,
			"AI captains slant the hull", 1, 0, 3, 0.1,
			"How much of its guns' bearing an AI captain (and the weak-patch rolling) gives up to present its hull slanted to the fire, so hits glance off its armour (0: it ignores the slant and lays its guns as before).",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.#"), "x"); }, "armor armour angle angling slant hull ai doctrine broadside capital");
		return value;
	}

	// ------------------------------------------------------------ one hit

	std::pair<double, double> ArmourAngling::Angling(double cos)
	{
		double k = Strength()->Value;
		if (k <= 0) return { 1, 1 };
		cos = cs::Math::Max(0.0, cs::Math::Min(1.0, cos));
		double cap = cs::Math::Max(1.0, MaxObliquity()->Value);
		double obliquity = cs::Math::Min(cap, 1 / cs::Math::Max(cos, 1 / cap));
		double armour = 1 + (obliquity - 1) * k;
		double through = 1 - k * (1 - cos);
		return { cs::Math::Max(0.0, armour), cs::Math::Max(0.0, cs::Math::Min(1.0, through)) };
	}

	std::pair<double, double> ArmourAngling::Angling(const cs::Ref<BattleUnit>& target, DVec3 at, DVec3 incoming)
	{
		auto spec = target->Combat ? target->Combat->Spec : cs::Ref<CombatSpec>();
		if (spec == nullptr || Strength()->Value <= 0 || incoming.LengthSquared() < 1e-18) return { 1, 1 };
		return Angling(StrikeCos(spec, target->ToLocal(at), target->DirToLocal(incoming)));
	}

	// ------------------------------------------------------------ the hull's surface

	void ArmourAngling::Shape(const cs::Ref<CombatSpec>& spec, double& r, double& core)
	{
		r = cs::Math::Max(0.005, cs::Math::Min(0.5, (spec ? spec->BeamFraction : FireCones::DefaultBeamFraction) * 0.5));
		core = cs::Math::Max(0.0, 0.5 - r);
	}

	DVec3 ArmourAngling::SurfaceNormal(const cs::Ref<CombatSpec>& spec, DVec3 local)
	{
		return FireCones::MountNormal(local.X, local.Y, local.Z, spec ? spec->BeamFraction : FireCones::DefaultBeamFraction);
	}

	bool ArmourAngling::Entry(const cs::Ref<CombatSpec>& spec, DVec3 throughLocal, DVec3 dirLocal, DVec3& hit)
	{
		hit = DVec3();
		if (dirLocal.LengthSquared() < 1e-24) return false;
		double r, core;
		Shape(spec, r, core);
		auto d = dirLocal.Normalized();
		auto p = throughLocal;
		double best = cs::Double::PositiveInfinity;
		// The straight length: x² + y² = r², between the ends.
		double a = d.X * d.X + d.Y * d.Y;
		if (a > 1e-18)
		{
			double bh = p.X * d.X + p.Y * d.Y;
			double c = p.X * p.X + p.Y * p.Y - r * r;
			double disc = bh * bh - a * c;
			if (disc >= 0)
			{
				double t = (-bh - cs::Math::Sqrt(disc)) / a;
				double z = p.Z + d.Z * t;
				if (z >= -core && z <= core) best = cs::Math::Min(best, t);
			}
		}
		// The rounded ends: spheres of the beam's radius at either end of the straight length.
		for (int sign = -1; sign <= 1; sign += 2)
		{
			auto o = p - DVec3(0, 0, sign * core);
			double bh = DVec3::Dot(o, d);
			double c = o.LengthSquared() - r * r;
			double disc = bh * bh - c;
			if (disc < 0) continue;
			double t = -bh - cs::Math::Sqrt(disc);
			double z = p.Z + d.Z * t;
			if (sign * z >= core - 1e-12) best = cs::Math::Min(best, t);
		}
		if (cs::Double::IsPositiveInfinity(best)) return false;
		hit = p + d * best;
		return true;
	}

	double ArmourAngling::StrikeCos(const cs::Ref<CombatSpec>& spec, DVec3 throughLocal, DVec3 dirLocal)
	{
		if (dirLocal.LengthSquared() < 1e-24) return 1;
		auto d = dirLocal.Normalized();
		DVec3 hit;
		auto point = Entry(spec, throughLocal, d, hit) ? hit : throughLocal;
		return cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(SurfaceNormal(spec, point), d)));
	}

	// ------------------------------------------------------------ the face a hull shows the fire

	double ArmourAngling::HarmAt(double cos)
	{
		auto [armour, through] = Angling(cos);
		return armour > 0 ? through / armour : through;
	}

	double ArmourAngling::FaceHarm(double r, double core, double keelCos)
	{
		constexpr int around = 48, polar = 24;
		double c = cs::Math::Max(0.0, cs::Math::Min(1.0, keelCos)), sn = cs::Math::Sqrt(1 - c * c);
		auto d = DVec3(sn, 0, c);
		double face = 0, harm = 0;
		double da = 2 * cs::Math::PI / around;
		double side = 2 * cs::Math::PI * r * 2 * core / around;
		for (int i = 0; i < around; i++)
		{
			double a = (i + 0.5) * da;
			double k = cs::Math::Cos(a) * sn;
			if (k <= 0 || side <= 0) continue;
			face += k * side;
			harm += k * side * HarmAt(k);
		}
		double dp = cs::Math::PI / 2 / polar;
		for (int sign = -1; sign <= 1; sign += 2)
			for (int j = 0; j < polar; j++)
			{
				double p = (j + 0.5) * dp;
				double area = r * r * cs::Math::Sin(p) * dp * da;
				for (int i = 0; i < around; i++)
				{
					double a = (i + 0.5) * da;
					auto n = DVec3(cs::Math::Sin(p) * cs::Math::Cos(a), cs::Math::Sin(p) * cs::Math::Sin(a), sign * cs::Math::Cos(p));
					double k = DVec3::Dot(n, d);
					if (k <= 0) continue;
					face += k * area;
					harm += k * area * HarmAt(k);
				}
			}
		return face > 0 ? harm / face : 1;
	}

	cs::Ref<cs::List<double>> ArmourAngling::TableOf(const cs::Ref<CombatSpec>& spec)
	{
		cs::Ref<Table> t;
		{
			// Tables.GetValue(spec, _ => new Table())
			std::lock_guard<std::mutex> guard(TablesLock());
			auto& tables = Tables();
			auto it = tables.find(spec.get());
			if (it == tables.end()) it = tables.emplace(spec.get(), cs::New<Table>()).first;
			t = it->second;
		}
		std::lock_guard<std::mutex> guard(t->Lock);
		double strength = Strength()->Value, cap = MaxObliquity()->Value;
		if (t->Harm == nullptr || t->Beam != spec->BeamFraction || t->Strength != strength || t->Cap != cap)
		{
			double r, core;
			Shape(spec, r, core);
			auto harm = cs::New<cs::List<double>>(cs::NewArray<double>(TableSteps + 1));
			for (int i = 0; i <= TableSteps; i++) (*harm)[i] = FaceHarm(r, core, static_cast<double>(i) / TableSteps);
			t->Harm = harm;
			t->Beam = spec->BeamFraction;
			t->Strength = strength;
			t->Cap = cap;
		}
		return t->Harm;
	}

	double ArmourAngling::Harm(const cs::Ref<CombatSpec>& spec, DVec3 towardShooterLocal)
	{
		if (spec == nullptr || Strength()->Value <= 0 || towardShooterLocal.LengthSquared() < 1e-24) return 1;
		auto table = TableOf(spec);
		double x = cs::Math::Min(1.0, cs::Math::Abs(towardShooterLocal.Z) / towardShooterLocal.Length()) * TableSteps;
		int i = cs::Math::Min(TableSteps - 1, cs::ToInt(x));
		double f = x - i;
		return (*table)[i] + ((*table)[i + 1] - (*table)[i]) * f;
	}

	double ArmourAngling::Harm(const cs::Ref<CombatSpec>& spec, const cs::List<IncomingFire>& fire, DVec3 right, DVec3 up, DVec3 forward)
	{
		if (Strength()->Value <= 0) return 1;
		double total = 0, weights = 0;
		for (const auto& f : fire)
		{
			auto d = DVec3(DVec3::Dot(f.Dir, right), DVec3::Dot(f.Dir, up), DVec3::Dot(f.Dir, forward));
			total += f.Weight * Harm(spec, d);
			weights += f.Weight;
		}
		return weights > 0 ? total / weights : 1;
	}

	bool ArmourAngling::AiSlants(const cs::Ref<BattleUnit>& u)
	{
		return Strength()->Value > 0 && AiWeight()->Value > 0 && !u->PlayerControlled() && Broadsides::Commands(u);
	}
}
