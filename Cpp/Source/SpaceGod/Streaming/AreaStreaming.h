#pragma once
// US-T133: seamless area streaming. No loading screens anywhere: an area (a star system and its sky, the kit the ships
// are built from) is streamed in progressively ahead of the player, under a per-frame time budget, so it is fully built
// before it is seen; and what is near the view is upgraded ring by ring as it closes in. Presentation only: nothing here
// touches the simulation (SpaceGodCore is read, never stepped or changed).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Galaxy;
	class StarSystem;
	struct StreamView;
}

namespace SpaceGod::Views
{
	class SkyView;
}

namespace SpaceGod
{
	/// <summary>What sort of area is streamed.</summary>
	enum class AreaKind : uint8_t
	{
		/// <summary>A star system: its sky, its bodies' maps, its stations' and Void Station's meshes, its ships' models. A
		/// star not yet built into a system (outside Sol the galaxy is procedural) still has its sky streamed, by name.</summary>
		System,
		/// <summary>The Star Sparrow kit's meshes and material, every hull's parts (loaded once, kept for good).</summary>
		Kit,
	};

	/// <summary>An area by kind and name (a system's or star's name; empty for the kit).</summary>
	struct AreaId
	{
		AreaKind Kind = AreaKind::System;
		cs::String Name;

		static AreaId System(const cs::String& name) { return AreaId{ AreaKind::System, name }; }
		static AreaId Kit() { return AreaId{ AreaKind::Kit, cs::String() }; }

		cs::String Key() const { return Kind == AreaKind::Kit ? cs::String("kit") : cs::Cat("system:", Name); }
	};

	/// <summary>How soon an area will be needed: the most urgent is worked on first, and given the one sky held ahead.</summary>
	enum class StreamPriority : int
	{
		/// <summary>Could be headed for (a star the nose is on, a course plotted but not flown).</summary>
		Background = 0,
		/// <summary>On the way (an autopilot flying a course there, the next gate of a course).</summary>
		Ahead = 1,
		/// <summary>About to be (a gate being closed on, a jump drive spooling for it).</summary>
		Imminent = 2,
		/// <summary>Next (a gate in activation range, a trip or a crossing under way to it).</summary>
		Urgent = 3,
	};

	/// <summary>
	/// The detail a body near the view is drawn at, by how big it is (or is about to be) on screen: a ring of apparent size
	/// rather than of distance, so it is the same in realistic and game scale (the scale model sizes and places the body,
	/// the ring only reads the pixels). Far off it is its <see cref="Impostor"/> (its own colour on a plain sphere, as any
	/// unmapped body is); nearer its colour map is drawn; close, its relief map too.
	/// </summary>
	enum class StreamRing : int
	{
		Impostor = 0,
		Low = 1,
		Full = 2,
	};

	/// <summary>
	/// The area streaming manager (US-T133). Other systems say where the player may be headed with <see cref="Prefetch"/>,
	/// and ask <see cref="IsReady"/>; the app predicts the rest each frame (a trip under way, an FTL crossing or spool-up, an
	/// autopilot's course, gates in range or being closed on at the view's present speed) and calls <see cref="Tick"/>,
	/// which works through what each area needs, most urgent first, within <see cref="BudgetMs"/> a frame:
	///   - a system: its sky loaded or baked ahead (<see cref="Views::SkyView::Prefetch"/>), its bodies' colour maps, its
	///     ships' models (async, via AssetStreamer), its stations' and Void Station's meshes built into their caches;
	///   - the kit: every Star Sparrow mesh async-loaded in the background once, so a hull is never built on a disk read.
	/// An area no longer asked for is let go of once it has gone unasked <see cref="ExpireAfterS"/> seconds (a gap, so
	/// a gate flown past and back is not loaded twice), and its maps released with the rest of what nothing draws.
	///
	/// In the active system each body is given a <see cref="StreamRing"/> by its apparent size within the view's lead
	/// (Core::Streaming's Urgency: where the view will be at its present speed, and the way it looks), with the same
	/// load/unload gap the ships' models use, so it does not flicker at the edge. A relief map is only loaded once its
	/// body is in the Full ring (AssetStreamer::Admit holds it back until then).
	/// </summary>
	class AreaStreaming
	{
	public:
		// ------------------------------------------------------------ the API other systems call

		/// <summary>Says <paramref name="area"/> may be needed soon: it starts streaming in (if it is not already), and is
		/// kept while it goes on being asked for. Cheap; call it every frame the area is still headed for.</summary>
		static void Prefetch(const AreaId& area, StreamPriority priority);

		/// <summary>Prefetches <paramref name="system"/> (nothing for null).</summary>
		static void Prefetch(const cs::Ref<Core::StarSystem>& system, StreamPriority priority);

		/// <summary>Prefetches the place a jump drive drops out at: <paramref name="system"/> if it is built, else the star
		/// <paramref name="star"/> by name (its sky).</summary>
		static void Prefetch(const cs::Ref<Core::StarSystem>& system, const cs::String& star, StreamPriority priority);

		/// <summary>Whether everything <paramref name="area"/> is drawn with is in memory and built. The active system is
		/// ready (it is drawn); an area never asked for is not.</summary>
		static bool IsReady(const AreaId& area);
		static bool IsReady(const cs::Ref<Core::StarSystem>& system);

		// ------------------------------------------------------------ rings

		/// <summary>On screen at this size (px) a body's colour map is wanted, and at this its relief map.</summary>
		static constexpr double LowRingPx = 4, FullRingPx = 48;

		/// <summary>The ring something <paramref name="size"/> scene units across at <paramref name="at"/> is in for
		/// <paramref name="view"/>, given the ring it was in (it leaves a ring only well below where it entered it).</summary>
		static StreamRing RingFor(const Core::StreamView& view, Core::DVec3 at, double size, StreamRing current);

		/// <summary>The ring body <paramref name="body"/> of the active system is in (Impostor if unknown).</summary>
		static StreamRing BodyRing(int body);

		// ------------------------------------------------------------ driven by the app (UnitySpaceApp.Streaming.cpp)

		/// <summary>The most a frame spends working through areas (ms), at least one step a frame for an Urgent one.</summary>
		static constexpr double BudgetMs = 1.0;

		/// <summary>Seconds an area goes unasked before it is let go of.</summary>
		static constexpr float ExpireAfterS = 20.0f;

		/// <summary>The sky the systems are drawn under and the galaxy names are found in (idempotent; null detaches).</summary>
		static void Attach(const cs::Ref<Views::SkyView>& sky, const cs::Ref<Core::Galaxy>& galaxy);

		/// <summary>The view has entered <paramref name="system"/> (just built): its maps are noted for the rings, and how
		/// well it was streamed ahead is measured over the next frames.</summary>
		static void EnterSystem(const cs::Ref<Core::StarSystem>& system);

		/// <summary>Rings for the active system's bodies from the view (the body the camera is focused on is held Full).</summary>
		static void UpdateRings(const Core::StreamView& view, const cs::List<Core::DVec3>& bodyScene, const cs::List<double>& bodyRadius, int focusBody);

		/// <summary>Maps the view needs now and does not have yet: a colour map of a body in the Low ring or nearer, a
		/// relief map of one in the Full ring. Far bodies drawn as impostors are not waited on.</summary>
		static int PendingInView();

		/// <summary>A frame: what was not asked for again ages (and goes, past <see cref="ExpireAfterS"/>), the sky slot goes
		/// to the most urgent system, and the areas' steps are worked through within the budget.</summary>
		static void Tick(float dt);

		/// <summary>Adds every asset path a live area holds to <paramref name="keep"/> (not to be released).</summary>
		static void Keep(cs::HashSet<cs::String>& keep);

		/// <summary>True once after an area has been let go of: what it held may now be released.</summary>
		static bool ConsumeReleased();

		/// <summary>Frame-time and readiness figures, for the log and the audits.</summary>
		struct Stats
		{
			int Areas = 0, StepsLeft = 0, StepsDone = 0;
			double SpentMs = 0, WorstSpentMs = 0;
			cs::String LastArrival;
			bool LastArrivalReady = false;
			double LastArrivalAheadS = 0, LastArrivalFrameMs = 0, WorstAfterArrivalMs = 0;
		};
		static const Stats& Statistics();

	private:
		struct State;
		static State& S();
		struct Area;
		static void Build(Area& a);
		static void Release(Area& a);
	};
}
