#pragma once
// Port of Unity.SpaceGod Runtime/Views/BodyView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "GasGiantWeather.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
}

namespace SpaceGod::Views
{
	/// <summary>The visible sphere (plus rings or corona) for one body. Positioned camera-relative every frame.</summary>
	class BodyView final
	{
	public:
		cs::Ref<Core::BodyDefinition> Body;
		cs::Ref<GameObject> Root;

	private:
		cs::Ref<MeshRenderer> renderer;
		cs::Ref<Material> material;
		cs::Ref<Transform> ring;
		cs::Ref<Material> ringMaterial;
		cs::Ref<Transform> glow;
		cs::Ref<Material> glowMaterial;

	public:
		/// <summary>Scales how large a star's corona is kept on screen from afar (1 normally): arriving from between the
		/// stars it starts no bigger than the star point it takes over from and grows to full as the camera falls in.</summary>
		static inline float CoronaPresence = 1.0f;

	private:
		/// <summary>A free-flying station: drawn as a model with the ship shader, not a sphere.</summary>
		bool station = false;

		// A flare star brightens suddenly and fades over seconds (Proxima Centauri).
		bool flareStar = false;
		float flare = 0;
		float nextFlareAt = 0;

		// Gas giant weather (GasGiant.shader).
		const Core::GasGiantWeather::Profile* windProfile = nullptr;
		cs::List<float> drift = cs::List<float>(std::vector<float>(37));
		float flowLoopDays = 0;
		bool hasGreatRedSpot = false;

		/// <summary>Venus's clouds super-rotate in about 4 days, retrograde like the planet (Mariner 10, Venus Express).</summary>
		static constexpr double VenusCloudPeriodDays = 4.0;

	public:
		BodyView(const cs::Ref<Core::BodyDefinition>& body, const cs::Ref<Transform>& parent);

		/// <summary>
		/// Places the body. <paramref name="relative"/> is scene position minus camera position;
		/// <paramref name="mapNorth"/> and <paramref name="primeMeridian"/> are its body-fixed frame
		/// (ecliptic unit vectors, see <see cref="BodyOrientation"/>).
		/// </summary>
		void Apply(Vector3 relative, float radius, Core::DVec3 mapNorth, Core::DVec3 primeMeridian, bool highlighted, Vector3 cameraForward);

		/// <summary>
		/// Advances moving cloud decks to simulation time <paramref name="jd"/>. <paramref name="simSecondsPerSecond"/>
		/// is the time warp: when a flow loop would pass in under a couple of real seconds the swirl
		/// fades out rather than strobing, while band and spot positions stay exact.
		/// </summary>
		void UpdateWeather(double jd, double simSecondsPerSecond);

		void SetVisible(bool visible);

		static Vector3 ToUnity(Core::DVec3 ecliptic)
		{
			return Vector3(static_cast<float>(ecliptic.X), static_cast<float>(ecliptic.Z), static_cast<float>(ecliptic.Y));
		}

	private:
		static cs::Ref<Mesh>& sphereMesh();
		static cs::Ref<Mesh>& quadMesh();
	};
}
