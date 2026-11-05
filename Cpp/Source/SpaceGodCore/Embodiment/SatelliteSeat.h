#pragma once
// Port of Unity.SpaceGod Core/Embodiment/SatelliteSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	class Constellation;
	class Satellite;
	class World;

	/// <summary>
	/// One satellite of a constellation as an <see cref="IEmbodiable"/> (U.SPC-T448). Taken, it leaves its Walker slot to
	/// fly on its own state (<see cref="Satellite"/>): the player turns and points it (its sensors and relay antenna look
	/// down its boresight) and nudges it about on its station-keeping thrusters. Handed back, its AI holds station again:
	/// it flies home to its slot from wherever it was left, settles back to its working attitude, and rejoins the cloud.
	/// The satellite is stepped by the world (<see cref="Infrastructure.UpdateSatellites"/>), so this only flips who holds it.
	/// </summary>
	class SPACEGODCORE_API SatelliteSeat final : public IEmbodiable
	{
		cs::Func<double> now;

	public:
		cs::Ref<Core::Constellation> Constellation;
		cs::Ref<BodyDefinition> Body;
		int Index = 0;

		/// <param name="now">The world's date, for where it leaves its slot when taken.</param>
		SatelliteSeat(const cs::Ref<Core::Constellation>& constellation, const cs::Ref<BodyDefinition>& body, int index, cs::Func<double> now);

		/// <summary>Satellite <paramref name="index"/> of a constellation in <paramref name="world"/>'s system, on its clock.</summary>
		static cs::Ref<SatelliteSeat> Of(const cs::Ref<World>& world, const cs::Ref<Core::Constellation>& constellation, int index);

		/// <summary>
		/// The constellation satellite nearest a point (km from <paramref name="body"/>'s centre) at a date, where it
		/// really is (in its slot, or flying on its own), or null if <paramref name="body"/> has none: what a picker
		/// offers for "the satellite there".
		/// </summary>
		static cs::Ref<SatelliteSeat> Nearest(const cs::Ref<World>& world, int body, DVec3 offsetKm, double jd);

		/// <summary>Its own state, while it flies on one (taken, or its AI still bringing it home); null while in its slot.</summary>
		cs::Ref<Core::Satellite> Satellite() const;

		cs::String Name() const override;
		EmbodimentKind Kind() const override { return EmbodimentKind::Satellite; }

		/// <summary>The constellations are the real, unowned spacecraft: no faction.</summary>
		cs::Ref<Core::Faction> Faction() const override { return nullptr; }

		cs::Ref<IEmbodiable> Host() const override { return nullptr; }
		bool CanEmbody() const override { return Blocker().empty(); }
		bool Embodied() const override;

		cs::String Blocker() const override;

		EmbodimentPose PoseAt(double jd) override;

		/// <summary>Its AI lets go: it leaves its slot where it is, still pointing where it was, and answers only the player.</summary>
		void TakeCharge() override;

		/// <summary>Where to point it and how to push it, for the world's next step of it.</summary>
		void Control(const EmbodimentInput& input, double dt) override;

		/// <summary>Its station-keeping AI takes it back, from wherever and however the player left it.</summary>
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
