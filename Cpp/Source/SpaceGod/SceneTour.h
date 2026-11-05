#pragma once

// SceneTour: the C++ port of Unity.SpaceGod's Runtime/SceneTour.cs (US-T31).
//
// The C# is a Unity coroutine (IEnumerator with `yield return`). It is ported as C++20 coroutines, so each step reads
// line for line as the original: `IEnumerator` -> SceneTour::Routine, `yield return null` -> `co_yield nullptr;`,
// `yield return Child(...)` -> `co_yield Child(...);`, `yield break` -> `co_return;`. A Routine does nothing by itself:
// SceneTour::Runner is the resumable state machine a per-frame host drives, one MoveNext() a frame, with Unity's
// coroutine scheduling:
//  - the first MoveNext() runs the routine up to its first `yield` (StartCoroutine runs the first step at once, so the
//    host calls it when it starts the routine, then once each later frame, after Update, as Unity resumes coroutines);
//  - `co_yield nullptr` waits for the next MoveNext() (the next frame);
//  - `co_yield child` runs the child at once, in the same MoveNext(), and the parent carries on in the very
//    MoveNext() in which the child ends (Unity resumes a coroutine waiting on a nested one when it finishes, the same
//    frame), so a child that ends without yielding (`yield break`) costs no frame.
// Waits are measured as the C# measures them: Wait(seconds) ends on the first frame whose Time.unscaledTime has
// reached the time it began plus `seconds`; the tour's other loops poll their own conditions once a frame.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"

#include <coroutine>
#include <exception>
#include <utility>
#include <vector>

namespace SpaceGod::Core
{
	class Battle;
	class Ship;
	class Faction;
	enum class ShipClass : int;
}

namespace SpaceGod
{
	class UnitySpaceApp;
	struct Target;

	/// The scripted camera tour through the system: inner planets, a carrier group, a battleship,
	/// a freighter mid-burn, Saturn, Earth at both scales, Jupiter, then real surfaces, weather and
	/// the asteroid belt. Run by CaptureRunner, which stops at each shot to save a
	/// screenshot when <c>-capture</c> is also active.
	class SceneTour final
	{
	public:
		/// C#'s IEnumerator of a coroutine: a C++20 coroutine that yields `nullptr` (wait a frame) or another Routine
		/// (run it to its end first). Move-only; destroying it ends the coroutine where it stands (StopCoroutine).
		class Routine
		{
		public:
			struct promise_type
			{
				/// A nested routine yielded by this one, for the Runner to take and run.
				Routine* child = nullptr;
				Routine pendingChild() { Routine r = std::move(*child); child = nullptr; return r; }
				std::exception_ptr exception;

				Routine get_return_object() { return Routine(std::coroutine_handle<promise_type>::from_promise(*this)); }
				std::suspend_always initial_suspend() noexcept { return {}; }
				std::suspend_always final_suspend() noexcept { return {}; }
				std::suspend_always yield_value(std::nullptr_t) noexcept { return {}; }
				/// `yield return Child(...)`: the child lives in the awaiting expression until this one resumes, by which
				/// time the Runner has moved it out (pendingChild).
				std::suspend_always yield_value(Routine&& nested) noexcept
				{
					child = &nested;
					return {};
				}
				void return_void() noexcept {}
				void unhandled_exception() noexcept { exception = std::current_exception(); }
			};

			Routine() = default;
			Routine(Routine&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
			Routine& operator=(Routine&& other) noexcept
			{
				if (this != &other)
				{
					if (handle) handle.destroy();
					handle = std::exchange(other.handle, nullptr);
				}
				return *this;
			}
			Routine(const Routine&) = delete;
			Routine& operator=(const Routine&) = delete;
			~Routine()
			{
				if (handle) handle.destroy();
			}

			explicit operator bool() const { return static_cast<bool>(handle); }

			/// The coroutine itself, for the Runner (null for an empty Routine).
			std::coroutine_handle<promise_type> Handle() const { return handle; }

		private:
			explicit Routine(std::coroutine_handle<promise_type> h) : handle(h) {}
			std::coroutine_handle<promise_type> handle;
		};

		/// Drives a Routine (and whatever it nests) once a frame: Unity's coroutine scheduler for one coroutine.
		class Runner
		{
		public:
			Runner() = default;
			explicit Runner(Routine root);
			Runner(Runner&&) noexcept = default;
			Runner& operator=(Runner&&) noexcept = default;

			/// Runs the routine to its next `yield return null` (or its end): true while it has more to do, false once it
			/// has finished. An exception escaping it is logged (Debug.LogError, as Unity logs a coroutine's
			/// exception) and ends the whole routine.
			bool MoveNext();

			/// True while there is more to run.
			bool Running() const { return !stack.empty(); }

			/// StopCoroutine: ends the routine (and every nested one) where it stands.
			void Stop() { stack.clear(); }

		private:
			/// The routine on top runs; the ones below each wait for the one above to finish.
			std::vector<Routine> stack;
		};

		/// `Func<string, IEnumerator> stop`: what to do at each framed shot, by name.
		using StopFunc = cs::Func<Routine, cs::String>;

		/// Runs one pass of the tour, calling <paramref name="stop"/> by name at each framed shot.
		static Routine Run(UnitySpaceApp& app, StopFunc stop);

		static Routine Wait(float seconds);

		/// Spawns <paramref name="classes"/> for each side parked near <paramref name="body"/>, already hostile, and starts the
		/// fight at once (the combat showcase's; public for the showcase tour's stops, US-T147). Null if no battle started.
		static cs::Ref<Core::Battle> TriggerSkirmish(UnitySpaceApp& app, int body, const cs::Ref<Core::Faction>& aggressor,
			const cs::Ref<Core::Faction>& defender, const cs::List<Core::ShipClass>& classes);

	private:
		// ---------------------------------------------------------------- what is in orbit
		static Routine InfrastructureTour(UnitySpaceApp& app, StopFunc stop);
		// ---------------------------------------------------------------- Alpha Centauri
		static Routine CentauriTour(UnitySpaceApp& app, StopFunc stop);
		// ---------------------------------------------------------------- the war
		static Routine BattleTour(UnitySpaceApp& app, StopFunc stop);
		// ---------------------------------------------------------------- combat showcase (U.SPC-T10)
		static Routine CombatShowcaseTour(UnitySpaceApp& app, StopFunc stop);
		static Routine BroadsideShowcase(UnitySpaceApp& app, StopFunc stop);
		static Routine DogfightShowcase(UnitySpaceApp& app, StopFunc stop, cs::String bodyName, cs::Ref<Core::Faction> aggressor,
			cs::Ref<Core::Faction> defender, Core::ShipClass cls, cs::String stopId);
		// ---------------------------------------------------------------- surfaces, weather, asteroids
		static Routine SurfaceTour(UnitySpaceApp& app, StopFunc stop);

		static cs::Ref<Core::Ship> Subject(const cs::Ref<Core::Battle>& battle, int shot);
		static void View(UnitySpaceApp& app, int body, Core::DVec3 dir, double radii);
		static void ViewShip(UnitySpaceApp& app, const cs::Ref<Core::Ship>& ship, Core::DVec3 dir, double radii);
		static float LevelPitchDeg(UnitySpaceApp& app, Core::DVec3 dir);
		static Core::DVec3 Dir(UnitySpaceApp& app, int from, int to);
		static Core::DVec3 Blend(Core::DVec3 a, Core::DVec3 b, double t);
		static Core::DVec3 Side(Core::DVec3 d);
		static Core::DVec3 GrsDirection(UnitySpaceApp& app, int jupiter);
		static double NextGrsNoon(UnitySpaceApp& app, int jupiter);
		static void Focus(UnitySpaceApp& app, const Target& t, double radii);
	};
}
