#pragma once
// Port of Unity.SpaceGod Core/SimClock.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;

	/// <summary>Simulation time as a Julian date, advanced by real time multiplied by a warp level.</summary>
	// C#: [SettingsSource] -- its settings (StartingWarp) register when StartingWarp() is first called; the C++
	// stand-in for SettingsRegistry.DiscoverDeclarations must call it (see the Settings port).
	class SimClock final
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

		WarpLevel Warp() const { return Levels()[WarpIndex]; }
		cs::DateTime Utc() const;

		explicit SimClock(double startJd) { Jd = startJd; }

		static cs::Ref<SimClock> StartingNow();

		/// <summary>
		/// The slowest warp level that gets <paramref name="simSeconds"/> of simulated time over within
		/// <paramref name="realSeconds"/> of real time (the fastest level if none does), never slower than
		/// <paramref name="atLeast"/>: how a piloted hyperlane ride of hours plays out in under a minute (U.SPC-T41).
		/// </summary>
		static int WarpToCover(double simSeconds, double realSeconds, int atLeast = 0);

		void SetWarp(int index) { WarpIndex = cs::Math::Max(0, cs::Math::Min(Levels().Count() - 1, index)); }
		void Faster() { SetWarp(WarpIndex + 1); }
		void Slower() { SetWarp(WarpIndex - 1); }
		void SetJd(double jd) { Jd = jd; }

		/// <summary>Advances by real seconds; returns the simulated days elapsed.</summary>
		double Advance(double realSeconds);
	};
}
