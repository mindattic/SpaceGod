// Port of Assets/UnitySpace/Core/Systems/StarSystemBuilder.cs (US-T17).

#include "Systems/StarSystemBuilder.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/AlphaCentauri.h"
#include "Astro.h"
#include "Kepler.h"
#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	cs::Ref<StarSystemBuilder::Result> StarSystemBuilder::Build(const cs::Ref<SystemSpec>& spec)
	{
		auto r = cs::New<Result>();
		// StringComparer.OrdinalIgnoreCase: keyed by the lower-cased name.
		cs::Dictionary<cs::String, cs::Ref<StarSpec>> stars;
		for (const auto& s : spec->Stars()) stars[cs::Strings::ToLowerInvariant(s->Name)] = s;

		bool pairRoot = spec->Primary->IsPair();
		if (pairRoot)
		{
			auto o = spec->Primary->Orbit;
			DVec3 peri, normal;
			OrbitalElements::VisualBinaryGeometry(spec->RaDeg, spec->DecDeg, o->IDeg, o->NodeDeg, o->ArgPeriDeg, peri, normal);
			r->ReferencePeriapsis = peri;
			r->ReferenceNormal = normal;
		}
		else
		{
			DVec3 peri, normal;
			OrbitalElements::VisualBinaryGeometry(spec->RaDeg, spec->DecDeg, spec->ReferenceIDeg, spec->ReferenceNodeDeg, 0, peri, normal);
			r->ReferencePeriapsis = peri;
			r->ReferenceNormal = normal;
		}

		AddNode(r, spec, spec->Primary, cs::String(), nullptr);

		cs::String rootName = r->Bodies[0]->Name;
		for (const auto& c : spec->Companions) AddCompanion(r, spec, c, rootName);
		for (const auto& p : spec->Planets) r->Bodies.Add(Planet(r, spec, p, StarOf(stars, p->Host, p->Name)));
		for (const auto& st : spec->Stations) r->Bodies.Add(Station(r, st, StarOf(stars, st->Host, st->Name)));
		for (const auto& d : spec->Discs)
		{
			StarOf(stars, d->Host, d->Name);
			auto normal = pairRoot && cs::Double::IsNaN(d->IDeg) ? r->ReferenceNormal : SkyNormal(spec, cs::Double::IsNaN(d->IDeg) ? spec->ReferenceIDeg : d->IDeg, spec->ReferenceNodeDeg);
			auto disc = cs::New<DebrisDisc>();
			disc->Name = d->Name; disc->HostName = d->Host; disc->InnerKm = d->InnerAu * Astro::AuKm; disc->OuterKm = d->OuterAu * Astro::AuKm;
			disc->Normal = normal; disc->Summary = d->Summary;
			r->Discs.Add(disc);
		}
		return r;
	}

	cs::Ref<StarSpec> StarSystemBuilder::StarOf(const cs::Dictionary<cs::String, cs::Ref<StarSpec>>& stars, const cs::String& host, const cs::String& what)
	{
		cs::Ref<StarSpec> star;
		// C#: host == null (a null string is the empty string here).
		if (host.empty() || !stars.TryGetValue(cs::Strings::ToLowerInvariant(host), star))
			throw cs::InvalidOperationException(cs::Cat(what, ": unknown host star ", host));
		return star;
	}

	// ---------------------------------------------------------------- stars

	/// <summary>Adds a star, or a pair and its barycentre, on <paramref name="orbit"/> about <paramref name="parent"/> (null: the root).</summary>
	void StarSystemBuilder::AddNode(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<StellarNode>& node, const cs::String& parent, const cs::Ref<OrbitalElements>& orbit)
	{
		if (!node->IsPair())
		{
			r->Bodies.Add(Star(node->Star, parent, orbit, 1, r->ReferenceNormal));
			return;
		}

		auto o = node->Orbit;
		DVec3 peri, normal;
		OrbitalElements::VisualBinaryGeometry(spec->RaDeg, spec->DecDeg, o->IDeg, o->NodeDeg, o->ArgPeriDeg, peri, normal);
		double period = o->PeriodYears * 365.25;
		double tPeri = Astro::J2000 + (o->PeriastronYear - 2000) * 365.25;
		double mean = Astro::WrapDegrees360((Astro::J2000 - tPeri) / period * 360);
		auto relative = OrbitalElements::FromVectors(Astro::J2000, o->AAu * Astro::AuKm, o->E, peri, normal, mean, period);
		double total = node->MassSolar();

		auto bary = cs::New<BodyDefinition>();
		bary->Name = node->Name; bary->Kind = BodyKind::Barycentre; bary->ParentName = parent; bary->RadiusKm = 0; bary->Gm = SunGm * total;
		bary->Elements = orbit;
		bary->Summary = !node->Summary.empty() ? node->Summary : cs::Cat("The centre of mass that ", node->A->Name, " and ", node->B->Name, " circle every ", cs::Fmt(o->PeriodYears, "0.0"), " years.");
		r->Bodies.Add(bary);
		r->Bodies.Add(Star(node->A, node->Name, relative, -node->B->MassSolar / total, normal));
		r->Bodies.Add(Star(node->B, node->Name, relative, node->A->MassSolar / total, normal));
	}

	cs::Ref<BodyDefinition> StarSystemBuilder::Star(const cs::Ref<StarSpec>& s, const cs::String& parent, const cs::Ref<OrbitalElements>& orbit, double orbitScale, DVec3 pole)
	{
		double ra, dec;
		Astro::RaDecOf(pole, ra, dec);
		auto b = cs::New<BodyDefinition>();
		b->Name = s->Name; b->Kind = BodyKind::Star; b->ParentName = parent;
		b->RadiusKm = s->RadiusSolar * SunRadiusKm; b->Gm = SunGm * s->MassSolar;
		b->Elements = orbit; b->OrbitScale = orbitScale;
		b->PoleRaDeg = ra; b->PoleDecDeg = dec; b->RotationHours = s->RotationDays * 24;
		b->TeffK = s->TeffK; b->LuminositySolar = s->LuminositySolar; b->Color = AlphaCentauri::StarColor(s->TeffK);
		b->SpectralType = s->Spectral;
		b->Summary = s->Summary;
		return b;
	}

	/// <summary>
	/// A wide companion on an orbit about the root through where it is seen today (its offset from
	/// the primary at the given mean anomaly). The orbit's plane is the reference plane, tipped to
	/// contain that offset.
	/// </summary>
	void StarSystemBuilder::AddCompanion(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<CompanionSpec>& c, const cs::String& root)
	{
		auto offset = CompanionOffsetKm(spec, c);
		auto u = offset.Normalized();
		auto normal = (r->ReferenceNormal - u * DVec3::Dot(r->ReferenceNormal, u)).Normalized();
		double ecc = Kepler::SolveEccentricAnomaly(c->MeanAnomalyDeg * Astro::Deg2Rad, c->E);
		double a = offset.Length() / (1 - c->E * cs::Math::Cos(ecc));
		double nu = 2 * cs::Math::Atan2(cs::Math::Sqrt(1 + c->E) * cs::Math::Sin(ecc / 2), cs::Math::Sqrt(1 - c->E) * cs::Math::Cos(ecc / 2));
		auto peri = u * cs::Math::Cos(nu) - DVec3::Cross(normal, u) * cs::Math::Sin(nu);
		double period = OrbitalElements::PeriodFromGm(a, SunGm * (spec->Primary->MassSolar() + c->Node->MassSolar()));
		auto orbit = OrbitalElements::FromVectors(Astro::J2000, a, c->E, peri, normal, c->MeanAnomalyDeg, period);
		AddNode(r, spec, c->Node, root, orbit);
	}

	DVec3 StarSystemBuilder::CompanionOffsetKm(const cs::Ref<SystemSpec>& spec, const cs::Ref<CompanionSpec>& c)
	{
		if (!cs::Double::IsNaN(c->RaDeg))
		{
			double ly = cs::Double::IsNaN(c->DistanceLy) ? spec->DistanceLy : c->DistanceLy;
			return (Astro::DirectionFromRaDec(c->RaDeg, c->DecDeg) * ly - spec->PositionLy()) * Astro::LightYearKm;
		}
		double ra = spec->RaDeg * Astro::Deg2Rad, dec = spec->DecDeg * Astro::Deg2Rad, pa = c->PositionAngleDeg * Astro::Deg2Rad;
		auto north = DVec3(-cs::Math::Sin(dec) * cs::Math::Cos(ra), -cs::Math::Sin(dec) * cs::Math::Sin(ra), cs::Math::Cos(dec));
		auto east = DVec3(-cs::Math::Sin(ra), cs::Math::Cos(ra), 0);
		return Astro::EquatorialToEcliptic(north * cs::Math::Cos(pa) + east * cs::Math::Sin(pa)) * (c->SeparationAu * Astro::AuKm);
	}

	// ---------------------------------------------------------------- planets

	DVec3 StarSystemBuilder::SkyNormal(const cs::Ref<SystemSpec>& spec, double iDeg, double nodeDeg)
	{
		DVec3 unused, normal;
		OrbitalElements::VisualBinaryGeometry(spec->RaDeg, spec->DecDeg, iDeg, nodeDeg, 0, unused, normal);
		return normal;
	}

	cs::Ref<BodyDefinition> StarSystemBuilder::Planet(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<PlanetSpec>& p, const cs::Ref<StarSpec>& host)
	{
		DVec3 normal, peri;
		double argPeri = cs::Double::IsNaN(p->ArgPeriDeg) ? 0 : p->ArgPeriDeg;
		if ((spec->Primary->IsPair() && cs::Double::IsNaN(p->IDeg)) || p->TiltFromReferenceDeg != 0)
		{
			// In the root pair's plane (or tilted out of it): periapsis from a fixed direction in the plane.
			normal = r->ReferenceNormal;
			if (p->TiltFromReferenceDeg != 0)
			{
				auto axis = DVec3::Cross(r->ReferenceNormal, r->ReferencePeriapsis).Normalized();
				double t = p->TiltFromReferenceDeg * Astro::Deg2Rad;
				normal = (r->ReferenceNormal * cs::Math::Cos(t) + axis * cs::Math::Sin(t)).Normalized();
			}
			auto reference = cs::Math::Abs(normal.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
			auto p0 = DVec3::Cross(reference, normal).Normalized();
			double w = argPeri * Astro::Deg2Rad;
			peri = p0 * cs::Math::Cos(w) + DVec3::Cross(normal, p0) * cs::Math::Sin(w);
		}
		else
		{
			// On the sky: the catalogued inclination (or the reference one), node and periastron argument.
			double i = cs::Double::IsNaN(p->IDeg) ? spec->ReferenceIDeg : p->IDeg;
			double node = cs::Double::IsNaN(p->NodeDeg) ? spec->ReferenceNodeDeg : p->NodeDeg;
			OrbitalElements::VisualBinaryGeometry(spec->RaDeg, spec->DecDeg, i, node, argPeri, peri, normal);
		}

		double mean = !cs::Double::IsNaN(p->MeanAnomalyDeg) ? p->MeanAnomalyDeg
			: !cs::Double::IsNaN(p->PeriastronJd) ? Astro::WrapDegrees360((Astro::J2000 - p->PeriastronJd) / p->PeriodDays * 360)
			: NamePhase(p->Name);

		bool gas = p->GasGiant.value_or(p->MassEarth >= GasGiantMassEarth);
		bool locked = p->TidallyLocked.value_or(!gas && p->PeriodDays < 40);
		double flux = HabitableZones::MeanFlux(host->LuminositySolar, p->AAu, p->E);
		auto zone = HabitableZones::Classify(host->LuminositySolar, host->TeffK, p->AAu, p->E);
		double poleRa, poleDec;
		Astro::RaDecOf(normal, poleRa, poleDec);

		cs::String summary = p->Summary; // ?? ""
		if (zone != HabitableZone::None)
			summary += cs::String(summary.size() > 0 ? " " : "") + (zone == HabitableZone::Conservative
				? "Inside the habitable zone."
				: "Inside the optimistic habitable zone (not the conservative one).");

		auto b = cs::New<BodyDefinition>();
		b->Name = p->Name; b->Kind = BodyKind::Planet; b->ParentName = p->Host;
		b->RadiusKm = p->RadiusEarth * EarthRadiusKm; b->Gm = p->MassEarth * EarthGm;
		b->Elements = OrbitalElements::FromVectors(Astro::J2000, p->AAu * Astro::AuKm, p->E, peri, normal, mean, p->PeriodDays);
		b->PoleRaDeg = poleRa; b->PoleDecDeg = poleDec;
		b->RotationHours = locked ? p->PeriodDays * 24 : !cs::Double::IsNaN(p->RotationHours) ? p->RotationHours : gas ? 10 : 24;
		b->FacesParent = locked;
		b->Color = p->Color.has_value() ? *p->Color : PlanetColor(p->MassEarth, flux);
		b->GasGiant = gas;
		b->AtmosphereColor = p->AtmosphereColor;
		b->AtmosphereStrength = p->AtmosphereStrength;
		b->Status = p->Status;
		b->MassIsMinimum = p->MassIsMinimum;
		b->HabitableZone = zone;
		b->DiscoveryYear = p->DiscoveryYear;
		b->Summary = summary;
		if (gas && b->AtmosphereStrength == 0)
		{
			b->AtmosphereColor = Rgb(b->Color.R * 1.05f, b->Color.G * 1.05f, b->Color.B * 1.05f);
			b->AtmosphereStrength = 0.3f;
		}
		return b;
	}

	double StarSystemBuilder::NamePhase(const cs::String& name)
	{
		uint32_t h = 2166136261u;
		// C# `foreach (char ch in name)` walks UTF-16 code units: decode the UTF-8 text to them.
		size_t i = 0, n = name.size();
		auto mix = [&](uint32_t ch) { h = (h ^ ch) * 16777619u; };
		while (i < n)
		{
			uint32_t c = static_cast<unsigned char>(name[i]);
			uint32_t cp; size_t len;
			if (c < 0x80) { cp = c; len = 1; }
			else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
			else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
			else { cp = c & 0x07; len = 4; }
			for (size_t k = 1; k < len && i + k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(name[i + k]) & 0x3F);
			i += len;
			if (cp >= 0x10000) { cp -= 0x10000; mix(0xD800 + (cp >> 10)); mix(0xDC00 + (cp & 0x3FF)); }
			else mix(cp);
		}
		return h % 360;
	}

	Rgb StarSystemBuilder::PlanetColor(double massEarth, double flux)
	{
		if (massEarth >= GasGiantMassEarth) return Rgb(0.8f, 0.72f, 0.6f);
		if (massEarth >= 8) return Rgb(0.62f, 0.75f, 0.85f);
		if (flux > 4) return Rgb(0.58f, 0.42f, 0.34f);
		if (flux > 0.25) return Rgb(0.55f, 0.52f, 0.5f);
		return Rgb(0.82f, 0.85f, 0.88f);
	}

	// ---------------------------------------------------------------- game layer

	cs::Ref<BodyDefinition> StarSystemBuilder::Station(const cs::Ref<Result>& r, const cs::Ref<StationSpec>& s, const cs::Ref<StarSpec>& host)
	{
		double ra, dec;
		Astro::RaDecOf(r->ReferenceNormal, ra, dec);
		auto b = cs::New<BodyDefinition>();
		b->Name = s->Name; b->Kind = BodyKind::Station; b->ParentName = s->Host; b->RadiusKm = 15; b->Gm = 1e-7;
		b->Elements = OrbitalElements::FromVectors(Astro::J2000, s->AAu * Astro::AuKm, 0, r->ReferencePeriapsis, r->ReferenceNormal, s->MeanAnomalyDeg,
			OrbitalElements::PeriodFromGm(s->AAu * Astro::AuKm, SunGm * host->MassSolar));
		b->PoleRaDeg = ra; b->PoleDecDeg = dec; b->RotationHours = 2;
		b->Color = Rgb(0.75f, 0.76f, 0.8f);
		b->Status = BodyStatus::Fictional;
		b->HabitatSite = s->HabitatSite;
		b->Summary = s->Summary;
		return b;
	}
}
