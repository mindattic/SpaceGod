#pragma once
// Port of Unity.SpaceGod Core/Streaming.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	struct StreamView;
	struct StreamItem;

	/// <summary>
	/// Seamless streaming (U.SPC-T414): nothing waits behind a loading screen. What is drawn in full (a ship's model, a
	/// planet's texture, the systems next door) is loaded in the background, a little each frame within a time budget,
	/// in the order it will be needed: what is big on screen now first, then what the view is flying toward, judged
	/// by where the view will be a few seconds ahead along its own motion (the lead), with what lies ahead of the way it
	/// looks counted bigger than what lies behind. Something drops out again once it is well below the size it is
	/// wanted at (a gap between the two, so nothing flickers in and out at the edge).
	///
	/// The lead is the promise that plain flight never waits: anything the view could reach at its present speed
	/// within the lead is asked for before it is reached, and a frame's budget clears far more than a lead's worth of
	/// newcomers. The one place loading may still be behind is the far side of a jump gate, where everything is new at
	/// once; the gate transit covers it (<see cref="GateCover"/>), holding its tunnel open until the far side is in.
	/// </summary>
	/// <remarks>C#: [SettingsSource]. The three settings are built together on first use (C#'s type initialiser), in textual order.</remarks>
	class Streaming
	{
	public:
		/// <summary>The share of a frame's time loading may spend.</summary>
		static const cs::Ref<RangeSetting>& FrameBudgetMs();

		/// <summary>How far ahead (seconds of the view's own motion) things are asked for before they are needed.</summary>
		static const cs::Ref<RangeSetting>& LeadSeconds();

		/// <summary>The most a jump gate's tunnel is held open past its own length while the far side finishes loading.</summary>
		static const cs::Ref<RangeSetting>& GateCoverMaxSeconds();

		/// <summary>Asked for once it would be this share of the size it is drawn at (well before it shows).</summary>
		static constexpr double LoadShare = 0.35;

		/// <summary>Let go of once it has fallen below this share (well under <see cref="LoadShare"/>, so no flicker).</summary>
		static constexpr double UnloadShare = 0.12;

		/// <summary>Something dead ahead of the view counts this many times its size; straight behind, once.</summary>
		static constexpr double AheadBoost = 2.5;

		/// <summary>Needed at least this soon (the view would reach it within <see cref="WarmSeconds"/>), a gate's far
		/// system is warmed: its textures and models start loading in the background.</summary>
		static constexpr double WarmSeconds = 30;

		/// <summary>
		/// How much more an item counts for lying in the way the view looks: 1 beside or behind it, rising smoothly to
		/// <see cref="AheadBoost"/> dead ahead.
		/// </summary>
		static double HeadingWeight(DVec3 heading, DVec3 toItem);

		/// <summary>
		/// The nearest the view comes to <paramref name="item"/> within the lead: the closest point on its path from
		/// where it is to where it will be <paramref name="leadSeconds"/> on at <paramref name="velocity"/>.
		/// </summary>
		static double NearestWithinLead(DVec3 viewer, DVec3 velocity, double leadSeconds, DVec3 item);

		/// <summary>
		/// How badly <paramref name="size"/> scene units of something at <paramref name="item"/> is wanted, as a share of the
		/// size on screen it is drawn at: its largest size on screen within the lead, weighted by the way the view
		/// looks. 1 or more: it is (or is about to be) drawn in full.
		/// </summary>
		static double Urgency(const StreamView& view, DVec3 item, double size);

		/// <summary>Whether something this <paramref name="urgency"/> should be held in memory: asked for at
		/// <see cref="LoadShare"/>, let go of only below <see cref="UnloadShare"/>.</summary>
		static bool WantsLoaded(double urgency, bool loaded) { return urgency >= (loaded ? UnloadShare : LoadShare); }

		/// <summary>
		/// What to load and what to let go of this frame. <paramref name="load"/> gets the items wanted and not loaded, the
		/// most urgent first (everything always wanted, such as the ship at the helm, ahead of all); <paramref name="unload"/>
		/// gets the loaded items no longer wanted. Both are cleared first.
		/// </summary>
		static void Plan(const cs::List<StreamItem>& items, const StreamView& view, cs::List<int>& load, cs::List<int>& unload);

	private:
		// C#: [ThreadStatic] static List<(double urgency, int id)> scoredList; scored => scoredList ??= new ...
		struct Scored { double urgency = 0; int id = 0; };
		static cs::List<Scored>& scored();

	public:
		/// <summary>
		/// Works down <paramref name="ordered"/> calling <paramref name="work"/> on each until <paramref name="budgetMs"/>
		/// (by <paramref name="nowMs"/>) is spent: at least one each frame, so a heavy item still gets through. Work that
		/// reports false (not ready yet, e.g. its asset still on its way) costs a look but is skipped. Returns how many
		/// were done.
		/// </summary>
		static int Spend(const cs::List<int>& ordered, const cs::Func<bool, int>& work, double budgetMs, const cs::Func<double>& nowMs);

		/// <summary>
		/// Whether the system beyond a gate at <paramref name="gate"/> should be warmed (its assets started loading): the
		/// view is within <paramref name="nearDistance"/> of it, or closing on it fast enough to be there within
		/// <see cref="WarmSeconds"/>.
		/// </summary>
		static bool ShouldWarm(DVec3 viewer, DVec3 velocity, DVec3 gate, double nearDistance);

	private:
		struct State;
		static State& S();
	};

	/// <summary>Where the view is and how it moves, for <see cref="Streaming.Plan"/>: scene units and real seconds.</summary>
	struct StreamView
	{
		DVec3 Position, Velocity, Heading;

		/// <summary>The camera's pixels per scene unit at unit distance (a size over a distance, times this, is pixels).</summary>
		double PixelsPerUnit = 0;

		/// <summary>The size on screen, pixels, something is drawn in full at (below it, an icon).</summary>
		double ModelPx = 0;

		double LeadSeconds = 0;

		StreamView() = default;
		StreamView(DVec3 position, DVec3 velocity, DVec3 heading, double pixelsPerUnit, double modelPx, double leadSeconds)
		{
			Position = position;
			Velocity = velocity;
			Heading = heading;
			PixelsPerUnit = pixelsPerUnit;
			ModelPx = modelPx;
			LeadSeconds = leadSeconds;
		}
	};

	/// <summary>One thing that may be streamed: where it is, how big (scene units), whether it is in memory, and whether
	/// it must always be (the ship at the helm, the one selected).</summary>
	struct StreamItem
	{
		int Id = 0;
		DVec3 Position;
		double Size = 0;
		bool Loaded = false, Always = false;

		StreamItem() = default;
		StreamItem(int id, DVec3 position, double size, bool loaded, bool always = false)
		{
			Id = id;
			Position = position;
			Size = size;
			Loaded = loaded;
			Always = always;
		}
	};

	/// <summary>
	/// The cover a jump gate transit gives the loading of what lies on the far side (U.SPC-T414). The effect (the tunnel
	/// between the stars, or the helm's flare-and-fold through a ring) runs its own length, but at its height
	/// (<see cref="HoldAt"/> of the way through) it is held for as long as the far side is still loading, up to a cap,
	/// so the destination is never seen half built; the rest of it plays out once loading is done.
	/// </summary>
	class GateCover final
	{
	public:
		/// <summary>The effect's own length, seconds, with no hold.</summary>
		double BaseSeconds = 0;

		/// <summary>Share of the way through at which the effect may hold.</summary>
		double HoldAt = 0;

		/// <summary>The longest it may hold, seconds.</summary>
		double MaxHoldSeconds = 0;

		/// <summary>Seconds since it began, and how many of them it was held.</summary>
		double Elapsed = 0;

		double Held = 0;

		bool Active = false;

		void Begin(double baseSeconds, double holdAt, double maxHoldSeconds);

		/// <summary>Moves the effect on by <paramref name="dt"/> seconds; at its hold point it stays put while
		/// <paramref name="farSideLoading"/>, until the cap is reached.</summary>
		void Advance(double dt, bool farSideLoading);

		/// <summary>0..1 through the effect, standing still while it is held.</summary>
		double T() const { return Clamp01((Elapsed - Held) / BaseSeconds); }

		/// <summary>Whether it is holding for the far side right now (at its hold point with time held).</summary>
		bool Holding() const { return Active && Held > 0 && cs::Math::Abs(Elapsed - Held - HoldAt * BaseSeconds) < 1e-9; }

		/// <summary>Played through to its end.</summary>
		bool Done() const { return !Active && Elapsed > 0; }

		/// <summary>The helm's own flight through a ring (no camera trip): seconds, and where it holds.</summary>
		static constexpr double HelmSeconds = 1.6, HelmHoldAt = 0.4;

		/// <summary>The glow of the far side's arrival through a ring at the helm, 0..1: up at once, held, then gone.</summary>
		static double HelmGlow(double t);

		/// <summary>The fold of space shut and open again at the helm, 0..1: the view pinches in toward the hold and opens
		/// back out as the far side appears.</summary>
		static double HelmFold(double t);

	private:
		static double Clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
	};
}
