// Port of Assets/UnitySpace/Core/Systems/SystemSources.cs (US-T17).

#include "Systems/SystemSources.h"
#include "Systems/CsGaps.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/Galaxy.h"
#include "Systems/AlphaCentauri.h"
#include "Systems/NearbySystems.h"
#include "Systems/FrontierSystems.h"
#include "Systems/VoidStations.h"
#include "Systems/CatalogueStars.h"
#include "Systems/ProceduralSystems.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	// ---------------------------------------------------------------- StarEntry

	GalacticPosition StarEntry::Origin() const { return GalacticPosition::FromLy(PositionLy); }

	NearbyStar StarEntry::ToNearbyStar() const
	{
		double mag = ApparentMagnitude;
		double d = PositionLy.Length();
		if (cs::Double::IsNaN(mag) && LuminositySolar > 0 && d > 0)
			mag = 4.83 - 2.5 * cs::Math::Log10(LuminositySolar) + 5 * (cs::Math::Log10(d / 3.261563777) - 1);
		return NearbyStar(Name, PositionLy, Spectral, mag, TeffK);
	}

	// ---------------------------------------------------------------- SystemSource

	cs::Ref<StarSystem> SystemSource::Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities)
	{
		(void)star;
		return StarSystem::Create(spec, jd, nullptr, polities);
	}

	// ---------------------------------------------------------------- SystemSources

	cs::List<cs::Ref<SystemSource>>& SystemSources::sources()
	{
		static cs::List<cs::Ref<SystemSource>> list
		{
			cs::New<HandBuiltSystems>(),
			cs::New<VoidStations>(),
			cs::New<CatalogueStars>(),
			cs::New<ProceduralSystems>(),
		};
		return list;
	}

	std::recursive_mutex& SystemSources::gate()
	{
		static std::recursive_mutex m;
		return m;
	}

	cs::List<cs::Ref<SystemContent>>& SystemSources::Content()
	{
		static cs::List<cs::Ref<SystemContent>> content;
		return content;
	}

	cs::List<cs::Ref<SystemSource>> SystemSources::All()
	{
		std::lock_guard<std::recursive_mutex> lock(gate());
		return sources().ToArray();
	}

	void SystemSources::Register(const cs::Ref<SystemSource>& source)
	{
		if (source == nullptr) throw cs::ArgumentNullException("source");
		std::lock_guard<std::recursive_mutex> lock(gate());
		auto& list = sources();
		list.RemoveAll([&](const cs::Ref<SystemSource>& s) { return EqualsNoCase(s->Name(), source->Name()); });
		int at = 0;
		while (at < list.Count() && list[at]->Order() <= source->Order()) at++;
		list.Insert(at, source);
	}

	bool SystemSources::Unregister(const cs::String& name)
	{
		std::lock_guard<std::recursive_mutex> lock(gate());
		return sources().RemoveAll([&](const cs::Ref<SystemSource>& s) { return EqualsNoCase(s->Name(), name); }) > 0;
	}

	cs::Ref<SystemSource> SystemSources::Find(const cs::String& name)
	{
		{
			std::lock_guard<std::recursive_mutex> lock(gate());
			for (const auto& s : sources())
				if (EqualsNoCase(s->Name(), name)) return s;
		}
		return nullptr;
	}

	cs::List<cs::Ref<StarEntry>> SystemSources::StarsNear(DVec3 centreLy, double radiusLy, int seed, cs::Func<bool, cs::Ref<SystemSource>> which)
	{
		cs::List<cs::Ref<StarEntry>> list;
		cs::HashSet<NoCase> names;
		auto order = All();
		for (int i = 0; i < order.Count(); i++)
		{
			if (which != nullptr && !which(order[i])) continue;
			for (const auto& e : order[i]->StarsNear(centreLy, radiusLy, seed))
			{
				if (e == nullptr || (e->PositionLy - centreLy).Length() > radiusLy || names.Contains(e->Name)) continue;
				bool claimed = false;
				for (int j = 0; j < i && !claimed; j++) claimed = order[j]->Claims(e->PositionLy);
				if (claimed) continue;
				e->Source = order[i]->Name();
				names.Add(e->Name);
				list.Add(e);
			}
		}
		list.Sort([&](const cs::Ref<StarEntry>& a, const cs::Ref<StarEntry>& b)
		{
			int c = CompareTo((a->PositionLy - centreLy).Length(), (b->PositionLy - centreLy).Length());
			return c != 0 ? c : cs::Strings::CompareOrdinal(a->Name, b->Name);
		});
		return list;
	}

	cs::Ref<StarEntry> SystemSources::Entry(const cs::String& name, DVec3 nearLy, int seed)
	{
		for (const auto& e : StarsNear(nearLy, 1, seed))
			if (EqualsNoCase(e->Name, name)) return e;
		return nullptr;
	}

	cs::Ref<SystemSpec> SystemSources::Define(const cs::Ref<StarEntry>& star, int seed)
	{
		auto source = star == nullptr ? nullptr : Find(star->Source);
		auto spec = source ? source->Define(star, seed) : nullptr;
		if (spec == nullptr) return nullptr;
		for (const auto& c : Content().ToArray()) c->Furnish(star, spec, seed);
		return spec;
	}

	cs::Ref<StarSystem> SystemSources::Build(const cs::Ref<StarEntry>& star, double jd, int seed, const cs::Ref<Polities>& polities)
	{
		auto spec = Define(star, seed);
		return spec == nullptr ? nullptr : Find(star->Source)->Build(star, spec, jd, polities);
	}

	const cs::List<SystemSources::MainSequenceRow>& SystemSources::MainSequence()
	{
		static const cs::List<MainSequenceRow> rows
		{
			{ 2570, 0.079 }, { 2680, 0.090 }, { 2810, 0.102 }, { 3060, 0.162 }, { 3210, 0.23 }, { 3430, 0.37 }, { 3560, 0.44 }, { 3660, 0.50 },
			{ 3850, 0.57 }, { 4440, 0.69 }, { 5270, 0.88 }, { 5770, 1.0 }, { 6510, 1.21 }, { 7220, 1.61 }, { 8550, 2.0 }, { 9700, 2.34 },
			{ 15700, 4.4 }, { 31400, 17 },
		};
		return rows;
	}

	double SystemSources::MainSequenceMass(double teffK)
	{
		const auto& ms = MainSequence();
		if (teffK <= ms[0].teff) return ms[0].mass;
		for (int i = 1; i < ms.Count(); i++)
			if (teffK <= ms[i].teff)
			{
				double t0 = ms[i - 1].teff, m0 = ms[i - 1].mass;
				double t1 = ms[i].teff, m1 = ms[i].mass;
				return m0 + (teffK - t0) / (t1 - t0) * (m1 - m0);
			}
		return ms[ms.Count() - 1].mass;
	}

	cs::Ref<SystemSpec> SystemSources::LoneStar(const cs::Ref<StarEntry>& star, const cs::Ref<StarSpec>& spec, const cs::String& summary)
	{
		double d = star->PositionLy.Length();
		double ra = 0, dec = 0;
		if (d > 0) Astro::RaDecOf(star->PositionLy, ra, dec);
		auto s = cs::New<SystemSpec>();
		s->Name = star->Name; s->NeighbourName = star->Name; s->RaDeg = ra; s->DecDeg = dec; s->DistanceLy = d;
		s->Primary = StellarNode::Single(spec); s->Summary = summary;
		s->Stations.Add(FrontierSystems::Habitat(star->Name, spec));
		return s;
	}

	// ---------------------------------------------------------------- HandBuiltSystems

	cs::Ref<cs::List<cs::Ref<StarEntry>>>& HandBuiltSystems::stars()
	{
		static cs::Ref<cs::List<cs::Ref<StarEntry>>> list;
		return list;
	}

	cs::List<HandBuiltSystems::TaggedSpec> HandBuiltSystems::Specs()
	{
		cs::List<TaggedSpec> r;
		r.Add({ "alpha", AlphaCentauri::Spec() });
		for (const auto& s : NearbySystems::All()) r.Add({ "nearby", s });
		for (const auto& s : FrontierSystems::All()) r.Add({ "frontier", s });
		return r;
	}

	const cs::List<cs::Ref<StarEntry>>& HandBuiltSystems::Stars()
	{
		if (stars() != nullptr) return *stars();
		auto list = cs::New<cs::List<cs::Ref<StarEntry>>>();
		for (const auto& [tag, spec] : Specs())
		{
			cs::Ref<StarSpec> first = nullptr;
			for (const auto& st : spec->Stars())
			{
				first = st;
				break;
			}
			auto e = cs::New<StarEntry>();
			e->Name = !spec->NeighbourName.empty() ? spec->NeighbourName : spec->Name; e->PositionLy = spec->PositionLy();
			e->Spectral = first ? first->Spectral : cs::String();
			e->MassSolar = spec->Primary->MassSolar(); e->RadiusSolar = first ? first->RadiusSolar : 0; e->LuminositySolar = first ? first->LuminositySolar : 0;
			e->TeffK = first ? first->TeffK : 0; e->Source = SourceName; e->Tag = tag;
			list->Add(e);
		}
		stars() = list;
		return *list;
	}

	cs::List<cs::Ref<StarEntry>> HandBuiltSystems::StarsNear(DVec3 centreLy, double radiusLy, int seed)
	{
		(void)seed;
		cs::List<cs::Ref<StarEntry>> r;
		for (const auto& e : Stars())
			if ((e->PositionLy - centreLy).Length() <= radiusLy) r.Add(Copy(e));
		return r;
	}

	cs::Ref<StarEntry> HandBuiltSystems::Copy(const cs::Ref<StarEntry>& e)
	{
		auto c = cs::New<StarEntry>();
		c->Name = e->Name; c->PositionLy = e->PositionLy; c->Spectral = e->Spectral; c->MassSolar = e->MassSolar; c->RadiusSolar = e->RadiusSolar;
		c->LuminositySolar = e->LuminositySolar; c->TeffK = e->TeffK; c->ApparentMagnitude = e->ApparentMagnitude; c->Source = e->Source; c->Tag = e->Tag;
		return c;
	}

	cs::Ref<SystemSpec> HandBuiltSystems::Define(const cs::Ref<StarEntry>& star, int seed)
	{
		(void)seed;
		for (const auto& [tag, spec] : Specs())
			if (EqualsNoCase(!spec->NeighbourName.empty() ? spec->NeighbourName : spec->Name, star->Name)) return spec;
		return nullptr;
	}

	cs::Ref<StarSystem> HandBuiltSystems::Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities)
	{
		if (spec->Name == "Alpha Centauri") return StarSystem::CreateAlphaCentauri(jd, polities);
		if (star->Tag == "nearby") return StarSystem::CreateSettled(spec, jd, polities);
		return StarSystem::Create(spec, jd, nullptr, polities);
	}
}
