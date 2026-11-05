#pragma once
// US-T97: self-destruct on whatever the player embodies. C++-only (no Unity.SpaceGod counterpart).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class CombatSpec;
	class Ship;
	class ShipCombat;
	class World;

	/// <summary>The self-destruct command at the controls (US-T97), as <see cref="EmbodimentInput.SelfDestruct"/> carries it.</summary>
	enum class SelfDestructKey : int
	{
		None = 0,
		/// <summary>Start the countdown (the key held for <see cref="SelfDestruct.ArmHoldS"/>); nothing if one is running.</summary>
		Arm = 1,
		/// <summary>Stop a countdown under way; nothing if none is.</summary>
		Abort = 2,
	};

	/// <summary>What one self-destruct did, for the HUD's toast (the last one to go off, see <see cref="SelfDestruct.Last"/>).</summary>
	struct SelfDestructReport
	{
		int Seq = 0;
		cs::String Name;
		double RadiusKm = 0, ContactHp = 0;
		/// <summary>Hulls (and craft, and stations) the blast reached, and how many of them it destroyed.</summary>
		int Hit = 0, Destroyed = 0;
		/// <summary>The crew and pilot who died aboard.</summary>
		double CrewLost = 0;
	};

	/// <summary>
	/// The self-destruct (US-T97): a timed overload of the reactor of whatever the player holds -- a fighter, a ship's helm,
	/// a station's controls, or a gun, which sets off the hull (or station) it is mounted on, the gunner being aboard. Two
	/// steps, so it never goes off by accident: the key held for <see cref="ArmHoldS"/> arms it, which starts a countdown
	/// (<see cref="FuseSeconds"/>: 5 s in a small craft, 8 s in a light hull, 15 s in a capital, 20 s on a station); the key
	/// pressed again aborts it. Handed back to its crew (or lost), the countdown stops.
	///
	/// At zero the craft is destroyed as a kill is (a wreck, its debris, its pieces) and its blast reaches everything within
	/// <see cref="RadiusKm"/> of its hull: <see cref="ContactHp"/> to whatever touches it, falling off with the square of the
	/// gap between the two hulls. Both scale with the cube root of its mass, and with its reactor (half as much with the
	/// reactor shot away): a fighter is a big grenade (about 2,200 hp, a third of a kilometre), a corvette 5,900 hp over 0.9
	/// km, a cruiser 27,000 hp over 4 km, a carrier 51,000 hp over 8 km. The damage goes in through the battle's own path
	/// (<see cref="Battle.ApplyDamage"/>): the shield over the patch struck takes it first, then armour, the hardpoints on the
	/// struck side by nearness, and a small craft's blast finds an open <see cref="WeakPoints"/> as its guns would. Out of a
	/// battle, ships near enough take it as a collision does (<see cref="Collisions.ApplyDamage"/>).
	///
	/// Everyone aboard dies (counted as casualties, <see cref="Crews.RecordDeaths"/>): nobody gets to the pods. The deed is the
	/// craft's own: its faction's and its captain's, never the player's (the hull is no longer counted as player-flown, so the
	/// law, US-T96, reads it as its crew's act). Only the player arms it: no crew ever does, so AI fights and the Bench run as
	/// before.
	/// </summary>
	class SPACEGODCORE_API SelfDestruct final
	{
	public:
		/// <summary>How long the key is held to arm it, seconds (read by the Runtime's input).</summary>
		static constexpr double ArmHoldS = 1.0;

		/// <summary>The blast at contact, hp per cube-root kilotonne of hull, and its reach, km per cube-root kilotonne (plus
		/// half the hull's own length).</summary>
		static constexpr double ContactHpPerCbrtKt = 1500;
		static constexpr double ReachKmPerCbrtKt = 0.2;

		/// <summary>The hull (or station) that blows when <paramref name="seat"/> is set off: a fighter's own, a helm's, a gun's
		/// host's, a station's; null for a seat with none (a satellite).</summary>
		static cs::Ref<ShipCombat> CombatOf(const cs::Ref<IEmbodiable>& seat);

		/// <summary>Whether <paramref name="seat"/> can be self-destructed now.</summary>
		static bool Can(const cs::Ref<IEmbodiable>& seat);

		/// <summary>The countdown, seconds, for <paramref name="seat"/>.</summary>
		static double FuseSeconds(const cs::Ref<IEmbodiable>& seat);
		static double FuseSeconds(const cs::Ref<ShipCombat>& c, bool smallCraft, bool station);

		/// <summary>Seconds left on a countdown under way on <paramref name="seat"/>, or -1.</summary>
		static double Remaining(const cs::Ref<IEmbodiable>& seat);
		static bool Armed(const cs::Ref<IEmbodiable>& seat) { return Remaining(seat) >= 0; }

		/// <summary>Arms or aborts (<paramref name="key"/>, a <see cref="SelfDestructKey"/>) on <paramref name="seat"/>.</summary>
		static void Command(const cs::Ref<IEmbodiable>& seat, int key);
		static void Command(const cs::Ref<ShipCombat>& c, int key, double fuse);

		/// <summary>One frame of the countdown on <paramref name="c"/>: <paramref name="key"/> first, then <paramref name="dt"/>
		/// off the clock. True when it reaches zero this frame (the caller then sets it off).</summary>
		static bool Tick(const cs::Ref<ShipCombat>& c, int key, double fuse, double dt);

		/// <summary>Stops a countdown (the seat handed back).</summary>
		static void Disarm(const cs::Ref<ShipCombat>& c);

		/// <summary>The blast's reach, km, and its damage at contact, hp, for a hull of <paramref name="c"/>.</summary>
		static double RadiusKm(const cs::Ref<ShipCombat>& c);
		static double ContactHp(const cs::Ref<ShipCombat>& c);

		/// <summary>Sets off <paramref name="u"/> in <paramref name="b"/>: the blast through the battle, the crew dead, the
		/// unit destroyed (its wreck and pods as any kill's, the pods empty).</summary>
		static SelfDestructReport InBattle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Sets off a hull out of any battle at <paramref name="offsetKm"/> from <paramref name="body"/>: the blast on
		/// the ships near it (as <see cref="Collisions"/> damages them), the crew dead. <paramref name="ship"/> (null for a
		/// station) is lost, its wreck left where it blew; a station's own removal is its seat's.</summary>
		static SelfDestructReport Outside(const cs::Ref<World>& world, const cs::Ref<ShipCombat>& c, const cs::String& name,
			const cs::Ref<Core::Faction>& owners, const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward);

		/// <summary>The last self-destruct to go off (its <see cref="SelfDestructReport.Seq"/> 0 before any has).</summary>
		static const SelfDestructReport& Last();

	private:
		static double ReactorFactor(const cs::Ref<ShipCombat>& c);
		static double MassKt(const cs::Ref<ShipCombat>& c);
		static double KillCrew(const cs::Ref<ShipCombat>& c, const cs::Ref<Core::Faction>& owners);
		static void Record(const SelfDestructReport& r);
	};
}
