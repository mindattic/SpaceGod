// Port of Assets/UnitySpace/Core/Systems/DeepSpaceLife.cs (see docs/MIGRATION.md).

#include "Systems/DeepSpaceLife.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	const char* ToString(FeaturePlacement v)
	{
		switch (v)
		{
		case FeaturePlacement::Scattered: return "Scattered";
		case FeaturePlacement::Midway: return "Midway";
		case FeaturePlacement::Approach: return "Approach";
		}
		return "";
	}

	// ================================================================ DeepSpaceFeatures

	const cs::List<cs::String>& DeepSpaceFeatures::Greek()
	{
		static const cs::List<cs::String> v{ "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota", "Kappa", "Lambda", "Sigma", "Tau", "Omega" };
		return v;
	}

	const cs::List<cs::String>& DeepSpaceFeatures::Wanderers()
	{
		static const cs::List<cs::String> v{ "Wanderer", "Nomad", "Drifter", "Orphan", "Vagrant", "Exile", "Stray" };
		return v;
	}

	const cs::List<cs::String>& DeepSpaceFeatures::Veils()
	{
		static const cs::List<cs::String> v{ "Ash", "Amber", "Cinder", "Grey", "Lantern", "Pale", "Smoke", "Violet", "Widow's", "Shroud" };
		return v;
	}

	const cs::List<cs::String>& DeepSpaceFeatures::Ices()
	{
		static const cs::List<cs::String> v{ "Frostbody", "Iceball", "Snowdrift", "Rimecomet", "Glacier" };
		return v;
	}

	const cs::List<cs::String>& DeepSpaceFeatures::Hulls()
	{
		static const cs::List<cs::String> v{ "Cormorant", "Good Hope", "Patience", "Halcyon", "Wayfarer", "Silver Thread", "Long Reach", "Fair Wind", "Kestrel", "Providence" };
		return v;
	}

	cs::String DeepSpaceFeatures::Pick(const cs::List<cs::String>& list, cs::Random& r) { return list[r.Next(list.Count())]; }

	cs::Dictionary<DeepSpaceObjectKind, cs::Ref<DeepSpaceFeatureSpec>>& DeepSpaceFeatures::specs()
	{
		static cs::Dictionary<DeepSpaceObjectKind, cs::Ref<DeepSpaceFeatureSpec>> d;
		return d;
	}

	// (Every draw from r is its own statement below: C++ leaves the order of a call's arguments, and of an operator's
	// operands, unspecified, where C# evaluates them left to right.)
	void DeepSpaceFeatures::StaticConstructor()
	{
		static bool done = false;
		if (done) return;
		done = true;
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::RoguePlanet; s->Noun = "rogue planet"; s->OnNetworkPer10Ly = 0; s->OffNetworkPer10Ly = 0.8;
			// Within two of its own wells of the line: about half pull a passing group out.
			s->MaxOffsetKm = 2 * FtlRules::WellRadiusKm(3 * JupiterGm, 1.5); s->SurveyCredits = 250000;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r)
			{
				double x1 = r.NextDouble();
				double x2 = r.NextDouble();
				o->Gm = JupiterGm * (0.05 + 2.95 * x1 * x2);
				cs::String wanderer = Pick(Wanderers(), r);
				int number = r.Next(100, 1000);
				o->Name = cs::Cat("the rogue planet ", wanderer, " ", number);
			};
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::Nebula; s->Noun = "nebula"; s->OnNetworkPer10Ly = 0; s->OffNetworkPer10Ly = 0.4;
			s->MaxOffsetKm = 5e9; s->SurveyCredits = 400000;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r)
			{
				o->RadiusKm = 5e9 + r.NextDouble() * 1.5e10;
				o->Name = cs::Cat("the ", Pick(Veils(), r), " Veil");
			};
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::IceBody; s->Noun = "ice body"; s->OnNetworkPer10Ly = 0.8; s->OffNetworkPer10Ly = 1.2;
			s->MaxOffsetKm = 8e8; s->SurveyCredits = 60000;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r)
			{
				o->IceTonnes = 5000 + r.NextDouble() * 45000;
				cs::String ice = Pick(Ices(), r);
				cs::String greek = Pick(Greek(), r);
				int number = r.Next(10, 100);
				o->Name = cs::Cat(ice, " ", greek, "-", number);
			};
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::Derelict; s->Noun = "derelict"; s->OnNetworkPer10Ly = 0.3; s->OffNetworkPer10Ly = 0.6;
			s->MaxOffsetKm = 8e8; s->Transient = true; s->SurveyCredits = 0;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r)
			{
				o->SalvageCredits = 50000 + r.NextDouble() * 350000;
				o->Name = cs::Cat("the derelict ", Pick(Hulls(), r));
			};
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::Relay; s->Noun = "relay"; s->Placement = FeaturePlacement::Midway; s->Owned = true;
			s->OnNetworkPer10Ly = cs::Double::PositiveInfinity; s->OffNetworkPer10Ly = cs::Double::PositiveInfinity;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r) { o->Name = cs::Cat("the ", o->HomeName, " deep-space relay ", Pick(Greek(), r)); };
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::ListeningPost; s->Noun = "listening post"; s->Placement = FeaturePlacement::Approach; s->Owned = true;
			s->OnNetworkPer10Ly = cs::Double::PositiveInfinity; s->OffNetworkPer10Ly = cs::Double::PositiveInfinity;
			s->Make = [](cs::Ref<DeepSpaceObject> o, cs::Random& r)
			{
				cs::String greek = Pick(Greek(), r);
				int number = r.Next(1, 20);
				o->Name = cs::Cat("the ", o->Owner->Name, " listening post ", greek, " ", number);
			};
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::Wreck; s->Noun = "wreck"; s->Transient = true;
			Register(s);
		}
		{
			auto s = cs::New<DeepSpaceFeatureSpec>();
			s->Kind = DeepSpaceObjectKind::Distress; s->Noun = "distress call"; s->Transient = true;
			Register(s);
		}
	}

	void DeepSpaceFeatures::Register(cs::Ref<DeepSpaceFeatureSpec> spec)
	{
		StaticConstructor();
		if (spec == nullptr) throw cs::ArgumentNullException("spec");
		specs()[spec->Kind] = spec;
	}

	void DeepSpaceFeatures::Unregister(DeepSpaceObjectKind kind)
	{
		StaticConstructor();
		specs().Remove(kind);
	}

	cs::Ref<DeepSpaceFeatureSpec> DeepSpaceFeatures::Of(DeepSpaceObjectKind kind)
	{
		StaticConstructor();
		cs::Ref<DeepSpaceFeatureSpec> s;
		return specs().TryGetValue(kind, s) ? s : nullptr;
	}

	cs::List<cs::Ref<DeepSpaceFeatureSpec>> DeepSpaceFeatures::All()
	{
		StaticConstructor();
		auto list = specs().Values();
		list.Sort([](const cs::Ref<DeepSpaceFeatureSpec>& a, const cs::Ref<DeepSpaceFeatureSpec>& b)
		{
			int x = static_cast<int>(a->Kind), y = static_cast<int>(b->Kind);
			return x < y ? -1 : (x > y ? 1 : 0);
		});
		return list;
	}

	bool DeepSpaceFeatures::Transient(DeepSpaceObjectKind kind)
	{
		auto s = Of(kind);
		return s != nullptr ? s->Transient : true;
	}

	uint64_t DeepSpaceFeatures::StableHash(const cs::String& s)
	{
		// (C# hashes UTF-16 chars; this hashes the UTF-8 bytes, the same for the ASCII names it is given.)
		uint64_t h = 14695981039346656037ULL;
		for (char ch : s)
		{
			h ^= static_cast<unsigned char>(ch);
			h *= 1099511628211ULL;
		}
		return h;
	}

	// ================================================================ DeepSpaceRoute

	double DeepSpaceRoute::Ly() const { return AtB.DistanceLy(AtA); }

	cs::String DeepSpaceRoute::Name() const { return cs::Cat(A, "\xE2\x80\x93", B); } // A + "–" + B

	// ================================================================ DeepSpace (DeepSpaceLife.cs)

	cs::Random& DeepSpace::LifeRng() { return lifeRng; }

	Core::Galaxy* DeepSpace::Galaxy() const { return galaxy; }

	const cs::List<cs::Ref<DeepSpaceRoute>>& DeepSpace::Routes() const { return routes; }

	const cs::List<cs::Ref<DeepSpaceAmbush>>& DeepSpace::Ambushes() const { return ambushes; }

	bool DeepSpace::TryStar(const cs::String& name, GalacticPosition& at)
	{
		at = GalacticPosition{};
		if (name.empty() || galaxy == nullptr) return false;
		auto s = galaxy->Find(name);
		if (s != nullptr)
		{
			at = s->Origin;
			return true;
		}
		for (const auto& n : galaxy->NearbyStars)
			if (cs::Strings::EqualsOrdinalIgnoreCase(n.Name, name))
			{
				at = n.Origin();
				return true;
			}
		return false;
	}

	cs::String DeepSpace::RouteKey(const cs::String& a, const cs::String& b)
	{
		auto ua = cs::Strings::ToUpperInvariant(a);
		auto ub = cs::Strings::ToUpperInvariant(b);
		return cs::Strings::CompareOrdinal(ua, ub) <= 0 ? cs::Cat(ua, "|", ub) : cs::Cat(ub, "|", ua);
	}

	bool DeepSpace::Seeded(const cs::String& a, const cs::String& b) { return !a.empty() && !b.empty() && seededRoutes.Contains(RouteKey(a, b)); }

	bool DeepSpace::SeedRoute(cs::String aName, GalacticPosition a, cs::String bName, GalacticPosition b)
	{
		if (!RouteLife || aName.empty() || bName.empty() || cs::Strings::EqualsOrdinalIgnoreCase(aName, bName)) return false;
		cs::String key = RouteKey(aName, bName);
		if (!seededRoutes.Add(key)) return false;
		// Always from the end first in name order, whichever way it is flown.
		if (cs::Strings::CompareOrdinal(cs::Strings::ToUpperInvariant(aName), cs::Strings::ToUpperInvariant(bName)) > 0)
		{
			std::swap(aName, bName);
			std::swap(a, b);
		}
		auto route = cs::New<DeepSpaceRoute>();
		route->A = aName; route->B = bName; route->AtA = a; route->AtB = b; route->OnNetwork = Linked(aName, bName);
		double ly = route->Ly();
		if (ly < 0.05 || ly > DeepSpaceFeatures::MaxRouteLy) return false;
		routes.Add(route);
		// new Random((int)(StableHash(key) ^ (ulong)(galaxy?.Seed ?? 0)) & int.MaxValue): the int widened sign-extended,
		// the ulong narrowed to its low 32 bits.
		int galaxySeed = galaxy != nullptr ? galaxy->Seed : 0;
		uint64_t mixed = DeepSpaceFeatures::StableHash(key) ^ static_cast<uint64_t>(static_cast<int64_t>(galaxySeed));
		cs::Random r(static_cast<int>(static_cast<uint32_t>(mixed)) & cs::Int::MaxValue);
		for (const auto& spec : DeepSpaceFeatures::All())
		{
			if (!spec->Make) continue;
			if (spec->Owned)
			{
				PlaceOwned(spec, route, r);
				continue;
			}
			double expected = cs::Math::Min(static_cast<double>(DeepSpaceFeatures::MaxPerRoute), (route->OnNetwork ? spec->OnNetworkPer10Ly : spec->OffNetworkPer10Ly) * ly / 10);
			int n = cs::ToInt(cs::Math::Floor(expected)) + (r.NextDouble() < expected - cs::Math::Floor(expected) ? 1 : 0);
			for (int i = 0; i < n; i++)
			{
				double t = 0.12 + 0.76 * r.NextDouble();
				double offKm = spec->MaxOffsetKm * cs::Math::Sqrt(r.NextDouble());
				Place(spec, route, t, offKm, r, nullptr, cs::String());
			}
		}
		return true;
	}

	void DeepSpace::SeedSystemRoutes()
	{
		if (galaxy == nullptr) return;
		const auto& list = galaxy->Systems;
		for (int i = 0; i < list.Count(); i++)
			for (int j = i + 1; j < list.Count(); j++)
				SeedRoute(list[i]->Name, list[i]->Origin, list[j]->Name, list[j]->Origin);
	}

	bool DeepSpace::Linked(const cs::String& a, const cs::String& b)
	{
		auto net = galaxy != nullptr ? galaxy->Network : nullptr;
		if (net == nullptr) return false;
		for (const auto& link : net->Links())
		{
			cs::String x = link->A != nullptr ? link->A->Name : cs::String(), y = link->B != nullptr ? link->B->Name : cs::String();
			if ((cs::Strings::EqualsOrdinalIgnoreCase(x, a) && cs::Strings::EqualsOrdinalIgnoreCase(y, b)) ||
				(cs::Strings::EqualsOrdinalIgnoreCase(x, b) && cs::Strings::EqualsOrdinalIgnoreCase(y, a)))
				return true;
		}
		return false;
	}

	cs::Ref<StarSystem> DeepSpace::SettledEnd(const cs::String& name, cs::Ref<Faction>& power)
	{
		power = nullptr;
		auto s = galaxy != nullptr ? galaxy->Find(name) : nullptr;
		if (s == nullptr || !s->Settled() || s->World == nullptr || s->World->Sol == nullptr) return nullptr;
		power = s->World->Sol;
		return s;
	}

	void DeepSpace::PlaceOwned(cs::Ref<DeepSpaceFeatureSpec> spec, cs::Ref<DeepSpaceRoute> route, cs::Random& r)
	{
		cs::Ref<Faction> powerA, powerB;
		auto endA = SettledEnd(route->A, powerA);
		auto endB = SettledEnd(route->B, powerB);
		if (spec->Placement == FeaturePlacement::Midway)
		{
			// A relay joins two running systems, kept by the power of either.
			if (galaxy->Find(route->A) == nullptr || galaxy->Find(route->B) == nullptr || (endA == nullptr && endB == nullptr)) return;
			bool atA = endA != nullptr && (endB == nullptr || r.NextDouble() < 0.5);
			Place(spec, route, 0.5, 0, r, atA ? powerA : powerB, atA ? route->A : route->B);
			return;
		}
		// A listening post on each settled end's approach whose power has a navy to tell.
		if (endA != nullptr && powerA->IsMilitary) Place(spec, route, 0.1, 1e8 * r.NextDouble(), r, powerA, route->A);
		if (endB != nullptr && powerB->IsMilitary) Place(spec, route, 0.9, 1e8 * r.NextDouble(), r, powerB, route->B);
	}

	cs::Ref<DeepSpaceObject> DeepSpace::Place(cs::Ref<DeepSpaceFeatureSpec> spec, cs::Ref<DeepSpaceRoute> route, double t, double offKm, cs::Random& r, cs::Ref<Faction> owner, const cs::String& home)
	{
		auto line = route->AtB.KmFrom(route->AtA);
		auto dir = line.LengthSquared() > 0 ? line.Normalized() : DVec3::UnitX;
		auto side = DVec3::Cross(dir, cs::Math::Abs(dir.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX).Normalized();
		auto up = DVec3::Cross(dir, side);
		double angle = r.NextDouble() * 2 * cs::Math::PI;
		auto off = (side * cs::Math::Cos(angle) + up * cs::Math::Sin(angle)) * offKm;
		auto o = cs::New<DeepSpaceObject>();
		o->Kind = spec->Kind; o->Position = GalacticPosition::Lerp(route->AtA, route->AtB, t).Offset(off); o->SinceJd = cs::Double::NegativeInfinity;
		o->RouteA = route->A; o->RouteB = route->B; o->OffNetwork = !route->OnNetwork; o->Owner = owner; o->HomeName = home;
		o->SurveyCredits = spec->SurveyCredits * (route->OnNetwork ? 1 : DeepSpaceFeatures::OffNetworkSurveyFactor);
		spec->Make(o, r);
		Add(o);
		return o;
	}

	// ---------------------------------------------------------------- what the features do

	cs::List<GravityField> DeepSpace::RogueWells(DeepSpace& space, cs::Ref<Cruise> /*c*/, GalacticPosition near, double km)
	{
		cs::List<GravityField> found;
		for (const auto& o : space.objects)
		{
			if (o->Kind != DeepSpaceObjectKind::RoguePlanet || o->Gm <= 0) continue;
			double radius = FtlRules::WellRadiusKm(o->Gm);
			if (o->Position.DistanceKm(near) < km + radius) found.Add(GravityField(o->Name, GravityFieldKind::Mass, o->Position, radius));
		}
		return found;
	}

	cs::Ref<DeepSpaceObject> DeepSpace::NebulaAt(GalacticPosition at)
	{
		for (const auto& o : objects)
			if (o->Kind == DeepSpaceObjectKind::Nebula && o->Position.DistanceKm(at) < o->RadiusKm) return o;
		return nullptr;
	}

	double DeepSpace::SensorRangeKm(GalacticPosition at, double openKm) { return NebulaAt(at) != nullptr ? openKm * NebulaSensorShare : openKm; }

	double DeepSpace::RelayLagDays(cs::Ref<StarSystem> a, cs::Ref<StarSystem> b)
	{
		if (a == nullptr || b == nullptr) return cs::Double::PositiveInfinity;
		for (const auto& o : objects)
		{
			if (o->Kind != DeepSpaceObjectKind::Relay) continue;
			if ((cs::Strings::EqualsOrdinalIgnoreCase(o->RouteA, a->Name) && cs::Strings::EqualsOrdinalIgnoreCase(o->RouteB, b->Name)) ||
				(cs::Strings::EqualsOrdinalIgnoreCase(o->RouteA, b->Name) && cs::Strings::EqualsOrdinalIgnoreCase(o->RouteB, a->Name)))
				return GateNetwork::TransitDays(b->Origin.DistanceLy(a->Origin)) * RelaySlowdown;
		}
		return cs::Double::PositiveInfinity;
	}

	bool DeepSpace::SurveyStar(const cs::String& name) { return !name.empty() && surveyedStars.Add(name); }

	bool DeepSpace::Surveys(cs::Ref<Cruise> c)
	{
		if (c->Ships.Count() == 0) return false;
		auto leader = c->Leader();
		return (leader->Spec != nullptr && leader->Spec->Class == ShipClass::Explorer) || (leader->Voyage != nullptr && leader->Voyage->Purpose == VoyagePurpose::Survey);
	}

	// ---------------------------------------------------------------- ambushes on the routes

	cs::Ref<Cruise> DeepSpace::LaunchToPoint(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, GalacticPosition station, const cs::String& name, cs::Ref<CruiseDriveSpec> drive,
		double jd, NewsList news)
	{
		return Launch(ships, from, from->At(fromKm), nullptr, name, station, drive, jd, news, false);
	}

	cs::Ref<DeepSpaceAmbush> DeepSpace::LieInWait(cs::Ref<Cruise> band, double jd, double days, const cs::String& where)
	{
		auto leader = band->Leader();
		auto projector = cs::New<GravityProjector>();
		projector->Name = cs::Cat(leader->Faction->Name, " interdictor under ", leader->Name); projector->Owner = leader->Faction; projector->Position = band->PositionAt(jd); projector->RadiusKm = AmbushFieldKm;
		auto p = AddProjector(projector);
		auto a = cs::New<DeepSpaceAmbush>();
		a->Band = band; a->Projector = p; a->UntilJd = jd + days; a->LegJd = band->LegJd; a->Where = where;
		ambushes.Add(a);
		Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " lies in wait ", where, " with a gravity projector"), p->Position);
		return a;
	}

	void DeepSpace::LiftAmbushes(double jd)
	{
		for (int i = ambushes.Count() - 1; i >= 0; i--)
		{
			auto a = ambushes[i];
			bool gone = a->Band->Ships.Count() == 0 || !cruises.Contains(a->Band);
			// Still at its station on the leg it was holding on (a fight there sends it off on a new one, home).
			if (!gone && jd < a->UntilJd && a->Band->LegJd == a->LegJd) continue;
			RemoveProjector(a->Projector);
			ambushes.RemoveAt(i);
			Log(jd, cs::Cat(a->Projector->Name, " goes dark ", a->Where), a->Projector->Position);
		}
	}

	// ================================================================ SurveyFinds

	void SurveyFinds::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		if (space.Objects().Count() == 0) return;
		for (const auto& c : space.Cruises().ToList())
		{
			if (!DeepSpace::Surveys(c) || !(c->Engaged(toJd) || c->Engaged(fromJd))) continue;
			for (const auto& o : space.Objects())
			{
				if (o->SurveyCredits <= 0 || o->SurveyedBy != nullptr || o->SinceJd >= toJd) continue;
				if (DeepSpace::ClosestKm(c, o->Position, fromJd, toJd) - o->RadiusKm > RangeKm) continue;
				auto leader = c->Leader();
				o->SurveyedBy = leader->Faction;
				leader->Credits += o->SurveyCredits;
				leader->Faction->Treasury += o->SurveyCredits;
				space.SurveyFindsMade++;
				auto spec = DeepSpaceFeatures::Of(o->Kind);
				cs::String noun = spec != nullptr && !spec->Noun.empty() ? spec->Noun : cs::String("find");
				space.Log(toJd, cs::Cat("Survey find: ", leader->Name, " of ", leader->Faction->Name, " charts ", o->Name, ", a ", noun, o->OffNetwork ? " off the gate network" : "", ", ",
					"for ", cs::Fmt(o->SurveyCredits, "N0"), " cr"), o->Position);
				if (!c->Piloted) space.DropOut(c, toJd, SurveyDays);
				break;
			}
		}
	}

	// ================================================================ IceRefuelling

	void IceRefuelling::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		if (space.Objects().Count() == 0) return;
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Ships.Count() == 0 || c->Piloted || !(c->Engaged(toJd) || c->Engaged(fromJd))) continue;
			double short_ = 0;
			bool low = false;
			for (const auto& s : c->Ships)
			{
				if (s->PropellantCapacity() <= 0) continue;
				short_ += s->PropellantCapacity() - s->Propellant();
				if (s->PropellantFraction() < RefuelBelow) low = true;
			}
			if (!low) continue;
			for (const auto& o : space.Objects())
			{
				if (o->Kind != DeepSpaceObjectKind::IceBody || o->IceTonnes <= 0) continue;
				double closest = DeepSpace::ClosestKm(c, o->Position, fromJd, toJd);
				if (closest > RangeKm || closest > space.SensorRangeKm(o->Position, RangeKm)) continue;
				double take = cs::Math::Min(o->IceTonnes, short_);
				double left = take;
				for (const auto& s : c->Ships)
				{
					double room = s->PropellantCapacity() - s->Propellant();
					if (room <= 0 || left <= 0) continue;
					double fill = cs::Math::Min(room, left);
					s->SetPropellant(s->Propellant() + fill);
					left -= fill;
				}
				o->IceTonnes -= take;
				space.Log(toJd, cs::Cat(c->Leader()->Name, " of ", c->Leader()->Faction->Name, " drops out at ", o->Name, " to mine ",
					cs::Fmt(take, "N0"), " t of ice for its tanks"), o->Position);
				space.DropOut(c, toJd, IceDays);
				break;
			}
		}
	}

	// ================================================================ DistressCalls

	void DistressCalls::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		auto& r = space.LifeRng();
		if (space.Routes().Count() > 0 && r.NextDouble() < CallsPerDay * (toJd - fromJd)) Call(space, toJd, r);
		if (space.Objects().Count() == 0) return;
		// Calls that have gone unanswered too long fall silent.
		for (const auto& o : space.Objects())
			if (o->Kind == DeepSpaceObjectKind::Distress && o->ExpiresJd <= toJd)
			{
				o->Kind = DeepSpaceObjectKind::Derelict;
				o->Survivors = 0;
				o->Name = cs::Strings::Replace(o->Name, "the adrift ", "the derelict ");
				space.Log(o->ExpiresJd, cs::Cat("The distress call from ", o->Name, " falls silent"), o->Position);
			}
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Ships.Count() == 0 || c->Piloted || !(c->Engaged(toJd) || c->Engaged(fromJd))) continue;
			if (c->Leader()->Voyage != nullptr && c->Leader()->Voyage->Purpose == VoyagePurpose::Campaign) continue;
			for (const auto& o : space.Objects().ToList())
			{
				if (o->Kind != DeepSpaceObjectKind::Distress || o->SinceJd >= toJd) continue;
				double closest = DeepSpace::ClosestKm(c, o->Position, fromJd, toJd);
				if (closest > RangeKm || closest > space.SensorRangeKm(o->Position, RangeKm)) continue;
				// A clan does not answer its own lure.
				if (o->LureBy != nullptr && c->Leader()->Faction == o->LureBy) continue;
				Answer(space, c, o, toJd);
				break;
			}
		}
	}

	void DistressCalls::Call(DeepSpace& space, double jd, cs::Random& r)
	{
		auto route = space.Routes()[r.Next(space.Routes().Count())];
		auto line = route->AtB.KmFrom(route->AtA);
		auto dir = line.LengthSquared() > 0 ? line.Normalized() : DVec3::UnitX;
		auto side = DVec3::Cross(dir, cs::Math::Abs(dir.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX).Normalized();
		double along = 0.15 + 0.7 * r.NextDouble();
		double offKm = r.NextDouble() * 5e8;
		auto at = GalacticPosition::Lerp(route->AtA, route->AtB, along).Offset(side * offKm);
		cs::Ref<Faction> lure = nullptr;
		if (r.NextDouble() < LureShare && space.Galaxy() != nullptr)
		{
			cs::List<cs::Ref<Faction>> clans;
			for (const auto& f : space.Galaxy()->Polities->Factions())
				if (f->IsPirate) clans.Add(f);
			if (clans.Count() > 0) lure = clans[r.Next(clans.Count())];
		}
		static const cs::List<cs::String> hulls{ "Mercy", "Tern", "Bright Promise", "Quiet Harbour", "Sparrow", "Northern Light", "Second Chance", "Lodestar" };
		auto o = cs::New<DeepSpaceObject>();
		o->Kind = DeepSpaceObjectKind::Distress; o->Name = cs::Cat("the adrift ", hulls[r.Next(hulls.Count())]); o->Position = at; o->SinceJd = jd;
		o->Survivors = 5 + r.Next(60); o->ExpiresJd = jd + 15 + r.NextDouble() * 25; o->SalvageCredits = 40000 + r.NextDouble() * 160000;
		o->RouteA = route->A; o->RouteB = route->B; o->OffNetwork = !route->OnNetwork; o->LureBy = lure;
		space.Add(o);
		space.Log(jd, cs::Cat("A distress call goes out from ", o->Name, " between ", route->A, " and ", route->B, ": ", o->Survivors, " aboard"), at);
	}

	void DistressCalls::Answer(DeepSpace& space, cs::Ref<Cruise> c, cs::Ref<DeepSpaceObject> o, double jd)
	{
		auto leader = c->Leader();
		space.Remove(o);
		if (o->LureBy != nullptr)
		{
			double strength = DeepSpace::Strength(c);
			if (leader->Spec->IsMilitary && !leader->Faction->IsPirate && strength > LureStrength)
			{
				double bounty = 2 * o->SalvageCredits;
				leader->Credits += bounty;
				leader->Faction->Treasury += bounty;
				space.Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " answers ", o->Name, " and finds a ", o->LureBy->Name, " lure; it beats the raiders off ",
					"and takes ", cs::Fmt(bounty, "N0"), " cr in bounty"), o->Position);
			}
			else
			{
				double taken = 0;
				for (const auto& s : c->Ships)
				{
					if (s->HasCargo)
					{
						taken += cs::Math::Max(0.0, s->CargoCost);
						s->HasCargo = false;
						s->CargoTonnes = 0;
						s->CargoCost = 0;
						s->Cargo = cs::String();
					}
					double purse = s->Credits * 0.25;
					s->Credits -= purse;
					taken += purse;
					if (s->Combat != nullptr) s->Combat->Hull = cs::Math::Max(s->Combat->Spec->Hull * 0.2, s->Combat->Hull - s->Combat->Spec->Hull * 0.25);
				}
				o->LureBy->Treasury += taken;
				space.Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " answers ", o->Name, ": a ", o->LureBy->Name, " lure; the raiders rob it of ",
					cs::Fmt(taken, "N0"), " cr and let it go"), o->Position);
			}
			space.DropOut(c, jd, CruiseMeeting::FightDays);
			return;
		}
		if (leader->Faction->IsPirate)
		{
			leader->Credits += o->SalvageCredits;
			leader->Faction->Treasury += o->SalvageCredits;
			space.Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " answers ", o->Name, " and strips it, ",
				cs::Fmt(o->SalvageCredits, "N0"), " cr of plunder; its ", o->Survivors, " are left to the dark"), o->Position);
			space.DropOut(c, jd, WreckSalvage::SalvageDays);
			return;
		}
		double reward = BountyPerSoul * o->Survivors;
		leader->Credits += reward;
		leader->Faction->Treasury += reward;
		space.Rescued += o->Survivors;
		space.Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " answers the distress call of ", o->Name, " and takes off its ", o->Survivors, " survivors ",
			"(", cs::Fmt(reward, "N0"), " cr rescue bounty)"), o->Position);
		space.DropOut(c, jd, RescueDays);
	}

	// ================================================================ DeepSpaceOutposts

	void DeepSpaceOutposts::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		if (space.Objects().Count() == 0 || space.Cruises().Count() == 0) return;
		for (const auto& o : space.Objects().ToList())
		{
			if ((o->Kind != DeepSpaceObjectKind::Relay && o->Kind != DeepSpaceObjectKind::ListeningPost) || o->Owner == nullptr) continue;
			for (const auto& c : space.Cruises())
			{
				if (c->Ships.Count() == 0 || !(c->Engaged(toJd) || c->Engaged(fromJd))) continue;
				auto leader = c->Leader();
				if (!space.Hostile(o->Owner, leader->Faction)) continue;
				double near = DeepSpace::ClosestKm(c, o->Position, fromJd, toJd);
				if ((leader->Faction->IsPirate || leader->Spec->IsMilitary) && DeepSpace::Strength(c) > 0 && near <= CruiseMeeting::RangeKm)
				{
					KnockOut(space, c, o, toJd);
					break;
				}
				if (o->Kind != DeepSpaceObjectKind::ListeningPost || near > ListenRangeKm || near > space.SensorRangeKm(o->Position, ListenRangeKm)) continue;
				if (!o->Heard) o->Heard = cs::New<cs::HashSet<cs::Ref<Ship>>>();
				if (!o->Heard->Add(leader)) continue;
				cs::String text = cs::Cat(o->Name, " hears ", leader->Name, c->Ships.Count() > 1 ? cs::Cat(" and ", c->Ships.Count() - 1, " more") : cs::String(), " of ", leader->Faction->Name, " pass, bound for ", c->ToName);
				space.Log(toJd, text, o->Position);
				auto home = space.Galaxy() != nullptr ? space.Galaxy()->Find(o->HomeName) : nullptr;
				if (home != nullptr) home->World->Log(toJd, text, WorldEventKind::Info, home->Catalog->Root()->Index);
			}
		}
	}

	void DeepSpaceOutposts::KnockOut(DeepSpace& space, cs::Ref<Cruise> c, cs::Ref<DeepSpaceObject> o, double jd)
	{
		auto leader = c->Leader();
		space.Remove(o);
		{
			auto wreck = cs::New<DeepSpaceObject>();
			wreck->Kind = DeepSpaceObjectKind::Wreck; wreck->Name = cs::Cat("the wreck of ", o->Name); wreck->Position = o->Position; wreck->SalvageCredits = DeepSpace::WreckPartsCredits * 2;
			wreck->LostBy = o->Owner; wreck->SinceJd = jd; wreck->RouteA = o->RouteA; wreck->RouteB = o->RouteB; wreck->OffNetwork = o->OffNetwork;
			space.Add(wreck);
		}
		cs::String text = cs::Cat(leader->Name, " of ", leader->Faction->Name, " knocks out ", o->Name);
		space.Log(jd, text, o->Position);
		auto home = space.Galaxy() != nullptr ? space.Galaxy()->Find(o->HomeName) : nullptr;
		if (home != nullptr) home->World->Log(jd, text, WorldEventKind::Loss, home->Catalog->Root()->Index);
		space.DropOut(c, jd, CruiseMeeting::FightDays);
	}

	// ================================================================ RouteAmbushes

	void RouteAmbushes::Step(DeepSpace& space, double /*fromJd*/, double toJd, cs::Random& /*rng*/)
	{
		if (space.Ambushes().Count() > 0) space.LiftAmbushes(toJd);
	}
}
