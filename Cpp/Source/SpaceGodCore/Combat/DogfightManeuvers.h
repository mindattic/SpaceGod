#pragma once
// US-T188: fighter-on-fighter dogfighting flown on Newtonian "real physics": the nose and the velocity are two things.

#include "Cs/Cs.h"
#include "DVec3.h"
#include <cstdint>

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;

	/// <summary>The named manoeuvre a dogfighting craft is flying (US-T188, see <see cref="DogfightManeuvers"/>).</summary>
	enum class DogfightTactic : int
	{
		/// <summary>Lead pursuit: the nose on the lead pip, guns working, closing on a point just behind the target.</summary>
		LeadPursuit,

		/// <summary>Lag pursuit: closing too fast to stay behind, it aims its track behind the target's tail and bleeds
		/// speed so it does not overshoot, the nose still swinging onto the lead pip.</summary>
		LagPursuit,

		/// <summary>Flip-and-burn: a pursuer close on its tail, it flips end over end to face it, guns on it, and burns its
		/// main drive against its own way, so the chaser overshoots straight into its fire.</summary>
		FlipAndBurn,

		/// <summary>Drift turn: momentum carrying it on and sideways, the nose swung off its track onto the target while the
		/// manoeuvre thrusters push the slide round.</summary>
		DriftTurn,

		/// <summary>Boom-and-zoom: a high-speed slashing pass under boost, guns on the target through the merge, then out the
		/// other side at speed (the zoom) before flipping back round for the next.</summary>
		BoomAndZoom,

		/// <summary>Jinking: a random walk of hard sideways kicks off its own line under fire, so the gunner behind it cannot
		/// settle its lead.</summary>
		Jink,

		/// <summary>Scissors: an attacker alongside or close behind, it reverses its turn into it again and again, slowing,
		/// to force it out in front.</summary>
		Scissors,
	};
	SPACEGODCORE_API const char* ToString(DogfightTactic v);

	/// <summary>
	/// The dogfight a small craft flies against an enemy fighter (US-T188). A craft's flight here is Newtonian: its
	/// facing and its velocity are decoupled, momentum carries it, and thrust changes it. The main drive pushes along the
	/// nose at the hull's acceleration, the retros against it at its braking, the manoeuvre thrusters across it at its
	/// strafe -- each the hull's own, from its catalog design (US-T193: <see cref="CatalogDesign"/>, thrust over mass),
	/// with its turn and roll rates its catalog row's (<see cref="ShipCatalog"/>, a crew's share of the helm's,
	/// <see cref="Battle.DogfightTurnShare"/>). So an interceptor carves where a heavy fighter slides.
	///
	/// The pilot picks a named manoeuvre by the situation (<see cref="Choose"/>): something on its tail sends it into a
	/// flip-and-burn (close and fast), the scissors (alongside) or a jink (further back); a target far off it slashes
	/// through on a boom-and-zoom; close, it flies lead pursuit, or lag pursuit when it would overshoot, or a drift turn
	/// when the target is well off its nose. Each manoeuvre holds for its own seconds and stages (<see cref="State"/>)
	/// and is cut short when the fight turns.
	///
	/// Deterministic: every draw is a hash of the pilot's seed and its own count of draws, never the battle's shared
	/// random stream nor wall time; and in battle-local km, the same at both scales. The one hook into
	/// <see cref="FighterAI"/> is its dogfight phase (<see cref="Fly"/>), which a wingman breaking out of its squadron's
	/// formation (US-T198, <see cref="Formations"/>) reaches like any lead.
	/// </summary>
	class SPACEGODCORE_API DogfightManeuvers final
	{
	public:
		/// <summary>A craft's dogfight, carried from step to step on its <see cref="FighterState"/> (a fleet light
		/// fighter's on its <see cref="BattleUnit.Tactics"/>, US-T204). Not saved: after a load the pilot simply picks its
		/// manoeuvre afresh.</summary>
		struct State
		{
			DogfightTactic Tactic = DogfightTactic::LeadPursuit;

			/// <summary>Seconds left in the manoeuvre, spent in it, and spent in its current <see cref="Stage"/>.</summary>
			double Left = 0, Time = 0, StageTime = 0;

			/// <summary>The manoeuvre's own stage (a boom's dive then zoom; a flip's swing then burn), and the way it
			/// breaks or slides (+1 right, -1 left).</summary>
			int Stage = 0, Side = 1;

			/// <summary>Seconds to the next jink kick, and the kick itself (unit, across the track).</summary>
			double KickIn = 0;
			DVec3 Kick;

			/// <summary>The enemy it is fighting (its unit id) and the one on its tail it is shaking (0: none).</summary>
			int FoeId = 0, ThreatId = 0;

			/// <summary>How many draws the pilot has made (its own deterministic stream, see <see cref="Draw"/>).</summary>
			uint32_t Draws = 0;
		};

		/// <summary>The hull's Newtonian handling in the dogfight: turn and roll, deg/s; top speed, km/s; and the drive's
		/// three pushes (main along the nose, retro against it, manoeuvre across it), km/s², all at its engines' power.</summary>
		struct Handling
		{
			double TurnDegS = 0, RollDegS = 0, TopSpeed = 0, Accel = 0, Brake = 0, Strafe = 0;

			/// <summary><paramref name="u"/>'s handling: its class's catalog row and its design's drive.</summary>
			static Handling Of(const cs::Ref<BattleUnit>& u);
		};

		/// <summary>One step of the crew's stick: where it wants the nose, the velocity it is burning for, the boost, and
		/// a roll input (-1..1).</summary>
		struct Command
		{
			DVec3 Face, WantVel;
			bool Boost = false;
			double Roll = 0;
		};

		/// <summary>How near behind a hostile with its nose on a craft must be to count as on its tail, km; and how close
		/// it must be for a flip-and-burn.</summary>
		static constexpr double TailKm = 2.5; static constexpr double FlipKm = 1.1;

		/// <summary>How far off the target sends a pilot on a boom-and-zoom pass, km, and how far it zooms out past it.</summary>
		static constexpr double BoomKm = 3.0; static constexpr double ZoomKm = 3.5;

		/// <summary>The closing speed inside gun range over which a pursuer flies lag pursuit, km/s.</summary>
		static constexpr double OvershootKmS = 0.22;

		/// <summary>How hard the burn closes on the velocity it wants: seconds to make up the gap at the drive's limit.</summary>
		static constexpr double RespondS = 0.35;

		/// <summary>How much harder than its cruise the boost burns (as <see cref="FighterAI.BurnBoost"/>).</summary>
		static constexpr double BoostFactor = 1.3;

		/// <summary>
		/// One step of <paramref name="u"/>'s dogfight on enemy craft <paramref name="t"/>: picks (or keeps) its manoeuvre
		/// and flies it, Newtonian, nose, drive and roll. Returns false (and does nothing) without a target to fight.
		/// </summary>
		static bool Fly(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double dt);

		/// <summary>The manoeuvre <paramref name="u"/> is flying, or null when it is not dogfighting.</summary>
		static cs::Nullable<DogfightTactic> TacticOf(const cs::Ref<BattleUnit>& u);

		/// <summary>The hostile craft close behind <paramref name="u"/> with its nose on it (nearest first), or null.</summary>
		static cs::Ref<BattleUnit> OnTail(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// The manoeuvre a pilot picks against <paramref name="t"/> with <paramref name="threat"/> (or none) on its tail: a
		/// pure function of the geometry, the pilot's skill and its own draw <paramref name="roll"/> (0..1).
		/// </summary>
		static DogfightTactic Choose(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, const Handling& h, double roll);

		/// <summary>
		/// One Newtonian step: the nose swings toward <paramref name="cmd"/>'s facing at the hull's turn rate, and the
		/// drive burns for its wanted velocity, the push along the nose held to the main drive (forward) or the retros
		/// (back) and the push across it to the manoeuvre thrusters; momentum does the rest.
		/// </summary>
		static void Thrust(const cs::Ref<BattleUnit>& u, const Command& cmd, const Handling& h, double dt);

		/// <summary>The pilot's next draw, 0..1: a hash of its seed and its count of draws.</summary>
		static double Draw(double seed, State& s);

	private:
		static Command Plan(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, const Handling& h, State& s, double dt);
		static void Start(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, const cs::Ref<BattleUnit>& threat, DogfightTactic tactic, const Handling& h, State& s, double seed);
	};
}
