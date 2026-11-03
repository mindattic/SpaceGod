// Port of Assets/UnitySpace/Core/Combat/Boarding.cs (see docs/MIGRATION.md).

#include "Combat/Boarding.h"

#include "Combat/Facilities.h"
#include "Combat/MarineHabitats.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(CompartmentKind v)
	{
		switch (v)
		{
			case CompartmentKind::Bow: return "Bow";
			case CompartmentKind::Hangar: return "Hangar";
			case CompartmentKind::Hold: return "Hold";
			case CompartmentKind::Quarters: return "Quarters";
			case CompartmentKind::Battery: return "Battery";
			case CompartmentKind::Midships: return "Midships";
			case CompartmentKind::Bridge: return "Bridge";
			case CompartmentKind::Engineering: return "Engineering";
			case CompartmentKind::BreacherBay: return "BreacherBay";
			case CompartmentKind::Barracks: return "Barracks";
		}
		return "";
	}

	double Interior::DrillFactor() const { return MarineHabitats::Factor(Training); }

	double Interior::Crew() const
	{
		double n = 0;
		for (const auto& c : Compartments) n += c->Crew;
		return n;
	}

	double Interior::Marines() const
	{
		double n = 0;
		for (const auto& c : Compartments) n += c->Marines;
		return n;
	}

	double Interior::Boarders() const
	{
		double n = 0;
		for (const auto& c : Compartments) n += c->Boarders;
		return n;
	}

	bool Interior::UnderBoarding() const { return Boarder != nullptr && Boarders() > 0.5 && !Captured; }

	bool Interior::Admits(const cs::Ref<Faction>& faction, int side) const
	{
		return !Captured && (Boarder == nullptr || Boarder == faction || (side >= 0 && side == BoarderSide));
	}

	bool Interior::IsParty(const cs::Ref<Faction>& faction) const
	{
		if (faction == nullptr || Boarder == nullptr) return false;
		if (Boarder == faction) return true;
		for (const auto& p : Parties) if (p->Faction == faction) return true;
		return false;
	}

	cs::String Interior::BoarderNames() const
	{
		if (Parties.Count() <= 1) return Boarder ? Boarder->Name : cs::String("");
		cs::List<cs::String> names;
		for (const auto& p : Parties) names.Add(p->Faction->Name);
		// names.GetRange(0, names.Count - 1)
		cs::List<cs::String> head(names.begin(), names.begin() + (names.Count() - 1));
		return names.Count() == 2 ? names[0] + " and " + names[1] : cs::Strings::Join(", ", head) + " and " + names[names.Count() - 1];
	}

	double Interior::Survivors() const
	{
		double n = 0;
		for (const auto& c : Compartments) n += c->Survivors;
		return n;
	}

	int Interior::TakenCount() const
	{
		int n = 0;
		for (const auto& c : Compartments) if (c->Taken) n++;
		return n;
	}

	cs::Ref<Compartment> Interior::Of(CompartmentKind kind) const
	{
		for (const auto& c : Compartments)
			if (c->Kind == kind) return c;
		return nullptr;
	}

	cs::Ref<Compartment> Interior::Nearest(double z, double y) const
	{
		cs::Ref<Compartment> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& c : Compartments)
		{
			if (c->Buried) continue;
			double d = cs::Math::Abs(c->Z - z) + cs::Math::Abs(c->Y - y) * 0.5;
			if (d < bestD)
			{
				bestD = d;
				best = c;
			}
		}
		if (best) return best;
		return Compartments.Count() > 0 ? Compartments[0] : nullptr;
	}

	// ---------------------------------------------------------------- building

	cs::Ref<Interior> Interior::For(ShipClass cls, double crew, double marines) { return For(FacilityLayouts::For(cls), crew, marines); }

	cs::Ref<Interior> Interior::ForPlatform(double crew, double marines) { return For(FacilityLayouts::Platform(), crew, marines); }

	cs::Ref<Interior> Interior::For(const cs::List<cs::Ref<FacilitySite>>& facilities, double crew, double marines)
	{
		auto i = cs::New<Interior>();
		// C# `facilities?.Count ?? 0`: a list reference here is never null.
		auto made = cs::NewArray<cs::Ref<Compartment>>(facilities.Count());
		cs::Ref<Compartment> last = nullptr;
		for (int f = 0; f < made.Count(); f++)
		{
			auto site = facilities[f];
			auto def = site->Def();
			if (!(def != nullptr && def->Compartment.has_value())) continue;
			CompartmentKind kind = *def->Compartment;
			auto c = cs::New<Compartment>();
			c->Index = i->Compartments.Count();
			c->Name = site->Name;
			c->Kind = kind;
			c->Z = site->Z;
			c->Y = site->Y;
			c->Buried = site->Buried;
			cs::Ref<Compartment> parent;
			if (site->Link == FacilitySite::Keel) parent = last;
			else if (site->Link >= 0 && site->Link < made.Count()) parent = made[site->Link];
			else
			{
				parent = i->Of(CompartmentKind::Midships);
				if (!parent) parent = i->Compartments.Count() > 0 ? i->Compartments[i->Compartments.Count() / 2] : nullptr;
			}
			if (site->Link == FacilitySite::Amidships && parent != nullptr)
			{
				c->Z = parent->Z;
				c->Y = parent->Y - 0.05;
			}
			if (parent != nullptr)
			{
				parent->Links.Add(c->Index);
				c->Links.Add(parent->Index);
			}
			i->Compartments.Add(c);
			made[f] = c;
			if (site->Link == FacilitySite::Keel) last = c;
		}
		if (i->Compartments.Count() == 0)
		{
			auto hull = cs::New<Compartment>();
			hull->Index = 0;
			hull->Name = "Hull";
			hull->Kind = CompartmentKind::Midships;
			i->Compartments.Add(hull);
		}
		i->Crew0(crew, marines);
		return i;
	}

	cs::List<cs::Ref<Compartment>> Interior::Barracks() const
	{
		cs::List<cs::Ref<Compartment>> r;
		for (const auto& c : Compartments) if (c->Kind == CompartmentKind::Barracks) r.Add(c);
		return r;
	}

	bool Interior::HasCrewSpace() const
	{
		for (const auto& c : Compartments) if (c->Kind != CompartmentKind::Barracks) return true;
		return false;
	}

	int Interior::CrewSpaces() const
	{
		int n = 0;
		for (const auto& c : Compartments) if (CrewSpace(c)) n++;
		return n;
	}

	void Interior::SpreadCrew(double crew, bool add)
	{
		int n = CrewSpaces();
		for (const auto& c : Compartments)
		{
			if (!CrewSpace(c)) { if (!add) c->Crew = 0; continue; }
			c->Crew = (add ? c->Crew : 0) + crew / n;
		}
	}

	void Interior::Quarter(double marines)
	{
		if (marines <= 0) return;
		int n = 0;
		for (const auto& _ : Barracks()) { (void)_; n++; }
		if (n > 0)
		{
			for (const auto& c : Barracks()) c->Marines += marines / n;
			return;
		}
		auto into = Of(CompartmentKind::Quarters);
		if (!into) into = Of(CompartmentKind::Midships);
		if (!into) into = Of(CompartmentKind::BreacherBay);
		if (!into) into = Compartments[Compartments.Count() / 2];
		into->Marines += marines;
	}

	void Interior::Crew0(double crew, double marines)
	{
		SpreadCrew(crew, false);
		Quarter(marines);
	}

	void Interior::Drill(double days)
	{
		if (days <= 0 || UnderBoarding() || Captured) return;
		double inBarracks = 0;
		for (const auto& c : Barracks()) inBarracks += c->Marines;
		if (inBarracks < 0.5) return;
		Training = cs::Math::Min(1.0, Training + days / cs::Math::Max(1e-9, MarineHabitats::DrillDays()->Value));
	}

	// ---------------------------------------------------------------- orders (U.SPC-T468)

	bool Interior::Command(BoardingRole role, BoardingOrder order, int objective)
	{
		if (Boarder == nullptr || Captured) return false;
		if (order == BoardingOrder::Push && (objective < 0 || objective >= Compartments.Count())) return false;
		if (order != BoardingOrder::Push) objective = -1;
		if (role == BoardingRole::Boarders)
		{
			BoarderOrder = order;
			BoarderObjective = objective;
		}
		else
		{
			DefenderOrder = order;
			DefenderObjective = objective;
		}
		return true;
	}

	void Interior::ClearOrders()
	{
		BoarderOrder = DefenderOrder = BoardingOrder::Auto;
		BoarderObjective = DefenderObjective = -1;
	}

	// ---------------------------------------------------------------- the fight

	bool Interior::Board(const cs::Ref<Faction>& boarder, double quality, double marines, const cs::Ref<Compartment>& at, bool breach, double seconds, cs::Random& rng, int side)
	{
		if (!Admits(boarder, side)) return false;
		// Defenders are roused once per boarding: more boarders landing later don't stand them down again.
		bool fresh = Boarder == nullptr;
		if (fresh)
		{
			Boarder = boarder;
			BoarderSide = side;
			Parties.Clear();
			BoarderQuality = quality;
		}
		else
		{
			// Everyone aboard fights as one party, as well as the mix of them who landed.
			double aboard = Boarders();
			BoarderQuality = aboard + marines > 1e-9 ? (aboard * BoarderQuality + marines * quality) / (aboard + marines) : quality;
		}
		JoinParty(boarder, marines, quality);
		at->Boarders += marines;
		BoardersLanded += marines;
		if (breach && !at->Vented)
		{
			at->Vented = true;
			// Emergency bulkheads: quick with the bridge giving the order, slower without.
			at->SealIn = 4 + rng.NextDouble() * 10;
		}
		double react = 15 + rng.NextDouble() * 30;
		if (fresh)
		{
			reactIn = react;
			ClearOrders();
		}
		Note(seconds, at->Index, cs::Cat(cs::Fmt(marines, "0"), " ", boarder->Name, " marines board at ", at->Name) + (breach ? ", hull breached" : "")
			+ (boarder != Boarder ? cs::Cat(", joining the ", Boarder->Name) : cs::String("")));
		return true;
	}

	void Interior::JoinParty(const cs::Ref<Faction>& faction, double marines, double quality)
	{
		cs::Ref<BoardingParty> party = nullptr;
		for (const auto& p : Parties) if (p->Faction == faction) party = p;
		if (party == nullptr)
		{
			party = cs::New<BoardingParty>();
			party->Faction = faction;
			party->Order = Parties.Count();
			Parties.Add(party);
		}
		party->Landed += marines;
		party->Strength += marines * quality;
	}

	void Interior::EndBoarding()
	{
		Boarder = nullptr;
		BoarderSide = -1;
		Parties.Clear();
		ClearOrders();
	}

	bool Interior::Step(double dt, double seconds, cs::Random& rng)
	{
		(void)rng; // unused in the C# too
		if (Captured || Boarder == nullptr) return false;

		// Vacuum: crew caught in a holed compartment die until it is sealed.
		for (const auto& c : Compartments)
		{
			if (!c->Vented || c->SealIn <= 0) continue;
			double killed = c->Crew * cs::Math::Min(1.0, 0.06 * dt);
			c->Crew -= killed;
			CrewLost += killed;
			c->SealIn -= dt;
			if (c->SealIn <= 0) Note(seconds, c->Index, cs::Cat("Emergency bulkheads seal ", c->Name));
		}

		// Defending marines move toward the fighting once they have been roused.
		reactIn -= dt;
		if (reactIn <= 0 && Boarders() > 0.01 && DefenderOrder != BoardingOrder::Hold) Reinforce(dt);

		for (const auto& c : Compartments)
		{
			if (c->Boarders <= 0.01)
			{
				// Defenders who have fought their way back in hold it again.
				if (c->Taken && c->Crew + c->Marines > 0.01)
				{
					c->Taken = false;
					Note(seconds, c->Index, cs::Cat("The crew retake ", c->Name));
				}
				continue;
			}
			double defenders = c->Crew + c->Marines;
			if (defenders <= 0.01)
			{
				if (!c->Taken)
				{
					c->Taken = true;
					c->Crew = c->Marines = 0;
					Note(seconds, c->Index, cs::Cat("Boarders take ", c->Name));
				}
				continue;
			}
			double attack = c->Boarders * BoarderQuality;
			// The bridge and engineering are fortified: blast doors, prepared positions, a last stand.
			double fortified = c->Kind == CompartmentKind::Bridge || c->Kind == CompartmentKind::Engineering ? 1.6 : 1;
			double defence = (c->Marines * DefenderQuality * DrillFactor() + c->Crew * CrewFight) * DefenderAdvantage * fortified * (c->Vented ? 0.7 : 1);
			double defenderLosses = cs::Math::Min(defenders, attack * Lethality * dt);
			double boarderLosses = cs::Math::Min(c->Boarders, defence * Lethality * dt);
			// Marines stand in front; crew take what the marines cannot.
			double fromMarines = cs::Math::Min(c->Marines, cs::Math::Min(defenderLosses, defenderLosses * (c->Marines / defenders + 0.3)));
			double fromCrew = cs::Math::Min(c->Crew, defenderLosses - fromMarines);
			c->Marines -= fromMarines;
			c->Crew -= fromCrew;
			MarinesLost += fromMarines;
			CrewLost += fromCrew;
			c->Boarders -= boarderLosses;
			BoardersLost += boarderLosses;
		}

		// Boarders who hold a space push on toward the bridge and engineering (or the objective they were given).
		if (BoarderOrder != BoardingOrder::Hold) Advance(dt);
		// An objective the boarders were sent for is theirs: they press on as the resolver would.
		if (BoarderOrder == BoardingOrder::Push && Compartments[BoarderObjective]->Taken)
		{
			Note(seconds, BoarderObjective, cs::Cat(Compartments[BoarderObjective]->Name, " taken as ordered; the boarders press on"));
			BoarderOrder = BoardingOrder::Auto;
			BoarderObjective = -1;
		}

		auto bridge = Of(CompartmentKind::Bridge);
		auto engineering = Of(CompartmentKind::Engineering);
		if (bridge->Taken && !bridge->Contested() && engineering->Taken && !engineering->Contested())
		{
			Captured = true;
			Note(seconds, bridge->Index, cs::Cat("The ", BoarderNames(), " hold the bridge and engineering: the ship is taken (", cs::Fmt(Crew(), "0"), " crew surrender)"));
			return true;
		}
		if (Boarders() <= 0.5)
		{
			Note(seconds, -1, cs::Cat("The ", BoarderNames(), " boarders are wiped out; the crew hold the ship"));
			for (const auto& c : Compartments)
			{
				c->Boarders = 0;
				if (c->Taken && c->Crew + c->Marines <= 0.01) c->Taken = false;
			}
			EndBoarding();
			SealBreaches();
		}
		return false;
	}

	void Interior::Reinforce(double dt)
	{
		cs::Ref<Compartment> target = nullptr;
		// Ordered to muster somewhere: to hold it before the boarders reach it, or to retake it.
		if (DefenderOrder == BoardingOrder::Push) target = Compartments[DefenderObjective];
		else
			for (const auto& c : Compartments)
				if (c->Boarders > 0.01 && (target == nullptr || c->Kind == CompartmentKind::Bridge || c->Kind == CompartmentKind::Engineering)) target = c;
		if (target == nullptr) return;
		for (const auto& c : Compartments)
		{
			if (c == target || c->Marines <= 0.01 || c->Boarders > 0.01) continue;
			// A tenth of a compartment's marines move up each second.
			double move = c->Marines * cs::Math::Min(1.0, 0.1 * dt);
			c->Marines -= move;
			target->Marines += move;
		}
	}

	void Interior::Advance(double dt)
	{
		// With nobody left fighting at the front, the rear guards have nothing to cover: they
		// gather and press on instead of holding what was taken for ever.
		bool front = false;
		for (const auto& c : Compartments)
			if (c->Boarders > 0.01 && !c->Taken) front = true;
		// Every party's move is worked out from where they all stood at the start of the step, then made: a party
		// arriving in a held compartment does not move on again in the same step.
		struct Move { cs::Ref<Compartment> from, to; double marines = 0; };
		cs::List<Move> moves;
		for (const auto& c : Compartments)
		{
			if (!c->Taken || c->Boarders <= (front ? 1 : 0.01)) continue;
			int next = NextToward(c->Index, BoarderOrder == BoardingOrder::Push ? BoarderObjective : -1);
			if (next < 0) continue;
			// Most of the party moves on; a rear guard holds what was taken.
			double move = c->Boarders * cs::Math::Min(1.0, 0.08 * dt);
			if (front && c->Boarders - move < 2) move = cs::Math::Max(0.0, c->Boarders - 2);
			moves.Add(Move{ c, Compartments[next], move });
		}
		for (const auto& m : moves)
		{
			m.from->Boarders -= m.marines;
			m.to->Boarders += m.marines;
		}
	}

	int Interior::NextToward(int from, int objective)
	{
		auto prev = cs::NewArray<int>(Compartments.Count());
		for (int k = 0; k < prev.Count(); k++) prev[k] = -2;
		cs::Queue<int> queue;
		queue.Enqueue(from);
		prev[from] = -1;
		while (queue.Count() > 0)
		{
			int here = queue.Dequeue();
			auto c = Compartments[here];
			if (here != from && (objective >= 0 ? here == objective : !c->Taken && (c->Kind == CompartmentKind::Bridge || c->Kind == CompartmentKind::Engineering)))
			{
				int step = here;
				while (prev[step] != from) step = prev[step];
				return step;
			}
			for (int n : c->Links)
			{
				if (prev[n] != -2) continue;
				prev[n] = here;
				queue.Enqueue(n);
			}
		}
		return -1;
	}

	void Interior::Note(double seconds, int compartment, const cs::String& text)
	{
		Log.Add(BoardingNote(seconds, compartment, text));
		if (Log.Count() > 60) Log.RemoveAt(0);
	}

	void Interior::Settle()
	{
		ClearOrders();
		for (const auto& c : Compartments)
		{
			c->Vented = false;
			c->SealIn = 0;
		}
	}

	void Interior::BoardersGone()
	{
		if (Captured || Boarder == nullptr) return;
		for (const auto& c : Compartments)
		{
			c->Boarders = 0;
			c->Taken = false;
		}
		EndBoarding();
		SealBreaches();
	}

	void Interior::SealBreaches()
	{
		for (const auto& c : Compartments)
			if (c->Vented && c->SealIn > 0) c->SealIn = 0;
	}

	void Interior::HandOver(double prizeCrew)
	{
		SpreadCrew(prizeCrew, false);
		for (const auto& c : Compartments)
		{
			c->Marines = c->Boarders;
			c->Boarders = 0;
			c->Taken = false;
		}
		EndBoarding();
		Captured = false;
		// The ship's own marines are gone; the boarders now aboard never drilled in its habitats.
		Training = 0;
	}

	void Interior::Abandon(double survivors, double lurkers, double lurkerQuality)
	{
		for (const auto& c : Compartments)
		{
			c->Crew = c->Marines = c->Boarders = c->Survivors = 0;
			c->Taken = false;
			c->Vented = false;
			c->SealIn = 0;
		}
		EndBoarding();
		Captured = false;
		DefenderQuality = lurkerQuality;
		Training = 0;
		if (survivors > 0)
		{
			auto at = Of(CompartmentKind::Engineering);
			if (!at) at = Compartments[Compartments.Count() - 1];
			at->Survivors = survivors;
		}
		if (lurkers > 0)
		{
			auto at = Of(CompartmentKind::Hold);
			if (!at) at = Of(CompartmentKind::Midships);
			if (!at) at = Of(CompartmentKind::Bridge);
			if (!at) at = Compartments[0];
			at->Marines = lurkers;
		}
	}

	double Interior::Recall()
	{
		double n = 0;
		for (const auto& c : Compartments)
		{
			n += c->Boarders;
			c->Boarders = 0;
			c->Taken = false;
		}
		EndBoarding();
		Captured = false;
		return n;
	}

	double Interior::TakeCrew(double n)
	{
		double have = Crew();
		if (have <= 0 || n <= 0) return 0;
		double share = cs::Math::Min(1.0, n / have);
		for (const auto& c : Compartments) c->Crew -= c->Crew * share;
		return have * share;
	}

	double Interior::TakeMarines(double n)
	{
		double have = Marines();
		if (have <= 0 || n <= 0) return 0;
		double share = cs::Math::Min(1.0, n / have);
		for (const auto& c : Compartments) c->Marines -= c->Marines * share;
		return have * share;
	}

	void Interior::Embark(double crew, double marines)
	{
		if (crew > 0) SpreadCrew(crew, true);
		Quarter(marines);
	}

	void Interior::Muster(double crew, double marines)
	{
		double had = Marines();
		if (marines > had) Training *= had / marines;
		for (const auto& c : Compartments) c->Marines = 0;
		Crew0(crew, marines);
		for (const auto& c : Compartments)
		{
			c->Boarders = 0;
			c->Taken = false;
			c->Vented = false;
		}
		EndBoarding();
		Captured = false;
	}
}
