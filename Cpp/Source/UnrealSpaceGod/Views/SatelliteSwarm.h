#pragma once
// Port of Unity.SpaceGod Runtime/Views/SatelliteSwarm.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Constellation;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Satellite constellations (Starlink, GPS, Galileo, GLONASS, BeiDou, the geostationary belt) as
	/// clouds of pixel-sized points, propagated on the CPU on their circular orbits and placed with
	/// the planet's own neighbourhood mapping. They fade out with distance from the planet.
	/// </summary>
	class SatelliteSwarm final
	{
		class Cloud final
		{
		public:
			cs::Ref<Core::Constellation> C;
			cs::Ref<UnityCompat::Mesh> Mesh;
			cs::Ref<MeshRenderer> Renderer;
			cs::Ref<UnityCompat::Material> Material;
			cs::List<Core::DVec3> Offsets;
			cs::List<Vector3> Vertices;
			cs::Ref<Transform> T;
		};

		UnitySpaceApp* app;
		cs::List<cs::Ref<Cloud>> clouds;

	public:
		SatelliteSwarm(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update(bool visible);
	};
}
