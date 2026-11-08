// US-T264: hands-on mining (see HandMining.h).

#include "Economy/HandMining.h"

#include "AsteroidField.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Outfitting.h"
#include "Economy/BeltMining.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Sensors/SensorPosture.h"
#include "Systems/SpaceFeatures.h"
#include "Systems/VoidSites.h"
#include "Systems/VoidStations.h"

#include <cstdint>
#include <map>

namespace SpaceGod::Core
{
	const char* ToString(RockSource v)
	{
		switch (v)
		{
			case RockSource::None: return "None";
			case RockSource::Belt: return "Belt";
			case RockSource::Body: return "Body";
			case RockSource::Ice: return "Ice";
		}
		return "";
	}

	const char* ToString(BeamState v)
	{
		switch (v)
		{
			case BeamState::Off: return "Off";
			case BeamState::NoLaser: return "NoLaser";
			case BeamState::Blocked: return "Blocked";
			case BeamState::NoRock: return "NoRock";
			case BeamState::Cutting: return "Cutting";
			case BeamState::Depleted: return "Depleted";
			case BeamState::HoldFull: return "HoldFull";
		}
		return "";
	}

	// ---------------------------------------------------------------- the laser

	MiningLaserSpec MiningLaserSpec::Of(int grade)
	{
		MiningLaserSpec s;
		s.Grade = cs::Math::Clamp(grade, 0, 3);
		switch (s.Grade)
		{
			case 1: s.RangeKm = 2.0; s.FractureSeconds = 6.0; s.FractureTonnes = 15; s.VeinFactor = 60; break;
			case 2: s.RangeKm = 3.5; s.FractureSeconds = 4.0; s.FractureTonnes = 25; s.VeinFactor = 150; break;
			case 3: s.RangeKm = 5.0; s.FractureSeconds = 3.0; s.FractureTonnes = 40; s.VeinFactor = 300; break;
			default: break;
		}
		return s;
	}

	cs::String MiningLaserSpec::Name(int grade)
	{
		switch (cs::Math::Clamp(grade, 0, 3))
		{
			case 1: return "fitted mining laser";
			case 2: return "industrial mining laser";
			case 3: return "deep-core mining laser";
			default: return "no mining laser";
		}
	}

	// ---------------------------------------------------------------- the beam

	cs::List<cs::String> MiningBeam::TakeNotices()
	{
		cs::List<cs::String> list = Notices;
		Notices.Clear();
		return list;
	}

	// ---------------------------------------------------------------- rocks

	namespace
	{
		/// FNV-1a over the id: a stable number per rock, the same on every machine.
		uint32_t Hash(const cs::String& s)
		{
			uint32_t h = 2166136261u;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h;
		}

		cs::String GoodName(Good g) { return cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name); }

		cs::String Km(double km) { return km < 10 ? cs::Cat(cs::Fmt(km, "0.00"), " km") : cs::Cat(cs::Fmt(km, "N0"), " km"); }

		/// The belt's rocks within a wide reach of where the beam was last fired, rebuilt only when the hull has moved far
		/// or time has run on (the belt has ~110,000 rocks; solving them all every frame would be wasteful). Only a speed-up:
		/// the rock picked is the same whatever the list holds, so long as it holds every rock near the hull.
		struct BeltCache
		{
			const AsteroidField* Field = nullptr;
			double Jd = cs::Double::NaN;
			DVec3 CentreHelio;
			cs::List<int> Near;
			cs::List<float> Xyz;
		};

		BeltCache& Cache()
		{
			static BeltCache c;
			return c;
		}

		constexpr double BeltCacheKm = 60000, BeltCacheSlackKm = 25000, BeltCacheDays = 0.005;

		const cs::List<int>& BeltNear(const AsteroidField& field, DVec3 sunHelio, DVec3 helio, double jd)
		{
			auto& c = Cache();
			bool fresh = c.Field == &field && !cs::Double::IsNaN(c.Jd) && cs::Math::Abs(jd - c.Jd) <= BeltCacheDays
				&& (helio - c.CentreHelio).Length() <= BeltCacheSlackKm;
			if (fresh) return c.Near;
			c.Field = &field;
			c.Jd = jd;
			c.CentreHelio = helio;
			c.Near.Clear();
			if (c.Xyz.Count() < field.Count * 3) c.Xyz = cs::NewArray<float>(field.Count * 3);
			field.PositionsAu(jd, 0, field.Count, c.Xyz);
			DVec3 p = (helio - sunHelio) / Astro::AuKm;
			double limit = BeltCacheKm / Astro::AuKm, limit2 = limit * limit;
			for (int i = 0; i < field.Count; i++)
			{
				double dx = c.Xyz[i * 3] - p.X, dy = c.Xyz[i * 3 + 1] - p.Y, dz = c.Xyz[i * 3 + 2] - p.Z;
				if (dx * dx + dy * dy + dz * dz < limit2) c.Near.Add(i);
			}
			return c.Near;
		}

		/// A void drift's rarer ore, by its station (the plan is built once a station).
		Good FieldOreOf(const cs::String& station)
		{
			static std::map<cs::String, Good> byStation;
			auto it = byStation.find(station);
			if (it != byStation.end()) return it->second;
			Good ore = VoidSites::Plan(station).FieldOre;
			byStation[station] = ore;
			return ore;
		}

		/// Whether a catalogue body is a rock the beam can cut, and what kind.
		bool MineableBody(const cs::Ref<BodyDefinition>& b, bool& icy, bool& metal, bool& hasOre, Good& ore)
		{
			icy = metal = hasOre = false;
			if (b == nullptr) return false;
			if (b->Kind == BodyKind::Asteroid) return true;
			auto site = VoidSites::Find(b->Name, b->ParentName);
			if (site == nullptr) return false;
			if (site->Kind != VoidSiteKind::FieldRock && site->Kind != VoidSiteKind::Chunk && site->Kind != VoidSiteKind::Planetoid) return false;
			icy = site->Icy || site->Field == VoidFieldStyle::IceField;
			metal = site->Field == VoidFieldStyle::MetalSwarm;
			if (site->Kind == VoidSiteKind::FieldRock && cs::Strings::StartsWith(site->ParentName, VoidStations::Prefix))
			{
				hasOre = true;
				ore = FieldOreOf(site->ParentName);
			}
			return true;
		}

		/// Every rock near <paramref name="helio"/> (surface within <paramref name="reachKm"/>), handed to <paramref name="visit"/>
		/// with its centre and radius; only enough is filled in to place it, the rest by <see cref="Complete"/> for the pick.
		template <class F> void ForEachRock(const cs::Ref<World>& world, DVec3 helio, double reachKm, bool withBelt, F&& visit)
		{
			if (world == nullptr || world->Ephemeris == nullptr) return;
			auto catalog = world->Catalog();
			if (catalog == nullptr || catalog->Count() == 0) return;
			auto eph = world->Ephemeris;
			double jd = eph->Jd;
			auto root = catalog->Root();

			// The catalogue's rocks: named asteroids, a void drift's rocks, a shattered planetoid and its chunks.
			for (int i = 1; i < catalog->Count(); i++)
			{
				auto b = (*catalog)[i];
				if (b == nullptr || b->Kind == BodyKind::Planet || b->Kind == BodyKind::Star || b->Kind == BodyKind::Barycentre) continue;
				DVec3 c = eph->Helio(i);
				double surface = (c - helio).Length() - b->RadiusKm;
				if (surface > reachKm) continue;
				bool icy, metal, hasOre;
				Good ore{};
				if (!MineableBody(b, icy, metal, hasOre, ore)) continue;
				MineableRock r;
				r.Source = RockSource::Body;
				r.Id = cs::Cat("body:", b->Name);
				r.Name = b->Name;
				r.Body = i;
				r.HelioKm = c;
				r.RadiusKm = b->RadiusKm;
				r.Class = HandMining::ClassOf(r.Id, icy, metal);
				r.HasFieldOre = hasOre;
				r.FieldOre = ore;
				r.ReserveTonnes = HandMining::ReserveOf(b->RadiusKm * 2);
				visit(r);
			}

			// The main belt's real rocks, in Sol only, between 1.5 and 6 AU (the belt and the Trojans).
			auto& field = HandMining::Belt();
			if (withBelt && field != nullptr && root != nullptr && root->Name == "Sun")
			{
				DVec3 sun = eph->Helio(0);
				double au = (helio - sun).Length() / Astro::AuKm;
				if (au >= 1.5 && au <= 6)
				{
					const auto& near = BeltNear(*field, sun, helio, jd);
					for (int i : near)
					{
						DVec3 c = sun + field->PositionAu(i, jd) * Astro::AuKm;
						double radius = field->DiameterKm(i) * 0.5;
						if ((c - helio).Length() - radius > reachKm) continue;
						MineableRock r;
						r.Source = RockSource::Belt;
						r.Id = cs::Cat("belt:", field->Number[i]);
						r.Name = cs::Cat("Asteroid ", field->Number[i]);
						r.HelioKm = c;
						r.RadiusKm = radius;
						r.Class = HandMining::ClassOf(r.Id, false, false);
						r.ReserveTonnes = HandMining::ReserveOf(radius * 2);
						visit(r);
					}
				}
			}

			// An ice field's chunks (drawn, not bodies), placed about the place's root.
			if (root != nullptr)
			{
				const auto& features = SpaceFeatures::Of(root->Name);
				DVec3 rootHelio = eph->Helio(0);
				for (const auto& f : features.IceFields)
				{
					if ((rootHelio + f.CentreKm - helio).Length() - f.RadiusKm > reachKm) continue;
					for (int k = 0; k < f.Chunks.Count(); k++)
					{
						const auto& chunk = f.Chunks[k];
						DVec3 c = rootHelio + chunk.OffsetKm;
						if ((c - helio).Length() - chunk.RadiusKm > reachKm) continue;
						MineableRock r;
						r.Source = RockSource::Ice;
						r.Id = cs::Cat("ice:", f.Name, "#", k);
						r.Name = cs::Cat(f.Name, " chunk ", k + 1);
						r.HelioKm = c;
						r.RadiusKm = chunk.RadiusKm;
						r.Class = AsteroidClass::CometIce();
						r.ReserveTonnes = HandMining::ReserveOf(chunk.RadiusKm * 2);
						visit(r);
					}
				}
			}
		}
	}

	cs::Ref<AsteroidField>& HandMining::Belt()
	{
		static cs::Ref<AsteroidField> value;
		return value;
	}

	int HandMining::GradeOf(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return 0;
		auto c = ship->Spec->Class;
		int grade = c == ShipClass::Miner || c == ShipClass::OreTug || c == ShipClass::CaptureTug ? 2 : 0;
		if (ship->Loadout != nullptr) grade += ship->Loadout->Count(ModuleKind::MiningLaser);
		return cs::Math::Min(3, grade);
	}

	cs::Ref<AsteroidClass> HandMining::ClassOf(const cs::String& id, bool icy, bool metalRich)
	{
		if (icy) return AsteroidClass::CometIce();
		if (metalRich) return AsteroidClass::Metallic();
		const auto& all = AsteroidClass::All();
		double total = 0;
		for (const auto& k : all) total += k->Share;
		if (all.Count() == 0 || total <= 0) return AsteroidClass::Stony();
		double r = (Hash(id) % 1000000u) / 1000000.0 * total;
		for (const auto& k : all)
		{
			r -= k->Share;
			if (r < 0) return k;
		}
		return all[all.Count() - 1];
	}

	double HandMining::ReserveOf(double diameterKm)
	{
		// The face a beam can reach, not the whole rock: a few hundred tonnes off a pebble, a few thousand off anything big.
		return 150 + 750 * cs::Math::Clamp(diameterKm, 0.0, 6.0);
	}

	double HandMining::Remaining(const cs::Ref<World>& world, const MineableRock& rock)
	{
		if (!rock.Valid()) return 0;
		double cut = 0;
		if (world != nullptr) world->HandCut.TryGetValue(rock.Id, cut);
		return cs::Math::Max(0.0, rock.ReserveTonnes - cut);
	}

	MineableRock HandMining::FindRock(const cs::Ref<World>& world, DVec3 helioKm, DVec3 direction, double rangeKm, bool withBelt)
	{
		MineableRock best;
		if (direction.LengthSquared() < 1e-18 || rangeKm <= 0) return best;
		DVec3 dir = direction.Normalized();
		double cone = ConeHalfDeg * cs::Math::PI / 180.0, bestDist = cs::Double::PositiveInfinity;
		ForEachRock(world, helioKm, rangeKm, withBelt, [&](const MineableRock& r)
		{
			DVec3 to = r.HelioKm - helioKm;
			double d = to.Length();
			double surface = d - r.RadiusKm;
			if (surface > rangeKm) return;
			if (surface > 0)
			{
				// Down the aim if the rock's disc comes within the cone of it.
				double off = cs::Math::Acos(cs::Math::Clamp(DVec3::Dot(to / d, dir), -1.0, 1.0));
				double disc = cs::Math::Asin(cs::Math::Clamp(r.RadiusKm / d, 0.0, 1.0));
				if (off - disc > cone) return;
			}
			if (surface < bestDist || (surface == bestDist && r.Id < best.Id))
			{
				bestDist = surface;
				best = r;
			}
		});
		return best;
	}

	MineableRock HandMining::NearestRock(const cs::Ref<World>& world, DVec3 helioKm, double maxKm, double& surfaceKm, bool withBelt)
	{
		MineableRock best;
		surfaceKm = cs::Double::PositiveInfinity;
		ForEachRock(world, helioKm, maxKm, withBelt, [&](const MineableRock& r)
		{
			double surface = cs::Math::Max(0.0, (r.HelioKm - helioKm).Length() - r.RadiusKm);
			if (surface < surfaceKm || (surface == surfaceKm && r.Id < best.Id))
			{
				surfaceKm = surface;
				best = r;
			}
		});
		return best;
	}

	cs::List<std::tuple<Good, double>> HandMining::YieldOf(const MineableRock& rock, int grade)
	{
		cs::List<std::tuple<Good, double>> out;
		if (rock.Class == nullptr) return out;
		auto laser = MiningLaserSpec::Of(grade);
		for (const auto& [g, share] : rock.Class->Yield)
		{
			double s = share;
			// A rare element (under a percent of the rock) is followed down its vein: the better the laser, the richer.
			if (share < 0.01) s = cs::Math::Min(MaxVeinShare, share * laser.VeinFactor);
			out.Add(std::make_tuple(g, s));
		}
		if (rock.HasFieldOre)
		{
			bool merged = false;
			for (auto& t : out)
				if (std::get<0>(t) == rock.FieldOre)
				{
					std::get<1>(t) += FieldOreShare;
					merged = true;
				}
			if (!merged) out.Add(std::make_tuple(rock.FieldOre, FieldOreShare));
		}
		return out;
	}

	cs::Ref<Faction> HandMining::ClaimHolderOf(const cs::Ref<World>& world, const MineableRock& rock)
	{
		if (world == nullptr || rock.Source != RockSource::Body || rock.Body < 0) return nullptr;
		if (world->Holdings != nullptr && world->Holdings->House != nullptr)
			for (const auto& p : world->Holdings->Properties)
				if (p->Kind == PropertyKind::MiningClaim && !p->Lost && p->Body == rock.Body) return world->Holdings->House;
		cs::Ref<Settlement> port;
		if (world->Economy != nullptr)
			for (const auto& s : world->Economy->At(rock.Body))
				if (!s->Fallen && s->Owner != nullptr && !s->Owner->IsPirate && (port == nullptr || s->Population > port->Population)) port = s;
		return port != nullptr ? port->Owner : nullptr;
	}

	cs::String HandMining::Blocker(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Destroyed) return "no hull";
		if (ship->Battle != nullptr) return "the beam is off in a fight";
		if (ship->Phase == ShipPhase::Lane) return "no mining in a hyperlane tube";
		if (SensorPosture::Offline(ship->Combat)) return "no mining while powered down (Shift+F3 to power up)";
		return cs::String();
	}

	void HandMining::Step(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam, bool held,
		DVec3 helioKm, DVec3 aim, double speedKmS, double dt)
	{
		if (beam == nullptr || world == nullptr || ship == nullptr) return;
		dt = cs::Math::Clamp(dt, 0.0, MaxStepSeconds);
		beam->Readout = cs::String();
		int grade = GradeOf(ship);
		beam->Grade = grade;
		bool belt = speedKmS < BeltSearchMaxKmS;
		if (!held)
		{
			// Off the trigger the face cools; a quick release and squeeze picks up where it was.
			beam->State = BeamState::Off;
			beam->Heat = cs::Math::Max(0.0, beam->Heat - dt * 0.5);
			// A hull with a laser near a rock is told it can cut it.
			if (grade > 0 && ship->Battle == nullptr && Blocker(ship).empty())
			{
				auto laser = MiningLaserSpec::Of(grade);
				double off = 0;
				auto near = NearestRock(world, helioKm, laser.RangeKm * 4, off, belt);
				if (near.Valid())
				{
					double left = Remaining(world, near);
					beam->Readout = cs::Cat("ROCK  \xC2\xB7  ", near.Name, " (", near.Class != nullptr ? near.Class->Name : cs::String("rock"), ")  \xC2\xB7  ", Km(off),
						left <= 0 ? cs::String("  \xC2\xB7  worked out") : cs::String(), "|",
						off <= laser.RangeKm ? cs::String("hold left mouse to cut it with the ") : cs::Cat("close to ", Km(laser.RangeKm), " to cut it with the "),
						MiningLaserSpec::Name(grade));
				}
			}
			return;
		}
		if (grade <= 0)
		{
			beam->State = BeamState::NoLaser;
			return;
		}
		auto why = Blocker(ship);
		if (!why.empty())
		{
			beam->State = BeamState::Blocked;
			beam->Readout = cs::Cat("MINING BEAM  \xC2\xB7  ", why);
			return;
		}
		auto laser = MiningLaserSpec::Of(grade);
		DVec3 dir = aim.LengthSquared() > 1e-18 ? aim.Normalized() : DVec3::UnitX;
		beam->Direction = dir;
		auto rock = FindRock(world, helioKm, dir, laser.RangeKm, belt);
		if (!rock.Valid())
		{
			beam->State = BeamState::NoRock;
			beam->Heat = cs::Math::Max(0.0, beam->Heat - dt * 0.5);
			double off = 0;
			auto near = NearestRock(world, helioKm, 200, off, belt);
			beam->Readout = near.Valid()
				? cs::Cat("MINING BEAM  \xC2\xB7  nothing down the aim|", near.Name, " is ", Km(off), " off: the ", MiningLaserSpec::Name(grade), " reaches ", Km(laser.RangeKm))
				: cs::Cat("MINING BEAM  \xC2\xB7  no rock in reach|fly to an asteroid, a void station's drift or an ice field: the ", MiningLaserSpec::Name(grade), " reaches ", Km(laser.RangeKm));
			return;
		}
		if (rock.Id != beam->Rock.Id)
		{
			beam->Heat = 0;
			beam->ClaimHolder = ClaimHolderOf(world, rock);
			auto house = world->Holdings != nullptr ? world->Holdings->House : nullptr;
			beam->Jumping = beam->ClaimHolder != nullptr && beam->ClaimHolder != ship->Faction && beam->ClaimHolder != house;
		}
		beam->Rock = rock;
		DVec3 to = rock.HelioKm - helioKm;
		beam->DistanceKm = cs::Math::Max(0.0, to.Length() - rock.RadiusKm);
		beam->HitHelioKm = helioKm + dir * beam->DistanceKm;

		double left = Remaining(world, rock);
		cs::String kind = rock.Class != nullptr ? rock.Class->Name : cs::String("rock");
		cs::String claim = beam->ClaimHolder == nullptr ? cs::String("free rock")
			: beam->Jumping ? cs::Cat("CLAIM OF ", cs::Strings::ToUpperInvariant(beam->ClaimHolder->Name), ": jumping it is a crime")
			: cs::Cat("claim of ", beam->ClaimHolder->Name);
		if (left <= 0)
		{
			beam->State = BeamState::Depleted;
			beam->Heat = 0;
			beam->Readout = cs::Cat("MINING BEAM  \xC2\xB7  ", rock.Name, " (", kind, ") is worked out|find another rock");
			return;
		}

		// The hold must take a crate of what the rock mostly yields.
		auto yield = YieldOf(rock, grade);
		Good main = Good::NickelIron;
		double mainShare = -1;
		for (const auto& [g, s] : yield)
			if (s > mainShare) { mainShare = s; main = g; }
		auto hold = ship->PersonalHold();
		if (hold == nullptr || (hold->CrateRoom(main) <= 0 && beam->Bin[static_cast<int>(main)] >= CargoFootprint::Of(main).Tonnes * 0.999))
		{
			beam->State = BeamState::HoldFull;
			beam->Readout = cs::Cat("MINING BEAM  \xC2\xB7  ", rock.Name, "  \xC2\xB7  the hold has no room for ", GoodName(main), "|sell at an assay camp or market (M), or jettison crates (I)");
			return;
		}

		beam->State = BeamState::Cutting;
		if (beam->Jumping) RecordJump(world, ship, beam);
		// Denser, harder rock takes longer to heat through; ice cuts quickly.
		double density = rock.Class != nullptr && rock.Class->Density > 0 ? rock.Class->Density : 2.7;
		double seconds = laser.FractureSeconds * cs::Math::Sqrt(density / 2.7);
		beam->Heat += dt / seconds;
		while (beam->Heat >= 1 && Remaining(world, rock) > 0)
		{
			beam->Heat -= 1;
			Fracture(world, ship, beam, laser);
		}
		if (Remaining(world, rock) <= 0) beam->Heat = 0;

		double crate = CargoFootprint::Of(main).Tonnes;
		beam->Readout = cs::Cat("MINING BEAM  \xC2\xB7  ", rock.Name, " (", kind, ")  \xC2\xB7  ", Km(beam->DistanceKm), "  \xC2\xB7  face ",
			cs::Fmt(beam->Heat * 100, "0"), "%  \xC2\xB7  ", GoodName(main), " ", cs::Fmt(beam->Bin[static_cast<int>(main)], "0"), "/", cs::Fmt(crate, "0"), " t|",
			MiningLaserSpec::Name(grade), " (grade ", grade, ")  \xC2\xB7  reach ", Km(laser.RangeKm), "  \xC2\xB7  ", cs::Fmt(Remaining(world, rock), "N0"),
			" t left on the face  \xC2\xB7  ", claim);
	}

	void HandMining::Fracture(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam, const MiningLaserSpec& laser)
	{
		const auto& rock = beam->Rock;
		double tonnes = cs::Math::Min(laser.FractureTonnes, Remaining(world, rock));
		if (tonnes <= 0) return;
		double cut = 0;
		world->HandCut.TryGetValue(rock.Id, cut);
		world->HandCut[rock.Id] = cut + tonnes;
		beam->Fractures++;
		beam->TonnesBroken += tonnes;
		for (const auto& [g, share] : YieldOf(rock, laser.Grade)) beam->Bin[static_cast<int>(g)] += tonnes * share;

		// Every good with a full crate's worth goes into the hold, first fit; what does not fit stays a crate's worth in the
		// bin and the rest drifts off as slag.
		auto hold = ship->PersonalHold();
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto good = static_cast<Good>(g);
			double crate = CargoFootprint::Of(good).Tonnes;
			if (crate <= 0) continue;
			while (beam->Bin[g] >= crate)
			{
				auto item = hold != nullptr ? hold->AddFirstFit(good) : nullptr;
				if (item == nullptr)
				{
					beam->Bin[g] = crate;
					break;
				}
				beam->Bin[g] -= crate;
				beam->Crates++;
				beam->Notices.Add(cs::Cat("Cut a crate of ", GoodName(good), " (", cs::Fmt(crate, "0"), " t) from ", rock.Name, " into the hold"));
			}
		}
		if (Remaining(world, rock) <= 0) beam->Notices.Add(cs::Cat(rock.Name, " is worked out: ", cs::Fmt(rock.ReserveTonnes, "N0"), " t cut from its face"));
	}

	void HandMining::RecordJump(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam)
	{
		if (world->Law == nullptr || world->Ephemeris == nullptr || beam->ClaimHolder == nullptr) return;
		double jd = world->Ephemeris->Jd, last = 0;
		if (beam->JumpedAt.TryGetValue(beam->Rock.Id, last) && jd - last < JumpCooldownDays) return;
		beam->JumpedAt[beam->Rock.Id] = jd;
		// The claim is watched: the settlement it belongs to (its claim beacons) sees the beam on its rock.
		cs::Ref<Settlement> port;
		if (world->Economy != nullptr)
			for (const auto& s : world->Economy->At(beam->Rock.Body))
				if (!s->Fallen && s->Owner == beam->ClaimHolder && (port == nullptr || s->Population > port->Population)) port = s;
		cs::String witness = port != nullptr ? port->Name : cs::Cat(beam->ClaimHolder->Name, " claim beacon");
		auto here = world->Law->JurisdictionAt(beam->Rock.Body);
		auto rec = world->Law->RecordOf(ship);
		if (rec == nullptr) return;
		world->Law->Commit(rec, CrimeKind::ClaimJumping, here != nullptr ? here : beam->ClaimHolder, beam->ClaimHolder, beam->Rock.Body, jd, witness,
			cs::Cat("cut ore on ", beam->Rock.Name));
		beam->Notices.Add(cs::Cat("Claim jumping: ", witness, " saw your beam on ", beam->Rock.Name, ", a claim of ", beam->ClaimHolder->Name, "; it is on your record"));
	}
}
