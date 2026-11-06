#include "Combat/Formations.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(FormationShape v)
	{
		switch (v)
		{
			case FormationShape::Wedge: return "Wedge";
			case FormationShape::Line: return "Line";
			case FormationShape::Echelon: return "Echelon";
		}
		return "";
	}

	FormationShape Formations::ShapeOf(FighterRole role)
	{
		switch (role)
		{
			case FighterRole::SpaceSuperiority: return FormationShape::Echelon;
			case FighterRole::Bomber:
			case FighterRole::Recon: return FormationShape::Line;
			default: return FormationShape::Wedge;
		}
	}

	FormationShape Formations::ShapeOf(const cs::Ref<BattleUnit>& lead)
	{
		return lead != nullptr && lead->Fighter != nullptr ? ShapeOf(lead->Fighter->Role) : FormationShape::Wedge;
	}

	DVec3 Formations::Slot(FormationShape shape, int slot, bool mirrored, double leadLengthKm)
	{
		if (slot <= 0) return DVec3::Zero;
		// Longer craft fly further apart, up to twice an interceptor's spacing.
		double scale = Min(2.0, Max(1.0, leadLengthKm / SpacingLengthKm));
		int rank = (slot + 1) / 2;
		double side = slot % 2 == 1 ? 1 : -1;
		switch (shape)
		{
			case FormationShape::Line:
				// Out to either side, a touch stepped back and down so no two craft share the lead's own line of fire.
				return DVec3(side * 0.2 * rank, -0.01 * rank, -0.03 * rank) * scale;
			case FormationShape::Echelon:
			{
				// One diagonal off the lead's wing, each craft a step further out and back.
				double out = mirrored ? -1 : 1;
				return DVec3(out * 0.15 * slot, 0.02 * slot, -0.12 * slot) * scale;
			}
			default:
				// The wedge: the vee wingmen always flew (FighterAI::SlotOffset), pairs alternating sides.
				return DVec3(side * 0.16 * rank, 0.03 * rank, -0.14 * rank) * scale;
		}
	}

	DVec3 Formations::SlotOf(const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		auto lead = f->Lead != nullptr ? f->Lead : u;
		return Slot(ShapeOf(lead), f->Slot, Mirrored(f->Squadron), lead->LengthKm);
	}

	bool Formations::Foe(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		return o != nullptr && o != u && o->Kind == UnitKind::Fighter && o->Present() && b->Hostile(u, o) && !o->Surrendering && !o->Captured
			&& o->Combat != nullptr && o->Combat->Armed() && !FlightDecks::Away(o);
	}

	bool Formations::Broken(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Fighter != nullptr && u->Fighter->Phase == FighterPhase::Dogfight;
	}

	cs::Ref<BattleUnit> Formations::BreakTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto f = u != nullptr ? u->Fighter : nullptr;
		if (b == nullptr || f == nullptr || f->Lead == nullptr || f->Order != WingOrder::FormUp) return nullptr;
		// Bombers and recon craft hold their line for the run; a shuttle never fights.
		if (f->Bomber || f->Role == FighterRole::Bomber || f->Role == FighterRole::Recon || f->Role == FighterRole::AssaultShuttle) return nullptr;
		auto lead = f->Lead;
		if (!lead->Present()) return nullptr;
		bool broken = Broken(u);
		double reach = broken ? ReformKm : EngageKm;
		// In the fight: near enough the lead (wider once broken), or close on this wingman itself.
		auto inFight = [&](const cs::Ref<BattleUnit>& o)
		{
			return (o->Pos - lead->Pos).Length() < reach || (o->Pos - u->Pos).Length() < JumpedKm;
		};
		// The squadron is engaged: its lead has gone for an enemy fighter close by, one has jumped this wingman, or (once
		// broken) the furball it broke into is still going on round the lead.
		auto lt = lead->Target;
		bool engaged = Foe(b, u, lt) && inFight(lt);
		for (int i = 0; !engaged && i < b->Units.Count(); i++)
		{
			const auto& o = b->Units[i];
			if (Foe(b, u, o) && ((o->Pos - u->Pos).Length() < JumpedKm || (broken && (o->Pos - lead->Pos).Length() < ReformKm))) engaged = true;
		}
		if (!engaged) return nullptr;
		// Stay on the one it is already fighting while that is still in the fight; otherwise the nearest one in it.
		if (broken && Foe(b, u, u->Target) && inFight(u->Target)) return u->Target;
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& o : b->Units)
		{
			if (!Foe(b, u, o) || !inFight(o)) continue;
			double d = (o->Pos - u->Pos).Length();
			if (d < bestD)
			{
				bestD = d;
				best = o;
			}
		}
		return best != nullptr ? best : lt;
	}

	void Formations::Break(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& /*foe*/)
	{
		auto f = u->Fighter;
		if (f->Phase == FighterPhase::Dogfight) return;
		// Out of the vee: a fresh approach, which turns straight into the dogfight on a fighter (FighterAI::Lead).
		f->Phase = FighterPhase::Approach;
		f->PhaseTime = 0;
		f->RunClosest = cs::Double::MaxValue;
	}

	void Formations::Rejoin(const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		if (f->Phase != FighterPhase::Dogfight) return;
		f->Phase = FighterPhase::Approach;
		f->PhaseTime = 0;
		f->RunClosest = cs::Double::MaxValue;
	}
}
