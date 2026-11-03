#pragma once
// Port of Unity.SpaceGod Core/Embodiment/HelmControls.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Combat/PilotTuning.h" // PilotControl (by value)

namespace SpaceGod::Core
{
	class Ship;

	/// <summary>
	/// What a hull is for at the helm (U.SPC-T449): the role its control set is fitted to. Every ship can be taken
	/// (<see cref="ShipHelm"/>), and each role gets the controls that make sense for its work, not one shared set.
	/// </summary>
	enum class HelmRole : int
	{
		/// <summary>A strike craft (fighter, interceptor, heavy fighter, bomber, recon, gate picket): the stick is the
		/// gunsight, the triggers fight, boost and flight assist are for the dogfight.</summary>
		Fighter,

		/// <summary>A hull flying in someone's formation: flown by its class's stick like any other, cut loose from the
		/// formation while the player holds it, with the guns on the trigger to cover its charge.</summary>
		Escort,

		/// <summary>A courier or blockade runner: the light fighter's free-aim stick on the fastest hull in the game, flown
		/// to run rather than to fight.</summary>
		Courier,

		/// <summary>A freighter, hauler, tender or explorer: a slow, deliberate stick, the hull flying heavy on its load, and
		/// docking mode on Ctrl inside a port's approach.</summary>
		Freighter,

		/// <summary>A miner or an ore or capture tug: a freighter's controls, flown to match a rock or bring a load in.</summary>
		Miner,

		/// <summary>A capital ship: the keys turn the hull and ring the engine telegraph, the mouse looks round it, the
		/// trigger calls broadsides and the helm assist can roll the weak patches away.</summary>
		Capital,
	};
	SPACEGODCORE_API const char* ToString(HelmRole v);

	/// <summary>
	/// The control set a hull is flown with at the helm (U.SPC-T449, see <see cref="ShipHelm.Controls"/>), from its class's
	/// flight model and the role it plays: how the stick steers it (<see cref="PilotControl"/>), how quickly the mouse
	/// swings its nose, and which of the role-specific helm functions it has. The Runtime input layer reads it to turn
	/// the player's hands into <see cref="EmbodimentInput"/>; <see cref="ShipHelm.Control"/> reads it to fly the hull.
	/// </summary>
	struct SPACEGODCORE_API HelmControls
	{
		HelmRole Role = HelmRole::Fighter;

		/// <summary>How the stick steers the hull (U.SPC-T269).</summary>
		PilotControl Stick{};

		/// <summary>How far the mouse swings the nose, as a fraction of the fighter's: a freighter is a heavy thing to point.</summary>
		double LookFactor = 0;

		/// <summary>Ctrl inside a port's approach is docking mode (U.SPC-T443): haulers only.</summary>
		bool DockingMode = false;

		/// <summary>W and S ring the engine telegraph a step at a time, and the helm assist may roll the weak patches away
		/// (U.SPC-T499, U.SPC-T502): capital ships only.</summary>
		bool Telegraph = false;

		/// <summary>The hull carries guns, so the triggers fight.</summary>
		bool Guns = false;

		/// <summary>A freighter's mouse is slower than a fighter's (U.SPC-T269).</summary>
		static constexpr double HaulerLookFactor = 0.6;

		HelmControls() = default;

		HelmControls(HelmRole role, PilotControl stick, double lookFactor, bool dockingMode, bool telegraph, bool guns)
		{
			Role = role;
			Stick = stick;
			LookFactor = lookFactor;
			DockingMode = dockingMode;
			Telegraph = telegraph;
			Guns = guns;
		}

		/// <summary>The controls <paramref name="ship"/> is flown with at the helm.</summary>
		static HelmControls Of(const cs::Ref<Ship>& ship);

		/// <summary>The role <paramref name="ship"/> plays at the helm: a capital ship is always one; otherwise an escort
		/// flies as an escort, and the rest by their class and their work.</summary>
		static HelmRole RoleOf(const cs::Ref<Ship>& ship);

		/// <summary>The role's own keys, for the helm's toast (the keys every helm shares are the toast's own).</summary>
		cs::String Hint() const;

	private:
		cs::String Heavy() const;

		cs::String StickHint() const;
	};
}
