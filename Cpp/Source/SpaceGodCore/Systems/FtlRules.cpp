// Port of Assets/UnitySpace/Core/Systems/FtlRules.cs (US-T17).

#include "Systems/FtlRules.h"
#include "Systems/CruiseDrive.h"
#include "Systems/Galaxy.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Ships.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	const char* ToString(GravityFieldKind v)
	{
		switch (v)
		{
		case GravityFieldKind::Mass: return "Mass";
		case GravityFieldKind::Projector: return "Projector";
		}
		return "";
	}

	GravityField::GravityField(const cs::String& name, GravityFieldKind kind, GalacticPosition center, double radiusKm, const cs::Ref<Faction>& owner)
	{
		Name = name;
		Kind = kind;
		Center = center;
		RadiusKm = cs::Math::Max(0.0, radiusKm);
		Owner = owner;
	}

	// ---------------------------------------------------------------- settings

	const cs::Ref<RangeSetting>& FtlRules::WellGravity()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.wellGravity", SettingGroups::Ftl,
			"Gravity well threshold", 1.5, 0.1, 20, 0.1,
			cs::String("The pull (mm/s²) above which an FTL drive will not engage and drops out: it sets every star's and planet's well ") +
			"(about 2 AU about the Sun, 9 million km about Jupiter, half a million about Earth at the default). Lower makes the wells wider.",
			[](double v) { return cs::Fmt(v, "0.0") + " mm/s²"; }, "ftl gravity well threshold planet star interdiction mass");
		return setting;
	}

	const cs::Ref<RangeSetting>& FtlRules::SpoolScale()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.spoolScale", SettingGroups::Ftl,
			"Spool-up time", 1, 0, 4, 0.25,
			"How long every FTL drive charges before it engages, as a multiple of its rated spool-up (a capital drive's is six hours).",
			[](double v) { return cs::Fmt(v, "0.##") + "x"; }, "ftl spool charge time drive");
		return setting;
	}

	const cs::Ref<RangeSetting>& FtlRules::CooldownScale()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.cooldownScale", SettingGroups::Ftl,
			"Cooldown", 1, 0, 4, 0.25,
			cs::String("How long an FTL drive must cool after it drops out before it can charge again, as a multiple of its rated cooldown ") +
			"(a capital drive's is twelve hours).",
			[](double v) { return cs::Fmt(v, "0.##") + "x"; }, "ftl cooldown drive recharge");
		return setting;
	}

	const cs::Ref<RangeSetting>& FtlRules::FuelScale()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.fuelScale", SettingGroups::Ftl,
			"Fuel cost", 1, 0, 4, 0.25,
			cs::String("How much reaction mass an FTL jump burns, as a multiple of the drive's rated cost (a capital drive's charge takes 2% ") +
			"of the tanks and each light-year 0.2% more). Zero makes jumps free.",
			[](double v) { return cs::Fmt(v, "0.##") + "x"; }, "ftl fuel propellant power cost");
		return setting;
	}

	const cs::Ref<RangeSetting>& FtlRules::CrossingSpeedKmS()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.crossingSpeedKmS", SettingGroups::Ftl,
			"Interdicted crossing speed", 3000, 100, 30000, 100,
			cs::String("How fast (km/s) a group pulled out of FTL by a star's well or a gravity projector crosses it under its ordinary drive ") +
			"before it can spool again on the far side.",
			[](double v) { return cs::Fmt(v, "N0") + " km/s"; }, "ftl interdiction crossing speed normal drive");
		return setting;
	}

	// ---------------------------------------------------------------- wells

	double FtlRules::ThresholdKmS2() { return WellGravity()->Value * 1e-6; }

	double FtlRules::WellRadiusKm(double gm) { return WellRadiusKm(gm, WellGravity()->Value); }

	double FtlRules::SolarMasses(const cs::String& spectralType)
	{
		if (cs::Strings::IsNullOrEmpty(spectralType)) return 1;
		// char.ToUpperInvariant(spectralType.TrimStart()[0]): the first UTF-16 unit (an all-blank type throws, as C#'s indexer).
		switch (cs::Char::ToUpperInvariant(cs::Char::At(cs::Strings::TrimStart(spectralType), 0)))
		{
		case 'O': return 30;
		case 'B': return 6;
		case 'A': return 2;
		case 'F': return 1.3;
		case 'G': return 1;
		case 'K': return 0.7;
		case 'M': return 0.3;
		case 'D': return 0.6;
		case 'L':
		case 'T':
		case 'Y': return 0.05;
		default: return 1;
		}
	}

	double FtlRules::StarsGm(const cs::Ref<StarSystem>& system)
	{
		if (system == nullptr) return 0;
		double gm = 0;
		for (const auto& b : system->Catalog->Bodies())
			if (b->Kind == BodyKind::Star) gm += cs::Math::Max(0.0, b->Gm);
		return gm;
	}

	GravityField FtlRules::StarWell(const cs::Ref<StarSystem>& system)
	{
		return GravityField(system->Name, GravityFieldKind::Mass, system->Origin, WellRadiusKm(StarsGm(system)));
	}

	GravityField FtlRules::StarWell(const NearbyStar& star)
	{
		return GravityField(star.Name, GravityFieldKind::Mass, star.Origin(), WellRadiusKm(SolarMasses(star.SpectralType) * SunGm));
	}

	cs::Ref<BodyDefinition> FtlRules::BodyWellAt(const cs::Ref<StarSystem>& here, DVec3 localKm)
	{
		int i = BodyWellIndex(here, localKm);
		return i < 0 ? nullptr : (*here->Catalog)[i];
	}

	int FtlRules::BodyWellIndex(const cs::Ref<StarSystem>& here, DVec3 localKm)
	{
		if (here == nullptr) return -1;
		auto cat = here->Catalog;
		int best = -1;
		double bestGm = 0;
		for (int i = 1; i < cat->Count(); i++)
		{
			auto b = (*cat)[i];
			if (!(b->Gm > 0)) continue;
			if (b->Kind != BodyKind::Star && b->Kind != BodyKind::Planet && b->Kind != BodyKind::DwarfPlanet && b->Kind != BodyKind::Moon) continue;
			double r = WellRadiusKm(b->Gm);
			if (r <= b->RadiusKm) continue;
			if ((localKm - here->Ephemeris->Helio(i)).LengthSquared() >= r * r) continue;
			// Of two wells it is in (a moon's inside its planet's), the heavier names it.
			if (b->Gm > bestGm)
			{
				best = i;
				bestGm = b->Gm;
			}
		}
		return best;
	}

	DVec3 FtlRules::ClearOfBodyWells(const cs::Ref<StarSystem>& here, DVec3 localKm)
	{
		for (int pass = 0; pass < 4; pass++)
		{
			int i = BodyWellIndex(here, localKm);
			if (i < 0) return localKm;
			auto at = here->Ephemeris->Helio(i);
			auto off = localKm - at;
			auto dir = off.LengthSquared() > 0 ? off.Normalized() : localKm.LengthSquared() > 0 ? localKm.Normalized() : DVec3::UnitX;
			localKm = at + dir * (WellRadiusKm((*here->Catalog)[i]->Gm) * 1.001);
		}
		return localKm;
	}

	// ---------------------------------------------------------------- drive costs

	double FtlRules::PropellantFor(const cs::Ref<CruiseDriveSpec>& drive, const cs::Ref<Ship>& ship, double ly)
	{
		if (drive == nullptr || ship == nullptr || ship->Spec == nullptr) return 0;
		double share = (drive->ChargePropellantShare + drive->PropellantSharePerLy * cs::Math::Max(0.0, ly)) * FuelScale()->Value;
		return cs::Math::Max(0.0, share) * ship->PropellantCapacity();
	}

	double FtlRules::RangeLy(const cs::Ref<CruiseDriveSpec>& drive, const cs::Ref<Ship>& ship)
	{
		if (drive == nullptr || ship == nullptr) return 0;
		double scale = FuelScale()->Value;
		if (scale <= 0 || drive->PropellantSharePerLy <= 0) return cs::Double::PositiveInfinity;
		double left = ship->PropellantCapacity() > 0 ? ship->Propellant() / ship->PropellantCapacity() / scale - drive->ChargePropellantShare : 0;
		return cs::Math::Max(0.0, left / drive->PropellantSharePerLy);
	}

	double FtlRules::CoolUntil(const cs::Ref<Ship>& ship) { return ship == nullptr ? 0 : ship->FtlCoolUntilJd; }

	void FtlRules::StartCooldown(const cs::Ref<Ship>& ship, const cs::Ref<CruiseDriveSpec>& drive, double jd)
	{
		if (ship == nullptr || drive == nullptr) return;
		ship->FtlCoolUntilJd = cs::Math::Max(ship->FtlCoolUntilJd, jd + drive->CooldownDays());
	}

	double FtlRules::GalaxyCrossingDays(const cs::Ref<CruiseDriveSpec>& drive) { return CruiseProfile::For(drive, GalaxyWidthLy).TotalDays(); }

	bool FtlRules::FirstEntry(GalacticPosition from, GalacticPosition to, const cs::List<GravityField>& fields, GravityField& hit, double& along)
	{
		hit = GravityField();
		along = cs::Double::PositiveInfinity;
		auto d = to.KmFrom(from);
		double len2 = d.LengthSquared();
		if (len2 <= 0) return false;
		double len = cs::Math::Sqrt(len2);
		for (const auto& f : fields)
		{
			if (f.RadiusKm <= 0) continue;
			auto c = f.Center.KmFrom(from);
			double r2 = f.RadiusKm * f.RadiusKm;
			if (c.LengthSquared() <= r2) continue;
			// |t d - c| = r: the nearer root, if it is on the run.
			double b = DVec3::Dot(c, d) / len2;
			if (b <= 0 || b >= 1 + f.RadiusKm / len) continue;
			double disc = b * b - (c.LengthSquared() - r2) / len2;
			if (disc < 0) continue;
			double t = b - cs::Math::Sqrt(disc);
			if (t < 0 || t > 1 || t >= along) continue;
			along = t;
			hit = f;
		}
		return !cs::Double::IsPositiveInfinity(along);
	}

	GalacticPosition FtlRules::ExitPoint(const GravityField& field, GalacticPosition entry, DVec3 dir)
	{
		double chord = 2 * DVec3::Dot(field.Center.KmFrom(entry), dir);
		return chord > 0 ? entry.Offset(dir * chord) : entry;
	}
}
