// Port of Assets/UnitySpace/Core/Systems/HelmFtl.cs (US-T17).

#include "Systems/HelmFtl.h"
#include "Systems/CruiseDrive.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/DeepSpace.h"
#include "Systems/InterstellarTraffic.h"
#include "Information/News.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Escorts.h"

namespace SpaceGod::Core
{
	// ================================================================ FtlTarget

	FtlTarget::FtlTarget(const cs::String& name, const cs::Ref<StarSystem>& system, GalacticPosition origin)
		: Name(name), System(system), Origin(origin)
	{
	}

	cs::Ref<FtlTarget> FtlTarget::OfStar(const cs::Ref<StarSystem>& system) { return cs::New<FtlTarget>(system->Name, system, system->Origin); }

	cs::Ref<FtlTarget> FtlTarget::OfStar(const NearbyStar& star) { return cs::New<FtlTarget>(star.Name, nullptr, star.Origin()); }

	bool FtlTarget::IsBeacon() const { return System != nullptr && System->IsVoidBase; }

	DVec3 FtlTarget::LocalKm(const cs::Ref<StarSystem>& here) const { return here->LocalKm(Origin); }

	// ================================================================ FtlTargets

	cs::List<FtlTargets::Source>& FtlTargets::Sources()
	{
		static cs::List<Source> sources{ Source(&Stars) };
		return sources;
	}

	cs::List<cs::Ref<FtlTarget>> FtlTargets::All(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here)
	{
		cs::List<cs::Ref<FtlTarget>> list;
		for (const auto& s : Sources())
			for (const auto& t : s(galaxy, here))
				if (t != nullptr && t->System != here) list.Add(t);
		return list;
	}

	cs::List<cs::Ref<FtlTarget>> FtlTargets::Stars(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here)
	{
		cs::List<cs::Ref<FtlTarget>> r;
		if (galaxy == nullptr) return r;
		for (const auto& s : galaxy->Systems)
			if (s != here) r.Add(FtlTarget::OfStar(s));
		for (const auto& n : galaxy->NearbyStars)
			if (!n.Name.empty()) r.Add(FtlTarget::OfStar(n)); // C#: n.Name != null
		return r;
	}

	const char* ToString(FtlStage v)
	{
		switch (v)
		{
		case FtlStage::Idle: return "Idle";
		case FtlStage::Spooling: return "Spooling";
		}
		return "";
	}

	const char* ToString(FtlEvent v)
	{
		switch (v)
		{
		case FtlEvent::None: return "None";
		case FtlEvent::Aborted: return "Aborted";
		case FtlEvent::Charged: return "Charged";
		}
		return "";
	}

	// ================================================================ HelmFtl

	double HelmFtl::RimKm(const cs::Ref<StarSystem>& system) { return CruiseDrives::WellLimitKm(system); }

	cs::String HelmFtl::Refusal(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return "No ship at the helm";
		if (ship->Destroyed || ship->Derelict) return "The " + ship->Name + " is in no state to jump";
		// Who has a drive of their own is the owner's roster (U.SPC-T472): a cruiser or destroyer has none, a
		// battleship only when fitted with one.
		return FtlRoster::NoDriveReason(ship);
	}

	cs::String HelmFtl::RimRefusal(const cs::Ref<StarSystem>& here, DVec3 fromKm)
	{
		return BeyondRim(here, fromKm) ? cs::String()
			: "No jump inside " + here->Name + "'s gravity wells: fly out beyond its outer rim, " +
			  cs::Fmt(RimKm(here) / Astro::AuKm, "0.##") + " AU from its star (" +
			  cs::Fmt(fromKm.Length() / Astro::AuKm, "0.##") + " AU now)";
	}

	cs::String HelmFtl::WellRefusal(const cs::Ref<StarSystem>& here, DVec3 fromKm, const cs::Ref<Galaxy>& galaxy)
	{
		auto rim = RimRefusal(here, fromKm);
		if (!rim.empty()) return rim;
		auto body = FtlRules::BodyWellAt(here, fromKm);
		if (body != nullptr) return "No jump inside " + body->Name + "'s gravity well: fly further from it";
		if (galaxy != nullptr)
		{
			auto at = here->At(fromKm);
			for (const auto& p : galaxy->DeepSpace->Projectors())
				if (p->Active && p->Field().Contains(at)) return "A gravity projector, " + p->Name + ", holds the drive down here: fly clear of its field";
		}
		return cs::String();
	}

	cs::String HelmFtl::DriveRefusal(const cs::Ref<Ship>& ship, double km, double jd)
	{
		auto drive = CruiseDrives::Of(ship);
		if (drive == nullptr) return Refusal(ship);
		double cool = FtlRules::CoolUntil(ship) - jd;
		if (cool > 0)
			return "The FTL drive is cooling from its last jump: " + cs::Fmt(cool * 24, "0.0") + " h more";
		double ly = km / Astro::LightYearKm;
		// Flown as even jumps of no more than the drive reaches (US-T86), each charge paid for again.
		int jumps = drive->JumpsFor(ly);
		double need = jumps * FtlRules::PropellantFor(drive, ship, ly / jumps);
		if (need > ship->Propellant() + 1e-9)
			return "Not enough reaction mass to jump " + cs::Fmt(ly, "0.00") + " ly" +
				(jumps > 1 ? cs::Cat(" in ", jumps, " jumps") : cs::String()) + ": it takes " +
				cs::Fmt(need, "N0") + " t, the tanks hold " + cs::Fmt(ship->Propellant(), "N0") + " t";
		return cs::String();
	}

	DVec3 HelmFtl::Bearing(const cs::Ref<FtlTarget>& target, const cs::Ref<StarSystem>& here, DVec3 fromKm, double& km)
	{
		auto d = target->LocalKm(here) - fromKm;
		km = d.Length();
		return km > 0 ? d / km : DVec3::UnitX;
	}

	double HelmFtl::OffNoseDeg(const cs::Ref<FtlTarget>& target, const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward)
	{
		double km;
		auto dir = Bearing(target, here, fromKm, km);
		if (km <= 0 || forward.LengthSquared() <= 0) return 180;
		double cos = cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(dir, forward.Normalized())));
		return cs::Math::Acos(cos) * Astro::Rad2Deg;
	}

	cs::Ref<FtlTarget> HelmFtl::Pointed(const cs::List<cs::Ref<FtlTarget>>& targets, const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward, double toleranceDeg)
	{
		cs::Ref<FtlTarget> best = nullptr;
		double bestDeg = cs::Double::MaxValue, bestKm = cs::Double::MaxValue;
		for (const auto& t : targets)
		{
			double km;
			Bearing(t, here, fromKm, km);
			double deg = OffNoseDeg(t, here, fromKm, forward);
			if (deg > toleranceDeg) continue;
			bool better = deg < bestDeg - 0.1 || (cs::Math::Abs(deg - bestDeg) <= 0.1 && km < bestKm);
			if (!better) continue;
			best = t;
			bestDeg = deg;
			bestKm = km;
		}
		return best;
	}

	bool HelmFtl::Spool(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& here, const cs::Ref<FtlTarget>& target, DVec3 fromKm, DVec3 forward, bool inCombat, double jd, const cs::Ref<Galaxy>& galaxy)
	{
		AbortReason = Refusal(ship);
		if (AbortReason.empty() && here == nullptr) AbortReason = "Nowhere to jump from";
		if (AbortReason.empty()) AbortReason = WellRefusal(here, fromKm, galaxy);
		if (AbortReason.empty() && target == nullptr) AbortReason = "No star ahead to jump to: put the nose on one";
		if (AbortReason.empty() && target->System == here) AbortReason = "No jump within a system: only to another star";
		if (AbortReason.empty() && inCombat) AbortReason = "The drive will not spool in a fight";
		if (AbortReason.empty() && OffNoseDeg(target, here, fromKm, forward) > HoldDeg) AbortReason = "The nose is off " + target->Name + ": line it up first";
		if (AbortReason.empty())
		{
			double km;
			Bearing(target, here, fromKm, km);
			AbortReason = DriveRefusal(ship, km, jd);
		}
		if (!AbortReason.empty()) return false;
		Target = target;
		Drive = CruiseDrives::Of(ship);
		Stage = FtlStage::Spooling;
		SpoolStartJd = jd;
		ChargedJd = jd + Drive->SpoolDays();
		return true;
	}

	FtlEvent HelmFtl::Step(const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward, bool inCombat, double jd, const cs::Ref<Galaxy>& galaxy)
	{
		if (Stage != FtlStage::Spooling) return FtlEvent::None;
		if (inCombat) return Abort("Charge lost: the drive will not hold in a fight");
		if (!BeyondRim(here, fromKm)) return Abort("Charge lost: back inside " + here->Name + "'s gravity wells");
		auto well = WellRefusal(here, fromKm, galaxy);
		if (!well.empty()) return Abort("Charge lost: " + well);
		if (OffNoseDeg(Target, here, fromKm, forward) > HoldDeg) return Abort("Charge lost: the nose swung off " + Target->Name);
		return jd >= ChargedJd ? FtlEvent::Charged : FtlEvent::None;
	}

	FtlEvent HelmFtl::Abort(const cs::String& why)
	{
		AbortReason = why;
		Reset();
		return FtlEvent::Aborted;
	}

	void HelmFtl::Reset()
	{
		Stage = FtlStage::Idle;
		Target = nullptr;
		Drive = nullptr;
		SpoolStartJd = ChargedJd = cs::Double::NaN;
	}

	cs::Ref<Cruise> HelmFtl::EngageInterstellar(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<Ship>& ship, DVec3 fromKm, double jd, DVec3 velocityKmS)
	{
		int unused;
		return EngageInterstellar(galaxy, here, ship, fromKm, jd, velocityKmS, unused);
	}

	cs::List<cs::Ref<Ship>> HelmFtl::Wing(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>>& aboard)
	{
		cs::List<cs::Ref<Ship>> going;
		cs::List<cs::Ref<Ship>> craft;
		for (const auto& e : world->Fleet->Ships())
		{
			if (e == ship || e->Leader != ship || e->Destroyed || e->Derelict || e->Battle != nullptr) continue;
			if (CruiseDrives::Of(e) != nullptr) going.Add(e);
			else craft.Add(e);
		}
		cs::List<cs::Ref<Ship>> carriers{ ship };
		carriers.AddRange(going);
		aboard = FtlCarriage::Stow(carriers, craft);
		for (const auto& e : craft)
			if (aboard.ContainsKey(e)) going.Add(e);
		return going;
	}

	cs::Ref<Cruise> HelmFtl::EngageInterstellar(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<Ship>& ship, DVec3 fromKm, double jd, DVec3 velocityKmS, int& wing)
	{
		wing = 0;
		if (Stage != FtlStage::Spooling || Target == nullptr) throw cs::InvalidOperationException("The drive is not charged");
		auto to = Target->System != nullptr ? Target->System : galaxy->Realize(Target->Name, jd);
		if (to == nullptr || to == here)
		{
			Abort("Nothing charted at " + Target->Name + " to drop out at");
			return nullptr;
		}
		auto world = here->World;
		// Its escorts go too (U.SPC-T419): alongside under their own drives, or docked aboard. Found before the hull
		// leaves the fleet, which would hand them over to the marker it leaves behind.
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> aboard;
		auto escorts = Wing(world, ship, aboard);
		if (world->War) world->War->Forget(ship);
		if (world->Economy) world->Economy->Release(ship);
		// The hull itself goes, not a copy: its place in this fleet keeps a marker so no other id moves.
		if (world->Fleet->LetGo(ship) == nullptr)
		{
			Abort("The " + ship->Name + " is not in " + here->Name + "'s fleet to jump from");
			return nullptr;
		}
		ship->Role = ShipRole::Free;
		cs::List<cs::Ref<Ship>> ships{ ship };
		cs::HashSet<cs::Ref<Ship>> hired;
		for (const auto& e : escorts)
		{
			if (world->Escorts != nullptr && world->Escorts->IsHired(e))
			{
				hired.Add(e);
				world->Escorts->UnmarkHired(e);
			}
			if (world->War) world->War->Forget(e);
			if (world->Economy) world->Economy->Release(e);
			if (world->Fleet->LetGo(e) == nullptr) continue;
			e->Leader = ship;
			ships.Add(e);
		}
		wing = ships.Count() - 1;
		DeepSpace::NewsList news = galaxy->Traffic != nullptr ? cs::New<cs::List<cs::Ref<SystemReport>>>(galaxy->Traffic->News->Bundle(here, jd)) : nullptr;
		auto c = galaxy->DeepSpace->Launch(ships, here, fromKm, to, Drive, jd, news, true);
		c->Piloted = true;
		// FTL carries momentum (U.SPC-T418): the hull comes out moving as it went in.
		c->CarriedVelocityKmS = velocityKmS;
		for (const auto& h : hired)
			if (ships.Contains(h)) c->Hired.Add(h);
		for (const auto& [key, value] : aboard)
			if (ships.Contains(key)) c->Aboard[key] = value;
		// Craft in its hangars, and escorts clamped to its hull (U.SPC-T473).
		int clamped = FtlCarriage::CountClamped(c->Aboard.Keys());
		int docked = c->Aboard.Count() - clamped;
		cs::String with = FtlCarriage::Describe(wing - c->Aboard.Count(), docked, clamped);
		int jumps = c->JumpsLeft(jd);
		world->Log(jd, cs::Cat(ship->Name, " of ", ship->Faction->Name, " engages its ", cs::Strings::ToLowerInvariant(Drive->Name), " beyond the rim for ", to->Name, ", ") +
			cs::Cat(cs::Fmt(c->Target.DistanceLy(c->Start), "0.00"), " ly under its own power", jumps > 1 ? cs::Cat(" in ", jumps, " jumps") : cs::String(), with), WorldEventKind::Info, here->Catalog->Root()->Index);
		Reset();
		return c;
	}
}
