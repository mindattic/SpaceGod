#pragma once
// Port of Unity.SpaceGod Core/Embodiment/FighterSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class FighterControl;
	class Subsystem;
	enum class WeaponGroup : int;
	enum class PowerSystem : int;
	enum class WingOrder : int;

	/// <summary>
	/// The cockpit of a launched fighter, heavy fighter or bomber, as an <see cref="IEmbodiable"/> (U.SPC-T422): one of a
	/// carrier's squadron, taken mid-battle and flown by the player (the X-wing run on the dreadnought). Taken, its pilot
	/// lets go and it flies by its own class's flight model (<see cref="FighterAI.FlyManned"/>), and every gunner on the
	/// other side shoots at it for real. The dogfight game (U.SPC-T424) is played from here: the guns on the main trigger
	/// and a selectable weapon group (missiles, torpedoes) on the second; a lock taken on the hostile nearest the
	/// crosshair, and a weak point picked on a locked capital ship for the missiles and torpedoes to go for; flares
	/// against missiles closing in; the reactor's power shared between weapons, shields and engines; and orders to the
	/// squadron's wingmen, who fly on it from the first order. Handed back, its pilot flies on from wherever it was left,
	/// picking a run of its own.
	/// </summary>
	class SPACEGODCORE_API FighterSeat final : public IEmbodiable
	{
	public:
		cs::Ref<Core::Battle> Battle;
		cs::Ref<BattleUnit> Unit;

		FighterSeat(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit);

		/// <summary>What it is: "interceptor", "fighter", "heavy fighter", "bomber" or "assault shuttle".</summary>
		cs::String Craft() const;

		cs::String Name() const override;

		EmbodimentKind Kind() const override { return EmbodimentKind::Fighter; }
		cs::Ref<Core::Faction> Faction() const override;
		cs::Ref<IEmbodiable> Host() const override { return nullptr; }
		bool CanEmbody() const override { return Blocker().empty(); }
		bool Embodied() const override;

		cs::String Blocker() const override;

		EmbodimentPose PoseAt(double jd) override;

	private:
		static DVec3 UpOf(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>The pilot lets go: the stick is the player's, holding the heading and speed the craft had.</summary>
		void TakeCharge() override;

		/// <summary>Stick, throttle (-1..1 eases it off or opens it up; it holds where it is left), strafe, roll, boost and triggers.</summary>
		void Control(const EmbodimentInput& input, double dt) override;

		/// <summary>How fast a held throttle key opens or closes the throttle: full range in two seconds.</summary>
		static constexpr double ThrottlePerSecond = 0.5;

		/// <summary>Its pilot takes the stick back and flies a run of its own from here.</summary>
		void HandBack() override;

	private:
		cs::Ref<FighterControl> Stick() const;

	public:
		// ------------------------------------------------------------ the dogfight game (U.SPC-T424)

		/// <summary>The group on the second trigger, or null when not held.</summary>
		cs::Nullable<WeaponGroup> SecondaryGroup() const;

		/// <summary>Puts the next guided group the craft carries on the second trigger; returns it, or null if it carries none.</summary>
		cs::Nullable<WeaponGroup> CycleSecondaryGroup();

		/// <summary>How far round the crosshair a lock may be taken, deg, and how far off, km.</summary>
		static constexpr double LockPickDeg = 30;
		static constexpr double LockPickKm = 15;

		/// <summary>
		/// Takes a lock on the hostile nearest the crosshair <paramref name="aim"/> (within <see cref="LockPickDeg"/> and
		/// <see cref="LockPickKm"/>); pressed again with that one already locked, the next nearest. A capital ship locked
		/// gets its most telling weak point picked. The lock then builds while the crosshair sits on it. Returns the lock, or null.
		/// </summary>
		cs::Ref<BattleUnit> CycleLock(DVec3 aim);

		/// <summary>Locks <paramref name="target"/> (null drops the lock); a capital ship gets its most telling weak point picked.</summary>
		void Lock(const cs::Ref<BattleUnit>& target);

		/// <summary>The next (or previous) weak point on the locked capital ship; returns it, or null if there is none.</summary>
		cs::Ref<Subsystem> CycleWeakPoint(int step = 1);

		/// <summary>Throws a flare-and-chaff cloud astern this step, if the dispenser is ready and has any left.</summary>
		void DropCountermeasures();

		/// <summary>Clouds left in the craft's dispensers.</summary>
		int Countermeasures() const;

		/// <summary>Flight assist on or off; returns whether it is now off.</summary>
		bool ToggleFlightAssist();

		bool FlightAssistOff() const;

		/// <summary>Moves a pip of the reactor's power to <paramref name="s"/>; false if it has as much as it can take.</summary>
		bool ShiftPower(PowerSystem s);

		/// <summary>Back to an even share of power.</summary>
		void BalancePower();

		/// <summary>Takes the squadron and gives its wingmen <paramref name="order"/>; returns how many heard it.</summary>
		int OrderWing(WingOrder order);

		/// <summary>The squadron's wingmen still flying.</summary>
		int Wingmen() const;

		/// <summary>A guided round closing on the craft (within flare reach), for the HUD's warning.</summary>
		bool MissileWarning() const;

		/// <summary>
		/// The cockpit's readout for the HUD: the craft, hull and shields, throttle and boost, power, the second trigger's
		/// group and rounds, flares, the lock and its weak point (open or not from here), the wing's order, and warnings.
		/// </summary>
		cs::String Readout();

	private:
		static cs::String Pct(double f);

	public:
		bool Equals(const cs::Ref<IEmbodiable>& obj) const override;
		size_t GetHashCode() const override;
	};
}
