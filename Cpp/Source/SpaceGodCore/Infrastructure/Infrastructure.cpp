// Port of Unity.SpaceGod Assets/UnitySpace/Core/Infrastructure/Infrastructure.cs (see docs/MIGRATION.md).

#include "Infrastructure/Infrastructure.h"

#include "BodyCatalog.h"
#include "BodyOrientation.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Infrastructure/Satellite.h"
#include "Kepler.h"
#include "OrbitalElements.h"
#include "ScaleModel.h"
#include "Ships.h"

#include <cmath>
#include <tuple>

namespace SpaceGod::Core
{
	const char* ToString(StructureKind v)
	{
		switch (v)
		{
			case StructureKind::Shipyard: return "Shipyard";
			case StructureKind::Factory: return "Factory";
			case StructureKind::Habitat: return "Habitat";
			case StructureKind::CrewedStation: return "CrewedStation";
			case StructureKind::Telescope: return "Telescope";
			case StructureKind::Probe: return "Probe";
			case StructureKind::Depot: return "Depot";
			case StructureKind::Relay: return "Relay";
			case StructureKind::Gateway: return "Gateway";
		}
		return "";
	}

	// ---------------------------------------------------------------- OrbitalStructure

	cs::Ref<Faction> OrbitalStructure::Owner() const
	{
		auto o = Settlement ? Settlement->Owner : nullptr;
		return o ? o : owner;
	}

	DVec3 OrbitalStructure::HelioAt(const cs::Ref<Ephemeris>& eph, double jd) const
	{
		if (AtLagrange()) return ::SpaceGod::Core::Lagrange::HelioAt(eph, Body, Lagrange, jd);
		return eph->HelioAt(Body, jd) + Orbit->OffsetAt(jd);
	}

	DVec3 OrbitalStructure::Helio(const cs::Ref<Ephemeris>& eph) const
	{
		if (AtLagrange()) return ::SpaceGod::Core::Lagrange::HelioAt(eph, Body, Lagrange, eph->Jd);
		return eph->Helio(Body) + Orbit->OffsetAt(eph->Jd);
	}

	double OrbitalStructure::SlipProgress() const
	{
		if (Kind == StructureKind::Shipyard && Settlement != nullptr && Settlement->OnSlip.has_value())
		{
			ShipClass c = *Settlement->OnSlip;
			return cs::Math::Min(1.0, Settlement->BuildPoints / Economy::HullCost(c));
		}
		return 0;
	}

	// ---------------------------------------------------------------- Constellation

	int Constellation::Count() const
	{
		int n = 0;
		for (const auto& s : Shells) n += s.Planes * s.PerPlane;
		return n;
	}

	void Constellation::Offsets(const cs::Ref<BodyDefinition>& body, double jd, cs::List<DVec3>& output) const
	{
		auto axis = body->SpinAxis();
		auto node0 = BodyOrientation::EquatorNode(axis);
		auto east0 = DVec3::Cross(axis, node0);
		int i = 0;
		int shellIndex = 0;
		for (const auto& s : Shells)
		{
			double rate = Kepler::CircularRate(body->Gm, s.RadiusKm);
			for (int p = 0; p < s.Planes; p++)
			{
				DVec3 n, v;
				PlaneAxes(axis, node0, east0, s, shellIndex, p, n, v);
				for (int k = 0; k < s.PerPlane && i < output.Count(); k++, i++)
				{
					double phase = Phase(s, p, k, rate, jd);
					output[i] = (n * cs::Math::Cos(phase) + v * cs::Math::Sin(phase)) * s.RadiusKm;
				}
			}
			shellIndex++;
		}
		for (auto f : Flying)
			if (f.first >= 0 && f.first < output.Count()) output[f.first] = f.second->OffsetKm;
	}

	bool Constellation::SlotAt(const cs::Ref<BodyDefinition>& body, int index, double jd, DVec3& offsetKm, DVec3& velocityKmS) const
	{
		offsetKm = velocityKmS = DVec3::Zero;
		if (index < 0) return false;
		auto axis = body->SpinAxis();
		auto node0 = BodyOrientation::EquatorNode(axis);
		auto east0 = DVec3::Cross(axis, node0);
		int first = 0, shellIndex = 0;
		for (const auto& s : Shells)
		{
			int count = s.Planes * s.PerPlane;
			if (index < first + count)
			{
				int local = index - first, p = local / s.PerPlane, k = local % s.PerPlane;
				double rate = Kepler::CircularRate(body->Gm, s.RadiusKm);
				DVec3 n, v;
				PlaneAxes(axis, node0, east0, s, shellIndex, p, n, v);
				double phase = Phase(s, p, k, rate, jd);
				double c = cs::Math::Cos(phase), sn = cs::Math::Sin(phase);
				offsetKm = (n * c + v * sn) * s.RadiusKm;
				velocityKmS = (v * c - n * sn) * (s.RadiusKm * rate / Astro::SecondsPerDay);
				return true;
			}
			first += count;
			shellIndex++;
		}
		return false;
	}

	cs::Ref<Satellite> Constellation::Detach(const cs::Ref<BodyDefinition>& body, int index, double jd)
	{
		cs::Ref<Satellite> sat;
		if (Flying.TryGetValue(index, sat)) return sat;
		sat = cs::New<Satellite>(shared_from_this(), body, index, jd);
		Flying[index] = sat;
		return sat;
	}

	void Constellation::Step(const cs::Ref<BodyDefinition>& body, double jd)
	{
		if (Flying.Count() == 0) return;
		cs::Ref<cs::List<int>> settled = nullptr;
		for (auto f : Flying)
		{
			f.second->AdvanceTo(jd);
			if (f.second->OnStation && !f.second->Held()) { if (!settled) settled = cs::New<cs::List<int>>(); settled->Add(f.first); }
		}
		if (settled != nullptr)
			for (int k : *settled)
				Flying.Remove(k);
	}

	void Constellation::PlaneAxes(DVec3 axis, DVec3 node0, DVec3 east0, const Shell& s, int shellIndex, int p, DVec3& n, DVec3& v)
	{
		double inc = s.IncDeg * Astro::Deg2Rad;
		double node = Astro::TwoPi * p / s.Planes + shellIndex * 0.37;
		n = node0 * cs::Math::Cos(node) + east0 * cs::Math::Sin(node);
		auto inPlane = DVec3::Cross(axis, n);
		v = inPlane * cs::Math::Cos(inc) + axis * cs::Math::Sin(inc);
	}

	double Constellation::Phase(const Shell& s, int p, int k, double rate, double jd)
	{
		return Astro::TwoPi * (k + 0.5 * (p % 2)) / s.PerPlane + rate * (jd - Astro::J2000);
	}

	// ---------------------------------------------------------------- DockingRing

	cs::Ref<Faction> DockingRing::Owner() const
	{
		auto o = Settlement ? Settlement->Owner : nullptr;
		return o ? o : owner;
	}

	DockingRing::DockingRing(cs::Ref<BodyDefinition> body)
	{
		this->body = body;
		Body = body->Index;
		SurfaceKm = body->RadiusKm;
	}

	cs::Ref<ParkingOrbit> DockingRing::Point(double radiusKm, double lonOffsetDeg)
	{
		auto north = BodyOrientation::MapNorth(body);
		auto node = BodyOrientation::EquatorNode(north);
		auto east = DVec3::Cross(north, node);
		double lat = LatDeg * Astro::Deg2Rad;
		double w0 = cs::Double::IsNaN(body->PrimeMeridianDeg) ? 0 : body->PrimeMeridianDeg;
		double epoch = Astro::J2000;
		double rate = BodyOrientation::RateDegPerDay(body) * Astro::Deg2Rad;
		if (body->FacesParent && Ephemeris != nullptr && body->ParentIndex >= 0)
		{
			// Longitude 0 faces the parent (as the surface is drawn). Its direction is taken once
			// and carried on at the mean orbital rate, so the point is the same whenever it is asked
			// for (a ship planned days ahead meets it exactly); the orbit's slight eccentricity swings
			// the true sub-parent point a degree or two either side.
			if (cs::Double::IsNaN(lockedEpoch))
			{
				// At the ephemeris' own date, or -- asked before it has one (a world still being built) --
				// at J2000: the cached frame is all NaN until the first update, which left the ring nowhere.
				lockedEpoch = cs::Double::IsNaN(Ephemeris->Jd) ? Astro::J2000 : Ephemeris->Jd;
				constexpr double h = 1e-3;
				auto RelativeAt = [&](double jd)
				{
					auto own = Ephemeris->HelioAt(body->Index, jd);
					auto parent = Ephemeris->HelioAt(body->ParentIndex, jd);
					return own - parent;
				};
				auto relative = RelativeAt(lockedEpoch);
				// C# evaluates the two operands left to right: + h first.
				auto ahead = RelativeAt(lockedEpoch + h);
				auto behind = RelativeAt(lockedEpoch - h);
				auto motion = ahead - behind;
				auto toParent = -relative;
				auto p = toParent - north * DVec3::Dot(toParent, north);
				lockedW0 = cs::Math::Atan2(DVec3::Dot(p, east), DVec3::Dot(p, node)) * Astro::Rad2Deg;
				double sense = DVec3::Dot(DVec3::Cross(relative, motion), north) >= 0 ? 1 : -1;
				lockedRate = sense * cs::Math::Abs(body->Elements->At(lockedEpoch).MeanMotion);
			}
			epoch = lockedEpoch;
			w0 = lockedW0;
			rate = lockedRate;
		}
		auto o = cs::New<ParkingOrbit>();
		o->Body = Body;
		o->Center = north * (radiusKm * cs::Math::Sin(lat));
		o->RadiusKm = radiusKm * cs::Math::Cos(lat);
		o->U = node;
		o->V = east;
		o->EpochJd = epoch;
		o->PhaseAtEpoch = (w0 + LonDeg + lonOffsetDeg) * Astro::Deg2Rad;
		o->RateRadPerDay = rate;
		o->Fixed = true;
		return o;
	}

	cs::Ref<ParkingOrbit> DockingRing::HoldPoint(const cs::Ref<Ship>& ship)
	{
		// C#'s double % is IEEE fmod (truncating), as std::fmod.
		double f = std::fmod(ship->Id * 0.6180339887498949, 1.0);
		double g = std::fmod(ship->Id * 0.7548776662466927, 1.0);
		return Point(RadiusKm + HoldKm() * (0.85 + 0.3 * g), (f - 0.5) * 1.2);
	}

	DVec3 DockingRing::Up(double jd) { return Point(1)->OffsetAt(jd); }

	// ---------------------------------------------------------------- Infrastructure

	Infrastructure::Infrastructure(cs::Ref<BodyCatalog> catalog, cs::Ref<SpaceGod::Core::Ephemeris> ephemeris)
	{
		this->catalog = catalog;
		this->ephemeris = ephemeris;
	}

	void Infrastructure::UpdateSatellites(double jd)
	{
		for (const auto& c : Constellations)
			if (c->Flying.Count() > 0) c->Step((*catalog)[c->Body], jd);
	}

	cs::Ref<OrbitalStructure> Infrastructure::YardOf(const cs::Ref<SpaceGod::Core::Settlement>& s) const
	{
		for (const auto& st : Structures)
			if (st->Kind == StructureKind::Shipyard && st->Settlement == s) return st;
		return nullptr;
	}

	cs::List<cs::Ref<DockingRing>> Infrastructure::RingsAt(int body) const
	{
		cs::List<cs::Ref<DockingRing>> result;
		for (const auto& r : Rings)
			if (r->Body == body) result.Add(r);
		return result;
	}

	cs::String Infrastructure::FacilityName(IndustryKind k)
	{
		switch (k)
		{
			case IndustryKind::Smelter: return "Foundry";
			case IndustryKind::ElectronicsFab: return "Fab";
			case IndustryKind::MachineWorks: return "Works";
			case IndustryKind::MunitionsPlant: return "Arsenal";
			case IndustryKind::PharmaLab: return "Labs";
			case IndustryKind::Shipyard: return "Yards";
			default: return ToString(k);
		}
	}

	void Infrastructure::AddSettlementStructures(const cs::Ref<Economy>& economy, int seed)
	{
		cs::Random rng(seed);
		cs::Dictionary<int, int> perBody;
		for (const auto& s : economy->Settlements)
		{
			if (s->Kind == SettlementKind::Haven) continue;
			auto b = (*catalog)[s->Body];
			// (kind, industry, name, km)
			cs::List<std::tuple<StructureKind, IndustryKind, cs::String, double>> list;
			for (const auto& f : s->Facilities)
			{
				auto k = f->Recipe->Kind;
				if (k == IndustryKind::Mine || k == IndustryKind::Farm) continue;
				if (k == IndustryKind::Shipyard) list.Add(std::make_tuple(StructureKind::Shipyard, k, cs::Cat(s->Name, " ", FacilityName(k)), 3.5));
				else list.Add(std::make_tuple(StructureKind::Factory, k, cs::Cat(s->Name, " ", FacilityName(k)), 1.6));
			}
			if (s->Population >= 100e3 && b->Kind != BodyKind::Station) list.Add(std::make_tuple(StructureKind::Habitat, IndustryKind::Farm, cs::Cat(s->Name, " Habitat"), 2.2));

			for (const auto& [kind, industry, name, km] : list)
			{
				int n = 0;
				perBody.TryGetValue(s->Body, n);
				perBody[s->Body] = n + 1;
				// Low, near-equatorial orbits, a layer apart; free stations get a loose cluster around them.
				double radii = b->Kind == BodyKind::Station ? 3.0 + n * 0.9 : 1.32 + n * 0.11;
				// The three draws in C#'s left-to-right argument order.
				double incDeg = rng.NextDouble() * 12 - 6;
				double nodeDeg = rng.NextDouble() * 360;
				double phaseDeg = rng.NextDouble() * 360;
				auto orbit = EquatorialOrbit(b, b->RadiusKm * radii, incDeg, nodeDeg, phaseDeg);
				auto st = cs::New<OrbitalStructure>();
				st->Name = name; st->Kind = kind; st->Body = s->Body; st->Orbit = orbit; st->SetOwner(s->Owner); st->Settlement = s; st->Industry = industry; st->LengthKm = km;
				st->Note = kind == StructureKind::Shipyard ? "Orbital shipyard (game layer)" : kind == StructureKind::Habitat ? "Orbital habitat (game layer)" : "Factory station (game layer)";
				Structures.Add(st);
			}
		}
	}

	cs::Ref<ParkingOrbit> Infrastructure::EquatorialOrbit(const cs::Ref<BodyDefinition>& b, double radiusKm, double incDeg, double nodeDeg, double phaseDeg, double epochJd)
	{
		auto axis = b->SpinAxis();
		auto node0 = BodyOrientation::EquatorNode(axis);
		auto east0 = DVec3::Cross(axis, node0);
		double node = nodeDeg * Astro::Deg2Rad, inc = incDeg * Astro::Deg2Rad;
		auto n = node0 * cs::Math::Cos(node) + east0 * cs::Math::Sin(node);
		auto v = DVec3::Cross(axis, n) * cs::Math::Cos(inc) + axis * cs::Math::Sin(inc);
		auto o = cs::New<ParkingOrbit>();
		o->Body = b->Index; o->RadiusKm = radiusKm; o->U = n; o->V = v; o->EpochJd = epochJd; o->PhaseAtEpoch = phaseDeg * Astro::Deg2Rad;
		o->RateRadPerDay = Kepler::CircularRate(cs::Math::Max(b->Gm, 1e-12), radiusKm);
		return o;
	}

	void Infrastructure::Real(const cs::String& name, StructureKind kind, const cs::String& body, double altitudeKm, double incDeg, double nodeDeg, double phaseDeg, double km, const cs::String& note)
	{
		auto b = (*catalog)[body];
		auto st = cs::New<OrbitalStructure>();
		st->Name = name; st->Kind = kind; st->Body = b->Index; st->Orbit = EquatorialOrbit(b, b->RadiusKm + altitudeKm, incDeg, nodeDeg, phaseDeg);
		st->LengthKm = km; st->Real = true; st->Note = note;
		Structures.Add(st);
	}

	void Infrastructure::AddSolSpacecraft()
	{
		Real("ISS", StructureKind::CrewedStation, "Earth", 420, 51.64, 40, 0, 0.109, "International Space Station: ~420 km, 51.64\xC2\xB0, crewed since 2000.");
		Real("Tiangong", StructureKind::CrewedStation, "Earth", 385, 41.47, 170, 120, 0.055, "China's space station: ~385 km, 41.5\xC2\xB0.");
		Real("Hubble", StructureKind::Telescope, "Earth", 530, 28.47, 290, 230, 0.0132, "Hubble Space Telescope: ~530 km, 28.5\xC2\xB0, since 1990.");
		AtPoint("JWST", StructureKind::Telescope, "Earth", LagrangePoint::L2, 0.021, nullptr,
			"James Webb Space Telescope, at the Sun-Earth L2 point 1.5 million km beyond Earth (its halo orbit is not drawn).");
		AtPoint("Euclid", StructureKind::Telescope, "Earth", LagrangePoint::L2, 0.0045, nullptr,
			"ESA's dark-universe survey telescope, at Sun-Earth L2 since 2023 (halo orbit not drawn).");
		AtPoint("SOHO", StructureKind::Probe, "Earth", LagrangePoint::L1, 0.0095, nullptr,
			"Solar and Heliospheric Observatory, watching the Sun from Sun-Earth L1 since 1996 (halo orbit not drawn).");
		AtPoint("DSCOVR", StructureKind::Probe, "Earth", LagrangePoint::L1, 0.0018, nullptr,
			"Deep Space Climate Observatory: solar-wind warning and the EPIC camera's whole-Earth view, at Sun-Earth L1 since 2015.");
		AtPoint("Queqiao", StructureKind::Relay, "Moon", LagrangePoint::L2, 0.0042, nullptr,
			"China's relay for the Moon's far side (Chang'e 4), on a halo orbit about Earth-Moon L2 since 2018 (drawn at the point).");
		Real("Lunar Reconnaissance Orbiter", StructureKind::Probe, "Moon", 100, 90, 30, 0, 0.004, "LRO: polar lunar orbit (drawn circular at 100 km).");
		Real("Mars Reconnaissance Orbiter", StructureKind::Probe, "Mars", 300, 93, 60, 0, 0.0137, "MRO: ~300 km sun-synchronous polar orbit.");
		Real("Mars Odyssey", StructureKind::Probe, "Mars", 400, 93.2, 150, 90, 0.0057, "2001 Mars Odyssey: ~400 km polar orbit.");
		Real("MAVEN", StructureKind::Probe, "Mars", 3000, 75, 250, 200, 0.0114, "MAVEN: 150 \xC3\x97 6,200 km ellipse at 75\xC2\xB0 (drawn circular at its mean).");
		Real("Mars Express", StructureKind::Probe, "Mars", 5000, 86.3, 330, 300, 0.012, "Mars Express: 300 \xC3\x97 10,000 km polar ellipse (drawn circular).");
		Real("ExoMars TGO", StructureKind::Probe, "Mars", 400, 74, 20, 45, 0.0175, "Trace Gas Orbiter: ~400 km, 74\xC2\xB0.");

		int earth = (*catalog)["Earth"]->Index;
		double re = (*catalog)[earth]->RadiusKm;
		auto Make = [&](const char* name, Rgb color)
		{
			auto c = cs::New<Constellation>();
			c->Name = name;
			c->Body = earth;
			c->Color = color;
			return c;
		};
		auto gps = Make("GPS", Rgb(0.55f, 0.95f, 0.6f));
		gps->Shells.Add(Constellation::Shell(26560, 55, 6, 5));
		auto galileo = Make("Galileo", Rgb(0.45f, 0.7f, 1.0f));
		galileo->Shells.Add(Constellation::Shell(29600, 56, 3, 9));
		auto glonass = Make("GLONASS", Rgb(1.0f, 0.55f, 0.5f));
		glonass->Shells.Add(Constellation::Shell(25510, 64.8, 3, 8));
		auto beidou = Make("BeiDou", Rgb(1.0f, 0.85f, 0.4f));
		beidou->Shells.Add(Constellation::Shell(27900, 55, 3, 8));
		auto starlink = Make("Starlink", Rgb(0.3f, 0.33f, 0.38f));
		starlink->Shells.Add(Constellation::Shell(re + 550, 53.0, 72, 22));
		starlink->Shells.Add(Constellation::Shell(re + 540, 53.2, 72, 22));
		starlink->Shells.Add(Constellation::Shell(re + 570, 70.0, 36, 20));
		starlink->Shells.Add(Constellation::Shell(re + 560, 97.6, 6, 58));
		starlink->Shells.Add(Constellation::Shell(re + 530, 43.0, 28, 40));
		auto geo = Make("Geostationary belt", Rgb(0.95f, 0.75f, 0.95f));
		geo->Shells.Add(Constellation::Shell(42164, 0.05, 1, 540));
		Constellations.AddRange(cs::List<cs::Ref<Constellation>>{ starlink, gps, galileo, glonass, beidou, geo });
	}

	cs::Ref<OrbitalStructure> Infrastructure::AtPoint(const cs::String& name, StructureKind kind, const cs::String& body, LagrangePoint point, double km, const cs::Ref<Faction>& owner, const cs::String& note)
	{
		auto st = cs::New<OrbitalStructure>();
		st->Name = name; st->Kind = kind; st->Body = (*catalog)[body]->Index; st->Lagrange = point; st->LengthKm = km; st->SetOwner(owner); st->Real = owner == nullptr; st->Note = note;
		Structures.Add(st);
		return st;
	}

	void Infrastructure::AddSolLagrangeStations(const cs::Ref<Faction>& inner, const cs::Ref<Faction>& trojans)
	{
		AtPoint("Cislunar Depot", StructureKind::Depot, "Moon", LagrangePoint::L1, 1.2, inner,
			"Propellant depot at Earth-Moon L1, the low-energy waypoint between Earth and the Moon (game layer).");
		AtPoint("L1 Gateway", StructureKind::Gateway, "Moon", LagrangePoint::L1, 2.0, inner,
			"Transfer station at Earth-Moon L1, where Earth-Moon traffic changes ships (game layer).");
		AtPoint("Farside Depot", StructureKind::Depot, "Moon", LagrangePoint::L2, 1.0, inner,
			"Propellant depot at Earth-Moon L2, beyond the Moon, for ships leaving cislunar space (game layer).");
		AtPoint("Sunward Relay", StructureKind::Relay, "Earth", LagrangePoint::L1, 0.4, inner,
			"Signal station at Sun-Earth L1: an hour's warning of solar storms, relayed across the system (game layer).");
		AtPoint("Outbound Depot", StructureKind::Depot, "Earth", LagrangePoint::L2, 1.4, inner,
			"Propellant depot at Sun-Earth L2, the departure point for the outer system (game layer).");
		AtPoint("Greek Camp Depot", StructureKind::Depot, "Jupiter", LagrangePoint::L4, 1.2, trojans,
			"Propellant depot at Sun-Jupiter L4, 60 degrees ahead of Jupiter, serving the Greek camp's miners (game layer).");
		AtPoint("Trojan Camp Relay", StructureKind::Relay, "Jupiter", LagrangePoint::L5, 0.4, trojans,
			"Signal station at Sun-Jupiter L5, 60 degrees behind Jupiter, the only ears in the Trojan camp (game layer).");
	}

	double Infrastructure::RingRadiusKm(const cs::Ref<BodyDefinition>& b)
	{
		double periodS = cs::Math::Abs(b->RotationHours) * 3600;
		double stationary = periodS > 0 ? cs::Math::Pow(b->Gm * periodS * periodS / (4 * cs::Math::PI * cs::Math::PI), 1.0 / 3) : cs::Double::MaxValue;
		double limit = ScaleModel::NeighbourhoodRadii * 0.85 * b->RadiusKm;
		return stationary < limit ? stationary : b->RadiusKm * 3;
	}

	double Infrastructure::EntryInterface(const cs::String& body)
	{
		if (body == "Venus") return 250;
		if (body == "Mars") return 125;
		if (body == "Titan") return 600;
		return 120;
	}

	cs::Ref<DockingRing> Infrastructure::AddRing(const cs::Ref<Economy>& economy, const cs::String& body, const cs::String& name, const cs::String& city, double latDeg, double lonDeg)
	{
		auto b = (*catalog)[body];
		cs::Ref<Settlement> owner = nullptr;
		for (const auto& s : economy->At(b->Index))
			if (owner == nullptr || s->Population > owner->Population) owner = s;
		auto ring = cs::New<DockingRing>(b);
		ring->Ephemeris = ephemeris;
		ring->Id = Rings.Count(); ring->Name = name; ring->City = city; ring->LatDeg = latDeg; ring->LonDeg = lonDeg;
		ring->RadiusKm = RingRadiusKm(b); ring->EntryInterfaceKm = EntryInterface(body); ring->Settlement = owner; ring->SetOwner(owner ? owner->Owner : nullptr);
		// About 3.5 hours down from geostationary altitude; proportionally less from closer in.
		ring->DescentDays = 3.5 / 24 * cs::Math::Max(0.25, (ring->RadiusKm - b->RadiusKm) / 35793.0);
		Rings.Add(ring);
		return ring;
	}

	int Infrastructure::RingsFor(double population)
	{
		return cs::Math::Max(1, cs::Math::Min(7, cs::ToInt(cs::Math::Ceiling(cs::Math::Log10(cs::Math::Max(population, 1.0) / 1e5)))));
	}

	void Infrastructure::AddSolRings(const cs::Ref<Economy>& economy)
	{
		// Earth: one over each great city (seven, as many as its population warrants).
		AddRing(economy, "Earth", "New York Ring", "New York", 40.7128, -74.0060);
		AddRing(economy, "Earth", "Paris Ring", "Paris", 48.8566, 2.3522);
		AddRing(economy, "Earth", "Beijing Ring", "Beijing", 39.9042, 116.4074);
		AddRing(economy, "Earth", "Tokyo Ring", "Tokyo", 35.6762, 139.6503);
		AddRing(economy, "Earth", "Sydney Ring", "Sydney", -33.8688, 151.2093);
		AddRing(economy, "Earth", "Johannesburg Ring", "Johannesburg", -26.2041, 28.0473);
		AddRing(economy, "Earth", "Moscow Ring", "Moscow", 55.7558, 37.6173);
		// Mars (1.5 million: two), over Jezero crater and Pavonis Mons on the Tharsis bulge.
		AddRing(economy, "Mars", "Jezero Gate", "Jezero", 18.38, 77.58);
		AddRing(economy, "Mars", "Tharsis Gate", "Pavonis", 1.48, -112.96);
		// Venus's aerostat and Titan's port: one each.
		AddRing(economy, "Venus", "Aphrodite Gate", "Aphrodite Aerostat", -5, 105);
		AddRing(economy, "Titan", "Kraken Gate", "Kraken Port", 68, 50);
	}

	void Infrastructure::AddCentauriRings(const cs::Ref<Economy>& economy)
	{
		// Proxima b is tidally locked: the colony lies along the terminator, at dawn and dusk.
		AddRing(economy, "Proxima b", "Dawn Gate", "New Kentaurus (dawn)", 10, 90);
		AddRing(economy, "Proxima b", "Dusk Gate", "New Kentaurus (dusk)", -10, -90);
	}
}
