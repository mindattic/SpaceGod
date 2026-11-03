// Port of Bench/CSharp/src/Digest.cs (see Digest.h).

#include "Digest.h"
#include "Simulation.h"

#include "SimClock.h"
#include "World.h"
#include "Fleet.h"
#include "Ships.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Combat/War.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Wreck.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"

#include <cstdio>
#include <cstring>

namespace Bench
{
	using namespace SpaceGod::Core;

	namespace
	{
		std::string I(int i) { return std::to_string(i); }
		std::string B(bool b) { return b ? "1" : "0"; }
		std::string V(const DVec3& v) { return Digest::H(v.X) + "," + Digest::H(v.Y) + "," + Digest::H(v.Z); }
		std::string S(const std::string& s)
		{
			if (s.empty()) return "-";
			std::string r = s;
			for (char& c : r)
				if (c == ' ' || c == '\n' || c == '\r') c = '_';
			return r;
		}
		std::string F(const cs::Ref<Faction>& f) { return f == nullptr ? "-" : S(f->Prefix); }

		void WriteBattle(std::string& sb, const cs::Ref<Battle>& bt)
		{
			int present = 0;
			for (const auto& u : bt->Units) if (u->Present()) present++;
			sb += "battle " + S(bt->Name) + " units=" + I(bt->Units.Count()) + " present=" + I(present) + " over=" + B(bt->Over) + " winner=" + I(bt->Winner) + "\n";
			for (const auto& u : bt->Units)
				sb += "unit " + I(u->Id) + " side=" + I(u->Side) + " " + ToString(u->Kind) + " " + F(u->Faction) + " present=" + B(u->Present()) +
					" pos=" + V(u->Pos) + " vel=" + V(u->Vel) + " hull=" + (u->Combat != nullptr ? Digest::H(u->Combat->Hull) : std::string("-")) + "\n";
		}
	}

	std::string Digest::H(double d)
	{
		uint64_t bits;
		std::memcpy(&bits, &d, sizeof bits);
		return Hex(bits);
	}

	uint64_t Digest::Fnv1a64(const std::string& text)
	{
		uint64_t h = 0xCBF29CE484222325ULL;
		for (unsigned char b : text)
		{
			h ^= b;
			h *= 0x100000001B3ULL;
		}
		return h;
	}

	std::string Digest::Hex(uint64_t h)
	{
		char buf[17];
		std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(h));
		return buf;
	}

	std::string Digest::Write(Simulation& sim, const std::string& scenario)
	{
		std::string sb;
		sb.reserve(1 << 20);
		auto L = [&sb](const std::string& line) { sb += line; sb += '\n'; };

		auto& g = *sim.Galaxy;
		L(Version);
		L("scenario " + S(scenario));
		L("clock jd=" + H(sim.Clock->Jd) + " warp=" + I(sim.Clock->WarpIndex) + " paused=" + B(sim.Clock->Paused()));
		int inTransit = 0;
		if (g.Traffic != nullptr) inTransit = g.Traffic->InTransit().Count();
		L("galaxy systems=" + I(g.Systems.Count()) + " nearby=" + I(g.NearbyStars.Count()) + " intransit=" + I(inTransit) +
			" focus=" + S(g.Focus != nullptr ? g.Focus->Name : std::string()) + " active=" + S(sim.ActiveSystem->Name) + " battlesStarted=" + I(sim.BattlesStarted));
		std::string arrivals = "arrivals";
		for (int p = static_cast<int>(VoyagePurpose::Trade); p <= static_cast<int>(VoyagePurpose::Colonise); p++)
		{
			int n = 0;
			if (g.Traffic != nullptr) g.Traffic->Arrivals.TryGetValue(static_cast<VoyagePurpose>(p), n);
			arrivals += std::string(" ") + ToString(static_cast<VoyagePurpose>(p)) + "=" + I(n);
		}
		L(arrivals);
		if (sim.CourseShip != nullptr)
		{
			auto in = sim.SystemHolding(sim.CourseShip);
			L("course ship=" + S(sim.CourseShip->Name) + " to=" + S(sim.CourseDestination != nullptr ? sim.CourseDestination->Name : std::string()) +
				" ended=" + B(sim.CourseEnded) + " arrived=" + B(sim.CourseArrived) + " in=" + S(in != nullptr ? in->Name : std::string()));
		}
		if (sim.Skirmish != nullptr)
		{
			L("skirmish");
			WriteBattle(sb, sim.Skirmish);
		}

		for (int si = 0; si < g.Systems.Count(); si++)
		{
			const auto& sys = g.Systems[si];
			const auto& w = sys->World;
			const auto& cat = sys->Catalog;
			const auto& ships = w->Fleet->Ships();
			std::string evb;
			for (const auto& e : w->Events)
				evb += H(e.Jd) + "|" + ToString(e.Kind) + "|" + I(e.Body) + "|" + e.Text + "\n";
			uint64_t eh = Fnv1a64(evb);
			L("system " + I(si) + " " + S(sys->Name) + " jd=" + H(w->Jd) + " coarse=" + B(w->Coarse()) + " bodies=" + I(cat->Count()) +
				" ships=" + I(ships.Count()) + " settlements=" + I(w->Economy->Settlements.Count()) + " battles=" + I(w->War->Battles.Count()) +
				" ended=" + I(w->War->Ended.Count()) + " wrecks=" + I(w->War->Wrecks->Items.Count()) + " events=" + I(w->Events.Count()) + " eventsHash=" + Hex(eh));

			for (int b = 0; b < cat->Count(); b++)
				L("body " + I(b) + " " + S((*cat)[b]->Name) + " " + V(sys->Ephemeris->Helio(b)));

			for (const auto& f : w->Fleet->Factions())
				L("faction " + F(f) + " " + S(f->Name) + " treasury=" + H(f->Treasury) + " losses=" + I(w->War->Losses(f)));

			for (const auto& st : w->Economy->Settlements)
			{
				std::string stock;
				for (int k = 0; k < st->Stock.Count(); k++)
				{
					if (k > 0) stock += ',';
					stock += H(st->Stock[k]);
				}
				L("settlement " + I(st->Id) + " " + S(st->Name) + " owner=" + F(st->Owner) + " pop=" + H(st->Population) + " stock=" + stock);
			}

			double jd = w->Jd;
			for (int k = 0; k < ships.Count(); k++)
			{
				const auto& s = ships[k];
				std::string line;
				line += "ship " + I(s->Id) + " " + S(s->Name) + " " + (s->Spec != nullptr ? std::string(ToString(s->Spec->Class)) : std::string("-")) +
					" " + F(s->Faction) + " phase=" + ToString(s->Phase) + " destroyed=" + B(s->Destroyed) +
					" battle=" + B(s->Battle != nullptr) + " voyage=" + B(s->Voyage != nullptr) +
					" hull=" + (s->Combat != nullptr ? H(s->Combat->Hull) : std::string("-"));
				if (!s->Destroyed)
				{
					try
					{
						auto state = w->Fleet->StateOf(s, jd);
						line += " pos=" + V(state.HelioKm) + " facing=" + V(state.Facing) + " speed=" + H(state.SpeedKmS);
					}
					catch (const std::exception&) { line += " pos=! facing=! speed=!"; }
					try { auto v = w->Fleet->HelioVelocityOf(s, jd); line += " vel=" + V(v); }
					catch (const std::exception&) { line += " vel=!"; }
				}
				L(line);
			}

			for (const auto& bt : w->War->Battles) WriteBattle(sb, bt);
		}
		L("end");
		return sb;
	}

	std::string Digest::TraceLine(int tick, const std::string& text)
	{
		std::string out = "tick " + std::to_string(tick);
		size_t start = 0;
		int section = -1;
		auto emit = [&](size_t end) {
			out += (section < 0 ? std::string(" head=") : " s" + std::to_string(section) + "=") + Hex(Fnv1a64(text.substr(start, end - start)));
		};
		for (size_t pos = 0; pos < text.size();)
		{
			size_t eol = text.find('\n', pos);
			if (eol == std::string::npos) eol = text.size();
			if (text.compare(pos, 7, "system ") == 0)
			{
				emit(pos);
				start = pos;
				section++;
			}
			pos = eol + 1;
		}
		emit(text.size());
		return out;
	}
}
