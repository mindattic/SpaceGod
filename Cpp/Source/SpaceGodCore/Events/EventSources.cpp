// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/EventSources.cs (see docs/MIGRATION.md).

#include "Events/EventSources.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Crews.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Law/Law.h"
#include "Ships.h"
#include "Cs/Cs.h"
#include "World.h"

namespace SpaceGod::Core
{
	PodState EventSources::DefaultPickUpPod(cs::Ref<EscapePod> p, cs::Ref<Ship> s)
	{
		p->State = PodState::Rescued;
		p->TakenBy = s ? s->Faction : nullptr;
		return p->State;
	}

	bool EventSources::DefaultIsMerchant(cs::Ref<Ship> s)
	{
		return s->Role == ShipRole::Trader || s->Role == ShipRole::Miner || s->Role == ShipRole::Courier;
	}

	cs::Ref<EventSources> EventSources::Of(const cs::Ref<World>& world, double jd)
	{
		auto eph = world->Ephemeris;
		auto fleet = world->Fleet;
		auto war = world->War;
		auto lanes = fleet->Lanes;
		auto catalog = world->Catalog();
		auto r = cs::New<EventSources>();
		r->Ships = fleet->Ships();
		r->Wrecks = war->Wrecks->Items;
		r->ShipHelio = [fleet, jd](const cs::Ref<Ship>& s) { return fleet->StateOf(s, jd).HelioKm; };
		r->BodyHelio = [eph, jd](int b) { return eph->HelioAt(b, jd); };
		r->BodyName = [catalog](int b) { return (*catalog)[b]->Name; };
		r->IsMerchant = [world](const cs::Ref<Ship>& s) { return world->IsTrader(s); };
		r->PirateHunters = world->Belt;
		r->WillHire = [world](const cs::Ref<Faction>& f) { return Law::OffersContracts(world->Law->Player, f); };
		// C# `PostedBounty = world.Law.BountyOn`: a method group, bound to the Law instance now.
		auto law = world->Law;
		r->PostedBounty = [law](const cs::Ref<Ship>& s) { return law->BountyOn(s); };
		r->WreckHelio = [world, jd](const cs::Ref<Wreck>& w) { return world->WreckHelio(w, jd); };
		r->Pods = war->Crews->Pods;
		r->PodHelio = [world, jd](const cs::Ref<EscapePod>& p) { return world->PodHelio(p, jd); };
		r->PickUpPod = [world, jd](const cs::Ref<EscapePod>& p, const cs::Ref<Ship>& s) { return world->PickUpPods(p, s, jd); };
		r->LaneDanger = [lanes, war](const cs::Ref<Lane>& lane)
		{
			double d = 0;
			if (lanes != nullptr) lanes->Danger.TryGetValue(lane, d);
			return d + EndDangerWeight * (war->Danger(lane->A) + war->Danger(lane->B));
		};
		r->NearestBody = [catalog, eph, jd](DVec3 p)
		{
			int best = 0;
			double bestD = cs::Double::MaxValue;
			for (int i = 1; i < catalog->Count(); i++)
			{
				double d = (eph->HelioAt(i, jd) - p).Length();
				if (d < bestD) { bestD = d; best = i; }
			}
			return best;
		};
		return r;
	}

	const cs::List<cs::String>& PirateCaptains::First()
	{
		static const cs::List<cs::String> value
		{
			"Mara", "Dagny", "Silas", "Orrin", "Vesna", "Tobiah", "Ines", "Kallum", "Rook", "Yara",
			"Bram", "Sefa", "Ludo", "Nkem", "Petra", "Ash", "Coralie", "Jory", "Hollis", "Zev",
		};
		return value;
	}

	const cs::List<cs::String>& PirateCaptains::Last()
	{
		static const cs::List<cs::String> value
		{
			"Voss", "Kade", "Marrow", "Thorne", "Okafor", "Lisk", "Varga", "Cray", "Hale", "Seddon",
			"Brask", "Ivers", "Quell", "Mendez", "Strand", "Tolliver", "Raske", "Ngata", "Fenn", "Ulver",
		};
		return value;
	}

	const cs::List<cs::String>& PirateCaptains::Epithet()
	{
		static const cs::List<cs::String> value
		{
			"", "", "", " \"the Knife\"", " \"Cold Iron\"", " \"Lanebreaker\"", " \"the Widow\"", " \"Red Ledger\"",
		};
		return value;
	}

	cs::String PirateCaptains::NameFor(const cs::Ref<Ship>& ship)
	{
		uint32_t h = Hash((static_cast<uint32_t>(ship->Id) * 2654435761u) ^ static_cast<uint32_t>(StableHash(ship->Name)));
		return cs::Cat("Captain ", First()[static_cast<int>(h % static_cast<uint32_t>(First().Count()))],
			Epithet()[static_cast<int>((h >> 8) % static_cast<uint32_t>(Epithet().Count()))], " ",
			Last()[static_cast<int>((h >> 16) % static_cast<uint32_t>(Last().Count()))]);
	}

	int PirateCaptains::StableHash(const cs::String& s)
	{
		// C# `if (s == null) return 0;`: a null string is the empty string here, which hashes 23 rather than 0.
		// A ship always has a name, so no ship reaches that branch.
		// unchecked: h * 31 + c wraps, over the string's UTF-16 code units as C#'s foreach (char c in s).
		int h = 23;
		cs::ForEachUtf16(s, [&](uint32_t c) { h = cs::WrapAdd(cs::WrapMul(h, 31), static_cast<int>(c)); });
		return h;
	}

	uint32_t PirateCaptains::Hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}
}
