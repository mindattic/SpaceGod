#pragma once
// Port of Unity.SpaceGod Core/Embodiment/ShipHelm.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/HelmControls.h"

namespace SpaceGod::Core
{
	class BattleUnit;
	class HelmFlight;
	struct PilotState;
	class Ship;
	class World;

	/// <summary>
	/// A ship's helm as an <see cref="IEmbodiable"/> (U.SPC-T18): every ship role -- freighters, miners, couriers,
	/// escorts, fighters and capital ships -- is taken and flown through it (U.SPC-T449). Who may take it, where it
	/// starts and the take-charge/hand-back bookkeeping (<see cref="World.PilotedShip"/>, an escort cut loose, the war's
	/// command handed over, a fighting hull's <see cref="BattleUnit"/> passed between player and crew) live here, and
	/// so does the hull's flight: <see cref="Control"/> flies its <see cref="Flight"/> by its class's own flight model
	/// (U.SPC-T17) on the control set fitted to its role (<see cref="Controls"/>), paying for every burn from its tanks.
	/// What the world does around it (gravity, surfaces, collisions, the fight, the law) is the host's.
	/// </summary>
	class SPACEGODCORE_API ShipHelm final : public IEmbodiable
	{
		cs::Ref<World> world;

	public:
		cs::Ref<Core::Ship> Ship;

		ShipHelm(const cs::Ref<World>& world, const cs::Ref<Core::Ship>& ship);

		cs::String Name() const override;
		EmbodimentKind Kind() const override { return EmbodimentKind::Ship; }
		cs::Ref<Core::Faction> Faction() const override;
		cs::Ref<IEmbodiable> Host() const override { return nullptr; }
		cs::String Blocker() const override { return BlockerOf(Ship); }
		bool CanEmbody() const override { return Blocker().empty(); }
		bool Embodied() const override;

	private:
		/// <summary>The hull's place in the fight it is in, if any.</summary>
		cs::Ref<BattleUnit> Unit() const;

	public:
		/// <summary>
		/// Why <paramref name="ship"/>'s helm cannot be taken right now, or null if it can: only a wreck, a
		/// derelict, or a prize already taken in a fight still on (its captors' to dispose of when it ends).
		/// </summary>
		/// (Empty stands for C#'s null.)
		static cs::String BlockerOf(const cs::Ref<Core::Ship>& ship);

		/// <summary>
		/// The body whose frame best describes a ship's current position, and its offset there in true km --
		/// from wherever it actually is (parked, mid-lane, mid-burn, queued, landed), not only a parked orbit.
		/// </summary>
		/// C#: (int body, DVec3 offsetKm).
		static std::tuple<int, DVec3> FrameOf(const cs::Ref<World>& world, const cs::Ref<Core::Ship>& ship, double jd);

		EmbodimentPose PoseAt(double jd) override;

		/// <summary>
		/// The player takes the helm: the fleet stops scheduling the hull, an escort is cut loose from its
		/// formation (the war's command handed over otherwise), and a hull already fighting has its own
		/// <see cref="BattleUnit"/> handed to the player rather than a new fight started.
		/// </summary>
		void TakeCharge() override;

		/// <summary>The motion this helm flies (U.SPC-T449): given by whoever hosts the helm, which keeps it across frames.
		/// With none, <see cref="Control"/> hands over only the trigger and the crosshair.</summary>
		cs::Ref<HelmFlight> Flight;

		/// <summary>The control set the hull is flown with, fitted to its role (U.SPC-T449).</summary>
		HelmControls Controls() const;

		/// <summary>
		/// One frame at the helm: the trigger and the crosshair go to the hull's battle unit, and the hull flies its
		/// <see cref="Flight"/> on the stick (see <see cref="Fly"/>).
		/// </summary>
		void Control(const EmbodimentInput& input, double dt) override;

		/// <summary>
		/// Flies <paramref name="f"/> one frame (U.SPC-T449): the stick, in world terms (<paramref name="e"/>'s aim is
		/// where the nose is wanted), through the hull's class's flight model (U.SPC-T17), on its own hull and tuning,
		/// with what the role's controls add (a hauler's load and docking mode, a capital's telegraph and the helm assist
		/// rolling its weak patches away), nothing at all while its drives are down; then every change of velocity its
		/// thrusters made (not gravity's, which is free) is paid for from its tanks, and a dry hull only coasts.
		/// </summary>
		void Fly(const cs::Ref<HelmFlight>& f, const EmbodimentInput& e, double dt);

	private:
		static double Clamp1(double v);

		/// <summary>The self-destruct's countdown has run out (US-T97, <see cref="SelfDestruct"/>): the hull blows, in its
		/// fight or, out of one, where the helm has flown it.</summary>
		void SetOff();

		/// <summary>A capital's broadside stance (US-T81): beam-on to the hull locked in its fight, or the heading held with
		/// none; <paramref name="note"/> gets the HUD line naming the flank that bears.</summary>
		DVec3 BroadsideAim(const PilotState& s, cs::String& note) const;

	public:
		/// <summary>
		/// The crew takes the helm back: a fighting hull's own AI (broadsides, point defence, missiles) picks
		/// up where manual control left off, same facing and velocity. Pinning it where it was left and
		/// rescheduling it are the Runtime's, which knows the pilot's last spot.
		/// </summary>
		void HandBack() override;

		/// US-T135: the universal embody contract (<see cref="IEmbodiable"/>; bodies in EmbodimentRoles.cpp).
		EmbodimentInputProfile InputProfile() const override;
		EmbodimentCameraRig CameraRig() const override;
		EmbodimentRolePanel RolePanel() override;
		EmbodimentSubject Subject() const override;

		bool Equals(const cs::Ref<IEmbodiable>& obj) const override;
		size_t GetHashCode() const override;
	};
}
