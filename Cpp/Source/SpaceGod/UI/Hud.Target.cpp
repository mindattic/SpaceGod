// Hud target card (US-T256): the hull the controls held have locked, as a docked panel in the right-hand column -- its
// name, class, flag and stance, range and closing speed, hull, the shield of each emitter's patch, its gun heat, its
// subsystems by kind with their health, its broadcast array (US-T96), and the weak point a fighter's guns go for. Declared
// in Hud.Panels.inl.
//
// Read only: nothing here enters the simulation. Screen space, a fixed width, the same at the gameplay and the realistic
// scale. BottomRight over the sensors (HudLayout), taken on every event so the Layout pass and the Repaint place it and
// the selection card above it alike, and sized to what the column has free so it never runs into the TopRight stack.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/BroadcastArrays.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Embodiment/FighterSeat.h"
#include "Ships.h"

#include <algorithm>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float CardW = 232, Pad = 10, RowH = 18;
		constexpr Color ShieldTint{0.45f, 0.7f, 1.0f, 1.0f};
		constexpr Color HeatTint{1.0f, 0.7f, 0.3f, 1.0f};

		/// One shield emitter's patch, named by where on the hull it sits.
		struct PatchRow
		{
			cs::String Name;
			double Share = 0;
			bool Lost = false;
		};

		/// The subsystems of one kind: how many stand of how many, their mean health, and whether the weak point is one.
		struct KindRow
		{
			SubsystemKind Kind{};
			int Alive = 0, Total = 0;
			double Health = 0;
			bool Weak = false;
		};

		/// The side of the hull a hardpoint sits on (hull frame: right, up, forward), by its largest offset.
		cs::String Side(const cs::Ref<SubsystemDef>& d)
		{
			double ax = cs::Math::Abs(d->X), ay = cs::Math::Abs(d->Y), az = cs::Math::Abs(d->Z);
			if (ax < 1e-6 && ay < 1e-6 && az < 1e-6) return "CORE";
			if (az >= ax && az >= ay) return d->Z > 0 ? "FORE" : "AFT";
			if (ax >= ay) return d->X > 0 ? "STBD" : "PORT";
			return d->Y > 0 ? "DORSAL" : "VENTRAL";
		}

		std::vector<PatchRow> Patches(const cs::Ref<ShipCombat>& c)
		{
			std::vector<PatchRow> rows;
			if (c == nullptr) return rows;
			for (const auto& s : c->Subsystems)
			{
				if (s->Kind() != SubsystemKind::ShieldGenerator) continue;
				PatchRow p;
				p.Name = Side(s->Def);
				p.Share = s->ShieldFraction();
				p.Lost = s->Destroyed();
				rows.push_back(p);
			}
			// Two on one side are told apart by number.
			for (size_t i = 0; i < rows.size(); i++)
			{
				int same = 0, nth = 0;
				for (size_t j = 0; j < rows.size(); j++)
					if (rows[j].Name == rows[i].Name)
					{
						same++;
						if (j < i) nth++;
					}
				if (same > 1) rows[i].Name = cs::Cat(rows[i].Name, " ", nth + 1);
			}
			return rows;
		}

		std::vector<KindRow> Kinds(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& weak)
		{
			std::vector<KindRow> rows;
			if (c == nullptr) return rows;
			for (const auto& s : c->Subsystems)
			{
				auto k = s->Kind();
				size_t i = 0;
				while (i < rows.size() && rows[i].Kind != k) i++;
				if (i == rows.size())
				{
					KindRow r;
					r.Kind = k;
					rows.push_back(r);
				}
				auto& r = rows[i];
				r.Total++;
				if (!s->Destroyed()) r.Alive++;
				r.Health += s->Health();
				if (s == weak) r.Weak = true;
			}
			for (auto& r : rows) r.Health /= std::max(1, r.Total);
			return rows;
		}

		cs::String ClassOf(const cs::Ref<BattleUnit>& u)
		{
			if (u->Ship != nullptr && u->Ship->Spec != nullptr) return u->Ship->Spec->DisplayName;
			if (u->Fighter != nullptr) return cs::Strings::ToLowerInvariant(cs::String(ToString(u->Fighter->Role)));
			if (u->Fort != nullptr) return "gate fort";
			if (u->Platform != nullptr) return "defence platform";
			return cs::Strings::ToLowerInvariant(cs::String(ToString(u->Kind)));
		}

		cs::String Pct(double share) { return cs::Cat(Mathf::RoundToInt(static_cast<float>(Mathf::Clamp01(static_cast<float>(share)) * 100)), "%"); }

		void Bar(Rect r, float fraction, Color color)
		{
			auto old = GUI::color;
			GUI::color = Color(0, 0, 0, 0.5f);
			GUI::DrawTexture(r, Icons::White());
			GUI::color = color;
			GUI::DrawTexture(Rect(r.x, r.y, r.width * Mathf::Clamp01(fraction), r.height), Icons::White());
			GUI::color = old;
		}

		Color HealthTint(double h)
		{
			if (h <= 0) return Color(0.55f, 0.55f, 0.55f, 0.9f);
			if (h < 0.35) return Color(1.0f, 0.42f, 0.3f, 1.0f);
			if (h < 0.7) return Color(1.0f, 0.78f, 0.35f, 1.0f);
			return Color(0.55f, 1.0f, 0.62f, 1.0f);
		}
	}

	void Hud::DrawTargetCard()
	{
		if (app->Travel->Active || app->InFtlCruise()) return;
		cs::Ref<Battle> battle;
		auto me = HeldUnit(battle);
		if (me == nullptr || battle == nullptr) return;
		auto t = me->PlayerLockTarget;
		if (t == nullptr || !t->Present() || t->Combat == nullptr || t->Combat->Spec == nullptr) return;
		auto c = t->Combat;
		bool fighterSeat = cs::As<FighterSeat>(app->HeldSeat()) != nullptr;
		bool arrayPicked = BroadcastArrays::Is(t, me->PlayerSubTarget);
		auto weak = me->PlayerSubTarget != nullptr && !me->PlayerSubTarget->Destroyed() && (c->Subsystems.Contains(me->PlayerSubTarget) || arrayPicked)
			? me->PlayerSubTarget : nullptr;

		auto patches = Patches(c);
		auto kinds = Kinds(c, weak);
		// Its broadcast array (US-T96): knocked out, it cannot report the attack. Whole until first picked.
		if (BroadcastArrays::Carries(t))
		{
			KindRow a;
			a.Kind = SubsystemKind::BroadcastArray;
			a.Total = 1;
			a.Alive = BroadcastArrays::Silenced(t) ? 0 : 1;
			a.Health = BroadcastArrays::Health(t);
			a.Weak = weak != nullptr && arrayPicked;
			kinds.push_back(a);
		}
		int patchRows = static_cast<int>((patches.size() + 1) / 2);
		bool weakLine = fighterSeat || weak != nullptr;

		// Heading, name, class line, range line, hull, heat, the shields' caption and rows, the subsystems' caption, the
		// weak point and its key; then as many subsystem kinds as the column has room for.
		float fixedH = 6 + RowH * 4 + RowH * 2 + RowH + patchRows * RowH + RowH + (weakLine ? RowH * 2 : 0) + 8;
		float room = layout.FreeRight() - HudLayout::Gap;
		int kindRows = static_cast<int>(kinds.size());
		if (fixedH + kindRows * RowH > room) kindRows = std::max(0, static_cast<int>((room - fixedH) / RowH));
		float h = fixedH + kindRows * RowH;
		auto r = layout.Take(HudRegion::BottomRight, CardW, h);
		RegisterUi(r);
		if (Event::current().type != EventType::Repaint) return;

		GUI::Box(r, GUIContent::none(), panel);
		float x = r.x + Pad, w = CardW - 2 * Pad, y = r.y + 6;
		bool made = me->LockProgress >= 1;
		Color lockTint = made ? LockColor : HostileColor;
		DrawLabel(made ? cs::String("TARGET  \xC2\xB7  LOCKED") : cs::Cat("TARGET  \xC2\xB7  LOCKING ", Pct(me->LockProgress)), Vector2(x, y), lockTint, true);
		y += RowH;

		bool hostile = battle->Hostile(me, t);
		Color stanceTint = hostile ? HostileColor : t->Side == me->Side ? ArrowFriendlyColor : ArrowNeutralColor;
		DrawLabel(FitText(t->Name(), label, w), Vector2(x, y), stanceTint, false);
		y += RowH;
		cs::String stance = hostile ? "hostile" : t->Side == me->Side ? "friendly" : "neutral";
		cs::String flag = t->Faction != nullptr ? t->Faction->Name : cs::String("no flag");
		DrawLabel(FitText(cs::Cat(ClassOf(t), "  \xC2\xB7  ", flag, "  \xC2\xB7  ", stance), minorLabel, w), Vector2(x, y), Dim, true);
		y += RowH;

		auto rel = t->Pos - me->Pos;
		double km = rel.Length();
		double closing = km > 1e-9 ? -DVec3::Dot(t->Vel - me->Vel, rel / km) : 0;
		DrawLabel(cs::Cat(RangeText(km), "   ", closing >= 0 ? "closing " : "opening ", cs::Fmt(cs::Math::Abs(closing), "0.00"), " km/s"), Vector2(x, y), Dim, true);
		y += RowH;

		// Hull and gun heat, each a caption and a bar.
		constexpr float capW = 74;
		double hull = c->HullFraction();
		DrawLabel(cs::Cat("HULL ", Pct(hull)), Vector2(x, y), HealthTint(hull), true);
		Bar(Rect(x + capW, y + 6, w - capW, 6), static_cast<float>(hull), HealthTint(hull));
		y += RowH;
		DrawLabel(t->Overheated ? cs::String("OVERHEATED") : cs::Cat("HEAT ", Pct(t->Heat)), Vector2(x, y), t->Overheated ? HostileColor : HeatTint, true);
		Bar(Rect(x + capW, y + 6, w - capW, 6), static_cast<float>(t->Heat), t->Overheated ? HostileColor : HeatTint);
		y += RowH;

		// The shield, patch by patch: each emitter covers the part of the hull nearest it (ShieldEmitters).
		double total = c->Spec->Shield > 0 ? c->Shield() / c->Spec->Shield : 0;
		DrawLabel(patches.empty() ? cs::String("SHIELDS  \xC2\xB7  none") : cs::Cat("SHIELDS ", Pct(total), "  \xC2\xB7  ", static_cast<int>(patches.size()), patches.size() == 1 ? " emitter" : " emitters"),
			Vector2(x, y), ShieldTint, true);
		y += RowH;
		float colW = w * 0.5f;
		for (size_t i = 0; i < patches.size(); i++)
		{
			const auto& p = patches[i];
			float px = x + (i % 2) * colW, py = y + (i / 2) * RowH;
			Color tint = p.Lost ? HostileColor : p.Share <= 0 ? Color(1.0f, 0.62f, 0.32f, 1.0f) : ShieldTint;
			DrawLabel(cs::Cat(p.Name, " ", p.Lost ? cs::String("LOST") : p.Share <= 0 ? cs::String("DOWN") : Pct(p.Share)), Vector2(px, py), tint, true);
			Bar(Rect(px + 2, py + 15, colW - 12, 2), static_cast<float>(p.Share), tint);
		}
		if (!patches.empty()) y += patchRows * RowH;

		// The subsystems by kind, the weak point's kind marked.
		DrawLabel(cs::Cat("SUBSYSTEMS", kindRows < static_cast<int>(kinds.size()) ? cs::Cat("  (", kindRows, " of ", static_cast<int>(kinds.size()), " kinds)") : cs::String()),
			Vector2(x, y), Accent, true);
		y += RowH;
		constexpr float barW = 60;
		for (int i = 0; i < kindRows; i++)
		{
			const auto& k = kinds[i];
			Color tint = k.Weak ? LockColor : k.Alive == 0 ? HealthTint(0) : Dim;
			cs::String name = cs::Cat(k.Weak ? "> " : "", SubsystemDef::Name(k.Kind), k.Total > 1 ? cs::Cat(" ", k.Alive, "/", k.Total) : cs::String());
			DrawLabel(FitText(name, minorLabel, w - barW - 40), Vector2(x, y), tint, true);
			DrawLabel(Pct(k.Health), Vector2(x + w - barW - 36, y), HealthTint(k.Health), true);
			Bar(Rect(x + w - barW, y + 6, barW, 6), static_cast<float>(k.Health), k.Weak ? LockColor : HealthTint(k.Health));
			y += RowH;
		}

		// The weak point the guns go for (a fighter's, on a destroyer and up), and the key that picks another.
		if (weakLine)
		{
			if (weak != nullptr)
				DrawLabel(FitText(cs::Cat("WEAK POINT  ", cs::Strings::ToUpperInvariant(WeakPoints::Label(weak)), "  ", Pct(weak->Health()), "  ",
					arrayPicked ? "silence it" : WeakPoints::ExposedFrom(t, weak, me->Pos) ? "OPEN" : "covered"), minorLabel, w), Vector2(x, y), LockColor, true);
			else if (BroadcastArrays::Silenced(t))
				DrawLabel("BROADCAST ARRAY  out: it cannot report", Vector2(x, y), LockColor, true);
			else
				DrawLabel(WeakPoints::IsCapital(t) ? "WEAK POINT  none left" : "WEAK POINT  none (a destroyer and up)", Vector2(x, y), Dim, true);
			y += RowH;
			// US-T96: any ship's broadcast array can be picked while it stands.
			bool pickable = weak != nullptr || WeakPoints::IsCapital(t) || (BroadcastArrays::Carries(t) && !BroadcastArrays::Silenced(t));
			if (fighterSeat) DrawLabel(FitText(pickable ? "6 next weak point  \xC2\xB7  T next lock" : "T next lock", minorLabel, w), Vector2(x, y), Dim, true);
		}
	}
}
