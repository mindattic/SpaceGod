#pragma once
// Port of Unity.SpaceGod Core/SimClock.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;

	/// <summary>Simulation time as a Julian date, advanced by real time multiplied by a warp level.</summary>
	// C#: [SettingsSource] -- its settings (StartingWarp) register when StartingWarp() is first called; the C++
	// stand-in for SettingsRegistry.DiscoverDeclarations must call it (see the Settings port).
	class SPACEGODCORE_API SimClock final
	{
	public:
		struct WarpLevel
		{
			double SimSecondsPerSecond = 0;
			cs::String Label;

			WarpLevel() = default;
			WarpLevel(double rate, cs::String label)
			{
				SimSecondsPerSecond = rate;
				Label = label;
			}
		};

		static const cs::List<WarpLevel>& Levels();

		static constexpr int DefaultWarpIndex = 4;

		/// <summary>The warp a new game starts at (a save keeps its own), from the settings menu's Simulation group (U.SPC-T431).</summary>
		static const cs::Ref<ChoiceSetting>& StartingWarp();

		double Jd = 0;
		int WarpIndex = DefaultWarpIndex;

		/// <summary>Stopped outright (scripts set this directly; the player's pause eases into it, see <see cref="ToggleEasedPause"/>).</summary>
		bool Paused() const { return paused; }
		void SetPaused(bool value)
		{
			paused = value;
			pausing = resuming = false;
			Speed = value ? 0 : 1;
		}

		/// <summary>How much of the warp rate is running (1 normally, easing to 0 when pausing and back when resuming).</summary>
		double Speed = 1;

		bool Pausing() const { return pausing; }
		bool Resuming() const { return resuming; }

		/// <summary>Each easing step multiplies (pausing) or divides (resuming) the speed by this.</summary>
		static constexpr double EaseFactor = 0.97;

		/// <summary>Easing steps per real second, so the ease takes the same time at any frame rate (about 1.3 s each way).</summary>
		static constexpr double EaseStepsPerSecond = 120;

		/// <summary>Below this share of full speed a pause snaps to a dead stop; a resume starts from it.</summary>
		static constexpr double EaseFloor = 0.01;

	private:
		bool paused = false, pausing = false, resuming = false;

	public:
		/// <summary>
		/// The player's pause: time slows by 0.97 a step until it is under 1% of full speed, then stops;
		/// pressed again (or while still slowing), it picks up from 1% (or wherever it had got to) and
		/// speeds back up the same way.
		/// </summary>
		void ToggleEasedPause();

		/// <summary>Paused, or on the way to it: what a pause button should show.</summary>
		bool PauseHeld() const { return paused || pausing; }

		/// <summary>
		/// Slow motion below real time (US-T166): half, a quarter, a tenth and a thirtieth of real time (a near-freeze), for
		/// stunts played on the clock like an ejected pilot's torpedo disarm. Reached by stepping slower from real time
		/// (<see cref="Slower"/>), left by stepping faster; it only ever runs at the real-time warp level.
		/// </summary>
		static const cs::List<WarpLevel>& SlowLevels();

		/// <summary>How deep into <see cref="SlowLevels"/> the clock is, 0 for none (1 the first). Not saved: a load runs at its
		/// warp. A warp above real time set clears it; real time set (as taking a seat does) keeps it.</summary>
		int SlowIndex = 0;

		bool SlowMotion() const { return WarpIndex == 0 && SlowIndex > 0; }

		/// <summary>The rate running: the warp level, or below real time the slow-motion level (US-T166).</summary>
		WarpLevel Warp() const { return SlowMotion() ? SlowLevels()[cs::Math::Min(SlowIndex, SlowLevels().Count()) - 1] : Levels()[WarpIndex]; }
		cs::DateTime Utc() const;

		explicit SimClock(double startJd) { Jd = startJd; }

		static cs::Ref<SimClock> StartingNow();

		/// <summary>
		/// The slowest warp level that gets <paramref name="simSeconds"/> of simulated time over within
		/// <paramref name="realSeconds"/> of real time (the fastest level if none does), never slower than
		/// <paramref name="atLeast"/>: how a piloted hyperlane ride of hours plays out in under a minute (U.SPC-T41).
		/// </summary>
		static int WarpToCover(double simSeconds, double realSeconds, int atLeast = 0);

		void SetWarp(int index)
		{
			WarpIndex = cs::Math::Max(0, cs::Math::Min(Levels().Count() - 1, index));
			if (WarpIndex > 0) SlowIndex = 0; // US-T166
		}
		/// <summary>One step faster: out of slow motion first (US-T166), then up the warp levels.</summary>
		void Faster()
		{
			if (SlowMotion()) SlowIndex--;
			else SetWarp(WarpIndex + 1);
		}
		/// <summary>One step slower: down the warp levels, then below real time into slow motion (US-T166).</summary>
		void Slower()
		{
			if (WarpIndex > 0) SetWarp(WarpIndex - 1);
			else SlowIndex = cs::Math::Min(SlowIndex + 1, SlowLevels().Count());
		}
		void SetJd(double jd) { Jd = jd; }

		/// <summary>Advances by real seconds; returns the simulated days elapsed.</summary>
		double Advance(double realSeconds);
	private:
		SimClock() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(SimClock); // ObjectGraph registration (Persistence/Registrations)
	};
}
