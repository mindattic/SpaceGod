// Port of Assets/UnitySpace/Core/Systems/FtlNavigation.cs (US-T17).

#include "Systems/FtlNavigation.h"
#include "Cs/Cs.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "Systems/DeepSpace.h"
#include "Astro.h"
#include "Ships.h"
#include "SimClock.h"
#include "Combat/PilotFlight.h"
#include "Economy/Propulsion.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	// ================================================================ FtlCourse

	cs::Ref<FtlCourse> FtlCourse::Plot(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<SpaceGod::Core::Ship>& ship, DVec3 fromKm, const cs::Ref<FtlTarget>& target, double jd)
	{
		auto c = cs::New<FtlCourse>();
		c->Target = target; c->From = here; c->Ship = ship; c->StartKm = fromKm; c->PlottedJd = jd; c->DepartureKm = fromKm;
		c->Refusal = HelmFtl::Refusal(ship);
		if (c->Refusal.empty() && here == nullptr) c->Refusal = "Nowhere to plot from";
		if (c->Refusal.empty() && target == nullptr) c->Refusal = "No star or beacon chosen";
		if (c->Refusal.empty() && target->System == here) c->Refusal = "No jump within a system: only to another star or beacon";
		if (here != nullptr && target != nullptr)
			c->Ly = here->At(fromKm).DistanceLy(target->Origin);
		if (!c->Refusal.empty()) return c;

		// What lies along the way is charted before the course is (U.SPC-T421): a rogue planet's well is plotted as the trip meets it.
		if (galaxy != nullptr && galaxy->DeepSpace != nullptr) galaxy->DeepSpace->SeedRoute(here->Name, here->Origin, target->Name, target->Origin);
		auto drive = c->Drive = CruiseDrives::Of(ship);
		c->TanksTonnes = ship->Propellant();
		c->RangeLy = FtlRules::RangeLy(drive, ship);

		// Out beyond the rim, clear of every well and field, on the line to the target.
		c->DepartureKm = DeparturePoint(galaxy, here, fromKm, target);
		auto start = here->At(c->DepartureKm);
		double wellKm = CruiseDrives::WellLimitKm(target->System);
		auto arrive = DeepSpace::ArrivalPoint(target->Origin, start, wellKm);
		c->Ly = arrive.DistanceLy(start);

		// The jump, leg by leg: each star's well or projector on the line pulls it out to cross under the ordinary drive.
		c->SpoolDays = drive->SpoolDays();
		auto probe = cs::New<Cruise>();
		probe->Drive = drive; probe->From = here; probe->To = target->System; probe->ToName = target->Name; probe->Target = arrive; probe->Plan = true;
		probe->Ships.Add(ship);
		auto at = start;
		double crossSpeedKmS = cs::Math::Max(1e-6, FtlRules::CrossingSpeedKmS()->Value);
		for (int leg = 0; ; leg++)
		{
			double legLy = arrive.DistanceLy(at);
			c->JumpFuelTonnes += FtlRules::PropellantFor(drive, ship, legLy);
			// As the trip itself is flown (FtlInterdiction): not the fields the leg starts in or ends in.
			cs::List<GravityField> fields;
			if (leg < MaxInterdictions && galaxy != nullptr && galaxy->DeepSpace != nullptr)
				for (const auto& f : galaxy->DeepSpace->FieldsAlong(probe, at, arrive))
					if (!f.Contains(at, 1.001) && !f.Contains(arrive, 1.001)) fields.Add(f);
			GravityField field;
			double along;
			if (!FtlRules::FirstEntry(at, arrive, fields, field, along))
			{
				c->FtlDays += CruiseProfile::For(drive, legLy).FlightDays();
				break;
			}
			auto entry = GalacticPosition::Lerp(at, arrive, along);
			c->FtlDays += DaysToReach(CruiseProfile::For(drive, legLy), entry.DistanceLy(at));
			auto dir = arrive.KmFrom(entry);
			dir = dir.LengthSquared() > 0 ? dir.Normalized() : DVec3::UnitX;
			auto exit = FtlRules::ExitPoint(field, entry, dir);
			if (exit.DistanceKm(entry) > arrive.DistanceKm(entry)) exit = arrive;
			double crossDays = exit.DistanceKm(entry) / crossSpeedKmS / Astro::SecondsPerDay;
			c->CrossingDays += crossDays;
			c->Interdictions.Add(field.Name);
			if (exit.DistanceKm(arrive) <= 1) break;
			// Spooled again on the far side, once the drive has cooled from being pulled out.
			c->FtlDays += drive->SpoolDays() + cs::Math::Max(0.0, drive->CooldownDays() - crossDays);
			at = exit;
		}

		// The run out: the hull's own thrust, flip and burn, or gentler so as to burn no more than its share of what the
		// tanks hold past the jump's (the rest is the hull's to fly on with at the far end).
		c->OutRunKm = (c->DepartureKm - fromKm).Length();
		if (c->NeedsOutRun())
		{
			double accel = cs::Math::Max(1e-9, ship->Spec->AccelG * 9.80665e-3);
			double left = ship->Propellant() - c->JumpFuelTonnes;
			double spare = left * OutRunShare;
			double dv = Propulsion::BrachistochroneDeltaV(accel, c->OutRunKm);
			if (left <= 0)
				c->Refusal = "Not enough reaction mass to jump " + cs::Fmt(c->Ly, "0.00") + " ly: it takes " +
					cs::Fmt(c->JumpFuelTonnes, "N0") + " t, the tanks hold " +
					cs::Fmt(ship->Propellant(), "N0") + " t";
			else if (Propulsion::PropellantFor(ship, dv) > spare)
			{
				// Gentler, so the run leaves the jump its reaction mass: what the spare buys, spent half out, half braking.
				double dvSpare = Propulsion::DeltaV(ship->ExhaustVelocityKmS(), ship->MassTonnes(), cs::Math::Max(1e-6, ship->MassTonnes() - spare)) * 0.999;
				accel = dvSpare * dvSpare / (4 * c->OutRunKm);
				dv = dvSpare;
			}
			c->OutRunAccelKmS2 = accel;
			c->OutRunDays = OutRunSeconds(c->OutRunKm, accel) / Astro::SecondsPerDay;
			c->OutRunFuelTonnes = Propulsion::PropellantFor(ship, dv);
			if (c->Refusal.empty() && accel < MinOutRunAccelKmS2)
				c->Refusal = "Too little reaction mass to fly out beyond the rim and still jump: refuel first";
		}
		if (!c->Refusal.empty()) return c;

		c->CoolDays = cs::Math::Max(0.0, FtlRules::CoolUntil(ship) - (jd + c->OutRunDays));
		if (c->JumpFuelTonnes > ship->Propellant() + 1e-9)
			c->Refusal = "Out of range: " + cs::Fmt(c->Ly, "0.00") + " ly, the tanks reach " +
				cs::Fmt(c->RangeLy, "0.#") + " ly";
		return c;
	}

	double FtlCourse::DaysToReach(const CruiseProfile& profile, double ly)
	{
		double t0 = 0, t1 = profile.FlightDays();
		for (int i = 0; i < 60; i++)
		{
			double mid = (t0 + t1) / 2;
			if (profile.LyAt(profile.SpoolDays + mid) < ly) t0 = mid;
			else t1 = mid;
		}
		return (t0 + t1) / 2;
	}

	DVec3 FtlCourse::DeparturePoint(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, DVec3 fromKm, const cs::Ref<FtlTarget>& target)
	{
		if (HelmFtl::WellRefusal(here, fromKm, galaxy).empty()) return fromKm;
		auto way = target->Origin.KmFrom(here->Origin);
		auto dir = way.LengthSquared() > 0 ? way.Normalized() : fromKm.LengthSquared() > 0 ? fromKm.Normalized() : DVec3::UnitX;
		auto at = dir * (HelmFtl::RimKm(here) * RimMargin);
		for (int pass = 0; pass < 6; pass++)
		{
			at = FtlRules::ClearOfBodyWells(here, at);
			if (HelmFtl::WellRefusal(here, at, galaxy).empty()) break;
			at = at * 1.25;
		}
		return at;
	}

	DVec3 FtlCourse::Bearing(DVec3 fromKm, double& km) const { return HelmFtl::Bearing(Target, From, fromKm, km); }

	cs::String FtlCourse::ToString() const { return Target != nullptr ? Target->Name : cs::String("no course"); }

	const char* ToString(FtlAutopilotStage v)
	{
		switch (v)
		{
		case FtlAutopilotStage::Off: return "Off";
		case FtlAutopilotStage::OutRun: return "OutRun";
		case FtlAutopilotStage::Aligning: return "Aligning";
		case FtlAutopilotStage::Cooling: return "Cooling";
		case FtlAutopilotStage::Spooling: return "Spooling";
		}
		return "";
	}

	const char* ToString(FtlAutopilotEvent v)
	{
		switch (v)
		{
		case FtlAutopilotEvent::None: return "None";
		case FtlAutopilotEvent::Arrived: return "Arrived";
		case FtlAutopilotEvent::Aligned: return "Aligned";
		case FtlAutopilotEvent::Disengaged: return "Disengaged";
		}
		return "";
	}

	// ================================================================ FtlAutopilot

	bool FtlAutopilot::Engage(const cs::Ref<FtlCourse>& course, DVec3 fromKm, bool inCombat, double jd)
	{
		Reason = course == nullptr ? cs::String("No course plotted: pick a star or beacon first") : course->Refusal;
		if (Reason.empty() && inCombat) Reason = "The autopilot will not fly in a fight";
		if (!Reason.empty())
		{
			Reset();
			return false;
		}
		Course = course;
		RunFromKm = fromKm;
		RunToKm = course->DepartureKm;
		RunStartJd = jd;
		RunAccelKmS2 = course->OutRunAccelKmS2;
		double km = (RunToKm - RunFromKm).Length();
		if (course->NeedsOutRun() && km > 1)
		{
			RunEndJd = jd + FtlCourse::OutRunSeconds(km, RunAccelKmS2) / Astro::SecondsPerDay;
			course->Ship->SetPropellant(cs::Math::Max(0.0, course->Ship->Propellant() - course->OutRunFuelTonnes));
			Stage = FtlAutopilotStage::OutRun;
		}
		else
		{
			RunEndJd = jd;
			Stage = FtlAutopilotStage::Aligning;
		}
		return true;
	}

	double FtlAutopilot::RunFraction(double jd) const
	{
		if (Stage != FtlAutopilotStage::OutRun) return Stage == FtlAutopilotStage::Off ? 0 : 1;
		double total = RunEndJd - RunStartJd;
		if (!(total > 0)) return 1;
		double t = cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - RunStartJd) / total));
		// Flip and burn: half the way accelerating, half braking.
		return t <= 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
	}

	DVec3 FtlAutopilot::VelocityKmS(double jd) const
	{
		if (Stage != FtlAutopilotStage::OutRun) return DVec3::Zero;
		double totalS = (RunEndJd - RunStartJd) * Astro::SecondsPerDay;
		if (!(totalS > 0)) return DVec3::Zero;
		double t = cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - RunStartJd) / (RunEndJd - RunStartJd)));
		double dsdt = (t <= 0.5 ? 4 * t : 4 * (1 - t)) / totalS;
		return (RunToKm - RunFromKm) * dsdt;
	}

	DVec3 FtlAutopilot::RunDirection() const
	{
		auto d = RunToKm - RunFromKm;
		return d.LengthSquared() > 0 ? d.Normalized() : DVec3::UnitX;
	}

	FtlAutopilotEvent FtlAutopilot::Step(bool inCombat, double jd)
	{
		if (!Active()) return FtlAutopilotEvent::None;
		if (inCombat) return Disengage("Autopilot off: a fight", jd);
		if (Stage == FtlAutopilotStage::OutRun && jd >= RunEndJd)
		{
			Stage = FtlAutopilotStage::Aligning;
			return FtlAutopilotEvent::Arrived;
		}
		return FtlAutopilotEvent::None;
	}

	FtlAutopilotEvent FtlAutopilot::Turn(DVec3& forward, DVec3 fromKm, double maxDeg)
	{
		if (Stage != FtlAutopilotStage::Aligning) return FtlAutopilotEvent::None;
		double unused;
		auto want = Course->Bearing(fromKm, unused);
		forward = TurnToward(forward, want, maxDeg * Astro::Deg2Rad);
		return HelmFtl::OffNoseDeg(Course->Target, Course->From, fromKm, forward) <= HelmFtl::AlignDeg * 0.5 ? FtlAutopilotEvent::Aligned : FtlAutopilotEvent::None;
	}

	DVec3 FtlAutopilot::TurnToward(DVec3 from, DVec3 to, double maxRad)
	{
		if (from.LengthSquared() <= 0) return to.Normalized();
		if (to.LengthSquared() <= 0) return from.Normalized();
		auto a = from.Normalized();
		auto b = to.Normalized();
		double cos = cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(a, b)));
		double angle = cs::Math::Acos(cos);
		if (angle <= maxRad) return b;
		auto axis = DVec3::Cross(a, b);
		if (axis.LengthSquared() < 1e-18) axis = cs::Math::Abs(a.Z) < 0.9 ? DVec3::Cross(a, DVec3::UnitZ) : DVec3::Cross(a, DVec3::UnitX);
		return PilotFlight::RotateAbout(a, axis.Normalized(), maxRad).Normalized();
	}

	FtlAutopilotEvent FtlAutopilot::Disengage(const cs::String& why, double jd)
	{
		Reason = why;
		Abandon(jd);
		return FtlAutopilotEvent::Disengaged;
	}

	void FtlAutopilot::Abandon(double jd)
	{
		if (Stage == FtlAutopilotStage::OutRun && Course != nullptr && Course->OutRunFuelTonnes > 0)
		{
			double total = RunEndJd - RunStartJd;
			// Flip and burn: the delta-v, so the propellant, is spent evenly across the run.
			double unflown = total > 0 ? 1 - cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - RunStartJd) / total)) : 0;
			Course->Ship->SetPropellant(Course->Ship->Propellant() + Course->OutRunFuelTonnes * unflown);
		}
		Reset();
	}

	void FtlAutopilot::Reset()
	{
		Stage = FtlAutopilotStage::Off;
		Course = nullptr;
		RunStartJd = RunEndJd = cs::Double::NaN;
		RunAccelKmS2 = 0;
	}

	// ================================================================ FtlNavigation

	const cs::Ref<RangeSetting>& FtlNavigation::TripRealSeconds()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.tripRealSeconds", SettingGroups::Ftl,
			"Long trip compression", 45, 10, 600, 5,
			cs::String("About how many real seconds each long stretch of a trip (the run out to the rim, the crossing between the stars) is ") +
			"compressed into: the clock is run at the slowest time warp that gets it over that fast. The rest of the galaxy moves on with it; " +
			", and . still change the pace in flight.",
			[](double v) { return cs::Fmt(v, "0") + " s"; }, "ftl time compression warp trip cruise autopilot");
		return setting;
	}

	int FtlNavigation::WarpFor(double days, int atLeast, double realSeconds)
	{
		double real = cs::Double::IsNaN(realSeconds) ? TripRealSeconds()->Value : realSeconds;
		if (cs::Double::IsNaN(days) || cs::Double::IsInfinity(days) || days <= 0) return cs::Math::Max(0, cs::Math::Min(SimClock::Levels().Count() - 1, atLeast));
		return SimClock::WarpToCover(days * Astro::SecondsPerDay, cs::Math::Max(1.0, real), atLeast);
	}

	cs::List<cs::Ref<FtlTarget>> FtlNavigation::Nearest(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, DVec3 fromKm)
	{
		auto list = FtlTargets::All(galaxy, here);
		if (here == nullptr) return list;
		auto at = here->At(fromKm);
		cs::Dictionary<cs::Ref<FtlTarget>, double> dist;
		for (const auto& t : list) dist[t] = t->Origin.DistanceKm(at);
		const auto& d = dist;
		list.Sort([&](const cs::Ref<FtlTarget>& a, const cs::Ref<FtlTarget>& b) { return cs::CompareTo(d[a], d[b]); });
		return list;
	}

	cs::Ref<FtlTarget> FtlNavigation::Cycle(const cs::List<cs::Ref<FtlTarget>>& ordered, const cs::Ref<FtlTarget>& current, int step)
	{
		// C#: ordered == null (a null list is the empty list here).
		if (ordered.Count() == 0) return nullptr;
		int i = -1;
		for (int k = 0; k < ordered.Count(); k++)
			if (Same(ordered[k], current))
			{
				i = k;
				break;
			}
		if (i < 0) return step < 0 ? ordered[ordered.Count() - 1] : ordered[0];
		int n = ordered.Count();
		return ordered[((i + step) % n + n) % n];
	}

	bool FtlNavigation::Same(const cs::Ref<FtlTarget>& a, const cs::Ref<FtlTarget>& b)
	{
		if (a == nullptr || b == nullptr) return a == b;
		if (a->System != nullptr || b->System != nullptr) return a->System == b->System;
		return cs::Strings::EqualsOrdinalIgnoreCase(a->Name, b->Name);
	}

	cs::String FtlNavigation::Describe(double days)
	{
		if (cs::Double::IsNaN(days) || cs::Double::IsInfinity(days)) return "never";
		if (days < 1) return cs::Fmt(days * 24, "0.0") + " h";
		if (days < 365.25 * 2) return cs::Fmt(days, "0.0") + " days";
		return cs::Fmt(days / 365.25, "0.0") + " years";
	}
}
