// Port of Unity.SpaceGod Core/Streaming.cs (see docs/MIGRATION.md).

#include "Streaming.h"
#include "Settings/GameSetting.h"
#include "Systems/GateJump.h"

namespace SpaceGod::Core
{
	struct Streaming::State
	{
		cs::Ref<RangeSetting> FrameBudgetMs, LeadSeconds, GateCoverMaxSeconds;
	};

	Streaming::State& Streaming::S()
	{
		// C#'s static field initialisers, in textual order.
		static State* s = []
		{
			auto* st = new State();
			st->FrameBudgetMs = cs::New<RangeSetting>("stream.frameBudgetMs", SettingGroups::Streaming,
				"Loading budget per frame", 3, 0.5, 12, 0.5,
				"How long each frame may spend building models and textures that have come into view. Lower keeps the frame rate steadier; higher fills a crowded view in sooner.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " ms"); }), "stream load budget frame hitch stutter async");
			st->LeadSeconds = cs::New<RangeSetting>("stream.leadSeconds", SettingGroups::Streaming,
				"Look-ahead", 5, 1, 20, 0.5,
				"Models and textures are loaded this many seconds before the view, at its present speed, would reach the size they are drawn at. Longer is safer at speed and uses more memory.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "stream prefetch lead ahead preload");
			st->GateCoverMaxSeconds = cs::New<RangeSetting>("stream.gateCoverMaxS", SettingGroups::Streaming,
				"Gate tunnel hold", 8, 0, 20, 0.5,
				"The longest a jump gate's tunnel is held open, past its usual length, while what lies on the far side finishes loading.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " s"); }), "gate jump tunnel transit loading cover");
			return st;
		}();
		return *s;
	}

	const cs::Ref<RangeSetting>& Streaming::FrameBudgetMs() { return S().FrameBudgetMs; }
	const cs::Ref<RangeSetting>& Streaming::LeadSeconds() { return S().LeadSeconds; }
	const cs::Ref<RangeSetting>& Streaming::GateCoverMaxSeconds() { return S().GateCoverMaxSeconds; }

	double Streaming::HeadingWeight(DVec3 heading, DVec3 toItem)
	{
		double h = heading.Length(), d = toItem.Length();
		if (h < 1e-12 || d < 1e-12) return AheadBoost;
		double cos = DVec3::Dot(heading, toItem) / (h * d);
		if (cos <= 0) return 1;
		return 1 + (AheadBoost - 1) * cos * cos;
	}

	double Streaming::NearestWithinLead(DVec3 viewer, DVec3 velocity, double leadSeconds, DVec3 item)
	{
		auto path = velocity * cs::Math::Max(leadSeconds, 0.0);
		double len2 = path.LengthSquared();
		if (len2 < 1e-24) return (item - viewer).Length();
		double s = DVec3::Dot(item - viewer, path) / len2;
		s = s < 0 ? 0 : s > 1 ? 1 : s;
		return (item - (viewer + path * s)).Length();
	}

	double Streaming::Urgency(const StreamView& view, DVec3 item, double size)
	{
		if (view.ModelPx <= 0) return cs::Double::PositiveInfinity;
		double nearest = NearestWithinLead(view.Position, view.Velocity, view.LeadSeconds, item);
		double px = size / cs::Math::Max(nearest, 1e-9) * view.PixelsPerUnit;
		return px / view.ModelPx * HeadingWeight(view.Heading, item - view.Position);
	}

	cs::List<Streaming::Scored>& Streaming::scored()
	{
		thread_local cs::List<Scored> scoredList;
		return scoredList;
	}

	void Streaming::Plan(const cs::List<StreamItem>& items, const StreamView& view, cs::List<int>& load, cs::List<int>& unload)
	{
		load.Clear();
		unload.Clear();
		scored().Clear();
		for (int i = 0; i < items.Count(); i++)
		{
			const auto& it = items[i];
			double u = it.Always ? cs::Double::PositiveInfinity : Urgency(view, it.Position, it.Size);
			if (it.Loaded)
			{
				if (!WantsLoaded(u, true)) unload.Add(it.Id);
			}
			else if (WantsLoaded(u, false)) scored().Add(Scored{ u, it.Id });
		}
		// C#: b.urgency.CompareTo(a.urgency) -- double.CompareTo, NaN ordered first.
		scored().Sort([](const Scored& a, const Scored& b)
		{
			double x = b.urgency, y = a.urgency;
			if (x < y) return -1;
			if (x > y) return 1;
			if (x == y) return 0;
			return cs::Double::IsNaN(x) ? (cs::Double::IsNaN(y) ? 0 : -1) : 1;
		});
		for (const auto& s : scored()) load.Add(s.id);
		scored().Clear();
	}

	int Streaming::Spend(const cs::List<int>& ordered, const cs::Func<bool, int>& work, double budgetMs, const cs::Func<double>& nowMs)
	{
		if (!work || ordered.Count() == 0) return 0;
		double start = nowMs();
		int done = 0;
		for (int i = 0; i < ordered.Count(); i++)
		{
			if (done > 0 && nowMs() - start >= budgetMs) break;
			if (work(ordered[i])) done++;
		}
		return done;
	}

	bool Streaming::ShouldWarm(DVec3 viewer, DVec3 velocity, DVec3 gate, double nearDistance)
	{
		auto to = gate - viewer;
		double d = to.Length();
		if (d <= nearDistance) return true;
		double closing = DVec3::Dot(velocity, to) / cs::Math::Max(d, 1e-12);
		return closing > 0 && (d - nearDistance) / closing <= WarmSeconds;
	}

	// ---------------------------------------------------------------- GateCover

	void GateCover::Begin(double baseSeconds, double holdAt, double maxHoldSeconds)
	{
		BaseSeconds = cs::Math::Max(baseSeconds, 1e-6);
		HoldAt = holdAt < 0 ? 0 : holdAt > 1 ? 1 : holdAt;
		MaxHoldSeconds = cs::Math::Max(maxHoldSeconds, 0.0);
		Elapsed = Held = 0;
		Active = true;
	}

	void GateCover::Advance(double dt, bool farSideLoading)
	{
		if (!Active || dt <= 0) return;
		double holdPoint = HoldAt * BaseSeconds;
		double played = Elapsed - Held;
		// Held only at the hold point itself: loading that starts later (past the height of the effect) does not
		// freeze it part way through its ending.
		if (farSideLoading && Held < MaxHoldSeconds && played <= holdPoint + 1e-9 && played + dt >= holdPoint)
		{
			// Play up to the hold point, then hold the rest of this step (as far as the cap allows).
			double toHold = cs::Math::Max(holdPoint - played, 0.0);
			double hold = cs::Math::Min(dt - toHold, MaxHoldSeconds - Held);
			Held += hold;
			Elapsed += toHold + hold;
			dt -= toHold + hold;
			if (dt <= 1e-12) return;
		}
		Elapsed += dt;
		if (Elapsed - Held >= BaseSeconds) Active = false;
	}

	double GateCover::HelmGlow(double t)
	{
		t = Clamp01(t);
		return GateJump::Smoother(t / 0.12) * (1 - GateJump::Smoother((t - 0.6) / 0.4));
	}

	double GateCover::HelmFold(double t)
	{
		t = Clamp01(t);
		return cs::Math::Sin(cs::Math::PI * t);
	}
}
