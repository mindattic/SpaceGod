#pragma once
// Port of Unity.SpaceGod Core/Embodiment/StationSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class JumpGate;
	class MountControl;
	class Settlement;
	class ShipCombat;
	class Subsystem;
	class World;

	/// <summary>
	/// A station as an <see cref="IEmbodiable"/> (U.SPC-T447): one of a settlement's defence platforms, or one of the forts
	/// guarding a jump gate. It never moves, but it has a place of its own whether or not a fight is going on round it
	/// (<see cref="PoseAt"/>), and taken it lays every gun it has on the player's aim at each gun's own traverse rate, within
	/// each gun's own fire cone, and in a fight fires them all down their barrels on the trigger (every shot a real aimed
	/// one, through <see cref="Battle.FireManned"/>); the second trigger launches whatever craft its hangars hold. Its
	/// launchers and everything else go on under its crew. Handed back, its crew works every gun again from where the
	/// player left it. Its <see cref="MountedTurret"/>s have it as their <see cref="IEmbodiable.Host"/>, so a single gun
	/// seat can move up to the whole station.
	/// </summary>
	class SPACEGODCORE_API StationSeat final : public IEmbodiable
	{
		cs::Ref<World> world;

		/// <summary>The fight it was found in, if it was found through one: tried first when finding its battle unit.</summary>
		cs::Ref<Battle> seen;

	public:
		/// <summary>The settlement it defends (null for a gate fort).</summary>
		cs::Ref<Core::Settlement> Settlement;

		/// <summary>The gate it guards (null for a settlement's platform).</summary>
		cs::Ref<JumpGate> Gate;

		/// <summary>The station's own hull: its guns, hangars and damage, kept between fights.</summary>
		cs::Ref<ShipCombat> Combat;

	private:
		/// <summary>The second trigger as last seen, so a held trigger launches once.</summary>
		bool secondaryWas = false;

	public:
		StationSeat(const cs::Ref<World>& world, const cs::Ref<Core::Settlement>& settlement, const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& combat, const cs::Ref<Battle>& battle = nullptr);

	private:
		/// <summary>The world it stands in: a fort's is its gate's system's.</summary>
		cs::Ref<World> WorldOf() const;

		/// <summary>Which of its settlement's platforms (or its gate's forts) it is, and how many there are.</summary>
		int Index() const;

		int Count() const;

	public:
		cs::String Name() const override;

		EmbodimentKind Kind() const override { return EmbodimentKind::Station; }

		cs::Ref<Core::Faction> Faction() const override;

		/// <summary>A station is mounted on nothing.</summary>
		cs::Ref<IEmbodiable> Host() const override { return nullptr; }

		bool CanEmbody() const override { return Blocker().empty(); }

		bool Embodied() const override;

		cs::String Blocker() const override;

		/// <summary>
		/// Its place in the fight going on round it, or null when there is none: the unit in a battle still on whose hull is
		/// this station's (the fight it was found through first, then any in its world).
		/// </summary>
		cs::Ref<BattleUnit> Unit(cs::Ref<Battle>& battle) const;

	private:
		cs::Ref<BattleUnit> UnitIn(const cs::Ref<Battle>& b) const;

	public:
		/// <summary>
		/// Where it holds station outside a fight: a settlement's platforms ring its world in the ecliptic at the distance its
		/// fights are fought (<see cref="Battle.AnchorDistanceKm"/>), a gate's forts ring the gate 6 km out (as they stand in a
		/// gate battle), each facing out from what it guards, roof to ecliptic north.
		/// </summary>
		/// C#: (int body, DVec3 offsetKm, DVec3 forward).
		static std::tuple<int, DVec3, DVec3> RestingPlace(const cs::Ref<World>& world, const cs::Ref<Core::Settlement>& settlement, const cs::Ref<JumpGate>& gate, int index, int count);

		EmbodimentPose PoseAt(double jd) override;

	private:
		/// <summary>Every gun on it the player can lay right now (direct fire, intact).</summary>
		/// (C# yields them lazily; nothing the callers do changes which guns qualify, so an eager list is the same.)
		cs::List<cs::Ref<Subsystem>> Guns() const;

	public:
		/// <summary>The station's crew stand aside from its guns: they are the player's, still pointing where they were left.</summary>
		void TakeCharge() override;

	private:
		/// <summary>Every gun on <paramref name="u"/> not already the station's is laid by the player from now on (a gun
		/// manned on its own is taken into the whole), sharing the one hand on them all.</summary>
		cs::Ref<MountControl> Man(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>
		/// One frame at the station: every gun swings toward the aim at its own rate within its own cone (in a fight, by the
		/// battle's own step; out of one, here), the trigger fires them in a fight, and the second trigger, pressed,
		/// launches its craft.
		/// </summary>
		void Control(const EmbodimentInput& input, double dt) override;

	private:
		/// <summary>Out of a fight, its guns traverse toward <paramref name="aim"/> at their own rates, each within its own
		/// cone, reckoned from where the station holds station; nothing is fired at.</summary>
		void TrainGuns(DVec3 aim, double dt);

		/// <summary>The self-destruct's countdown has run out (US-T97, <see cref="SelfDestruct"/>): the station blows, as its
		/// unit <paramref name="u"/> in <paramref name="b"/>, or where it stands out of a fight.</summary>
		void SetOff(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>
		/// Puts every craft fit to fly in its hangars out into the fight round it (the same launch a carrier makes,
		/// <see cref="FighterAI.Launch"/>); returns how many went. None outside a fight, nor from a station with no hangar
		/// left or nothing aboard.
		/// </summary>
		int LaunchCraft();

		/// <summary>The crew takes every gun back and picks its own targets again; the guns point where they were left.</summary>
		void HandBack() override;

		bool Equals(const cs::Ref<IEmbodiable>& obj) const override;
		size_t GetHashCode() const override;
	};
}
