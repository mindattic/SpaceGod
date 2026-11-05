#pragma once
// US-T93: the sensors and power the player plays (passive listening, an active ping, going dark and a timed power-up).
// C++-only (no Unity.SpaceGod counterpart).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class ShipCombat;
	class World;

	/// <summary>The sensor and power command at the helm (US-T93), as <see cref="EmbodimentInput.Sensor"/> carries it: a
	/// command, not a toggle, so one carried twice (co-op, a replay) lands the same.</summary>
	enum class SensorKey : int
	{
		None = 0,
		/// <summary>An active ping: a burst of range and detail for the helm's own picture, and a flash everyone hears.</summary>
		Ping = 1,
		/// <summary>Full power-down: the reactor to minimum, everything off, the hull cooling (from online or a restart).</summary>
		GoDark = 2,
		/// <summary>Start the reactor again from dark: a timed restart.</summary>
		PowerUp = 3,
	};

	/// <summary>Where the hull's reactor stands (<see cref="ShipCombat.PowerDown"/>).</summary>
	enum class PowerState : int
	{
		Online = 0,
		/// <summary>Reactor at minimum: no guns, no shields, no drive, no transponder, the hull cooling toward
		/// <see cref="SensorPosture.DarkFloor"/>; it drifts on its momentum.</summary>
		Dark = 1,
		/// <summary>Coming back up (<see cref="ShipCombat.RestartLeftS"/>): still nothing online, the heat climbing back.</summary>
		Restarting = 2,
	};

	/// <summary>
	/// The player's sensors and power (US-T93). Passive is the default: the helm's own picture holds what others give off,
	/// as far as its suite reaches their signature (<see cref="Sensors.UpdatePilot"/>). Three things change that:
	/// <list type="bullet">
	/// <item>An active ping (<see cref="SensorKey.Ping"/>): for <see cref="PingBurstS"/> the helm's suite reaches
	/// <see cref="PingRangeFactor"/> times as far and resolves <see cref="PingResolutionFactor"/> times as finely, swept at
	/// once; but for <see cref="PingLitS"/> the hull adds <see cref="PingSignature"/> to its signature (a coasting freighter
	/// is 1, a battle 25), so every faction's passive sensors in reach hear it at once (<see cref="Sensors.PilotPinged"/>):
	/// hostiles hold you, witnesses see you. One every <see cref="PingCooldownS"/>.</item>
	/// <item>Going dark (<see cref="SensorKey.GoDark"/>): the reactor drops to minimum. Weapons, shields and drive are
	/// offline (the reactor's split, US-T94, reads zero: guns stop, the field bleeds away), the transponder is off, and
	/// the signature is the hull's residual heat alone, cooling from where it was toward <see cref="DarkFloor"/> of a
	/// coasting hull's over <see cref="CoolTauS"/> (bigger hulls hold their heat longer). The helm's own sensors listen on
	/// at <see cref="DarkSensorShare"/> of their reach, passive only. The craft drifts on its momentum.</item>
	/// <item>Powering up (<see cref="SensorKey.PowerUp"/>): a timed restart, <see cref="RestartSeconds"/> (8 s for a fighter
	/// to about a minute for the biggest hulls), during which nothing is back yet and the heat and EM climb back.</item>
	/// </list>
	/// Line of sight counts both ways while the player is the observer or the observed (<see cref="Sensors.PilotOccluded"/>):
	/// a planet, moon or asteroid between the two hides one from the other, so a hull dark behind a rock is gone.
	/// The state lives on the hull (<see cref="ShipCombat.PowerDown"/> and its fellows, saved as optional fields) and runs on
	/// the simulation clock (<see cref="Advance"/>, from <see cref="War.Update"/>); only the player ever pings or goes
	/// dark, so every crew's hull and the Bench run exactly as before. Handed back to its crew, the hull is brought
	/// straight back to normal (<see cref="Reset"/>).
	/// </summary>
	class SPACEGODCORE_API SensorPosture final
	{
	public:
		/// <summary>What a ping adds to the hull's signature while it is lit, in signature units.</summary>
		static constexpr double PingSignature = 300;
		/// <summary>How long the hull stays lit after a ping, seconds; and how long before the next one.</summary>
		static constexpr double PingLitS = 15;
		static constexpr double PingCooldownS = 20;
		/// <summary>How long the ping's burst of range and resolution lasts for the helm's own picture, seconds.</summary>
		static constexpr double PingBurstS = 3;
		static constexpr double PingRangeFactor = 3;
		static constexpr double PingResolutionFactor = 2;

		/// <summary>Where a dark hull's heat settles, as a share of its coasting hull heat (silent running holds 0.08).</summary>
		static constexpr double DarkFloor = 0.01;
		/// <summary>How much of its reach the helm's suite keeps while dark or restarting (passive arrays only).</summary>
		static constexpr double DarkSensorShare = 0.5;

		/// <summary>The hull's reactor state.</summary>
		static PowerState StateOf(const cs::Ref<ShipCombat>& c);
		/// <summary>Dark or restarting: no guns, shields or drive.</summary>
		static bool Offline(const ShipCombat* c);
		static bool Offline(const cs::Ref<ShipCombat>& c) { return Offline(c.get()); }
		/// <summary>Within <see cref="PingLitS"/> of a ping.</summary>
		static bool Lit(const ShipCombat* c);
		/// <summary>Within <see cref="PingBurstS"/> of a ping: the helm's picture sees with the boosted suite.</summary>
		static bool Bursting(const ShipCombat* c);

		/// <summary>The e-folding time of a dark hull's cooling, seconds: 30 s for a freighter, more for bigger hulls.</summary>
		static double CoolTauS(const cs::Ref<Ship>& s);
		/// <summary>How long a restart from dark takes, seconds: longer for bigger hulls.</summary>
		static double RestartSeconds(const cs::Ref<Ship>& s);

		/// <summary>Why <paramref name="key"/> cannot be done on <paramref name="s"/> now; empty when it can.</summary>
		static cs::String Refusal(const cs::Ref<Ship>& s, int key);

		/// <summary>The command the power key asks for on <paramref name="s"/>: go dark when online or restarting,
		/// power up when dark.</summary>
		static int PowerToggle(const cs::Ref<Ship>& s);

		/// <summary>Lands <paramref name="key"/> (a <see cref="SensorKey"/>) on <paramref name="s"/>; true when it changed
		/// anything. A ping is heard at once by every faction in reach (<see cref="Sensors.PilotPinged"/>).</summary>
		static bool Command(const cs::Ref<World>& world, const cs::Ref<Ship>& s, int key);

		/// <summary>One simulation tick (<paramref name="seconds"/> off the sim clock) of the helm's hull: the ping's windows
		/// run down, a dark hull cools, a restart counts down and, run out, brings everything back online.</summary>
		static void Advance(const cs::Ref<World>& world, double seconds);
		static void Advance(const cs::Ref<Ship>& s, double seconds);

		/// <summary>Back to normal at once (the crew takes the hull back).</summary>
		static void Reset(const cs::Ref<ShipCombat>& c);
	};
}
