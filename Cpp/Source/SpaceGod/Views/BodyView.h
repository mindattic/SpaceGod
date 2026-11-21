#pragma once
// Port of Unity.SpaceGod Runtime/Views/BodyView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "GasGiantWeather.h"
#include "Rendering/VoidSiteModel.h"

#include <vector>

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
		cs::Ref<MeshRenderer> glowRenderer;

		/// <summary>US-T442: the god view from inside the sphere: its structure as a wireframe (shells at its layers'
		/// depths, its own latitude and longitude, the axis) and its skin seen through from within; built the first time
		/// the camera goes in.</summary>
		cs::Ref<MeshRenderer> interiorWire, interiorSkin;
		cs::Ref<Material> interiorSkinMaterial;
		float interiorShown = 0;
		void BuildInterior();

	public:
		/// <summary>Scales how large a star's corona is kept on screen from afar (1 normally): arriving from between the
		/// stars it starts no bigger than the star point it takes over from and grows to full as the camera falls in.</summary>
		static inline float CoronaPresence = 1.0f;

	private:
		/// <summary>A free-flying station: drawn as a model with the ship shader, not a sphere.</summary>
		bool station = false;

		/// <summary>US-T143: the Space Station Creator model drawn in place of the procedural one (a station, a Void
		/// Station's frame, a megastructure), when the pack is there; shown and hidden with the body.</summary>
		cs::Ref<GameObject> sscRoot;

		// A void station's generator (US-T84): a metal Dyson sphere (see VoidStationModel). The root's own renderer draws
		// the far shell; the near shell, the dark core, the accretion ring and the two gimbal rings are its children.
		bool voidStation = false;
		bool nearDetail = false;
		cs::Ref<Material> metal;
		cs::Ref<MeshRenderer> nearShell;
		cs::Ref<Transform> accretion;
		cs::Ref<Transform> gimbals[2];

		/// <summary>The generator as a Dyson sphere: builds the shell, core and rings under the root.</summary>
		void BuildVoidStation(const cs::Ref<Transform>& parent);

		/// <summary>Turns the shell and its rings, and picks the level of detail by how large it is on screen.</summary>
		void ApplyVoidStation(Vector3 relative, float radius, bool highlighted, Vector3 cameraForward);

		// A void station's site (US-T123, Core's VoidSites): its own model, the debris drawn round it (laid out again when the
		// scale blend moves) and a halo for a sight that glows.
		bool voidSite = false;
		cs::List<Rendering::VoidSiteModel::Fragment> debrisPieces;
		cs::Ref<Mesh> debrisMesh;
		cs::Ref<Transform> debris;
		double laidBlend = cs::Double::NaN;
		cs::Ref<Transform> siteHalo;
		cs::Ref<Material> siteHaloMaterial;

		/// <summary>Builds a void site's model, debris and halo; false if the body is no site.</summary>
		bool BuildVoidSite(const cs::Ref<Transform>& parent);

		/// <summary>Lays out and turns the debris and faces the halo to the camera.</summary>
		void ApplyVoidSite(Vector3 relative, float radius, Vector3 cameraForward);

		// US-T449: the frontier's strange bodies. A pulsar is a small white-hot core (a fraction of the radius the framing
		// takes) in a tight blue-white glare and a wider halo, its two beams sweeping round its spin axis (one long glow
		// through the star along its magnetic axis); a black hole is a black shadow, a tilted accretion disc in its equator
		// (hot at its inner edge, the side turning toward the eye brighter and bluer: Doppler beaming) and a thin photon ring
		// hugging the shadow (no wide glow quad: up close it washed the view out white); a rogue planet is a dark world faintly warm; a relic is a broken ring-world.
		bool pulsar = false, blackHole = false, rogue = false, relic = false;
		cs::Ref<Transform> beam;
		cs::Ref<Material> beamMaterial;
		cs::Ref<Transform> accretionDisc;
		cs::Ref<Material> accretionMaterial;
		cs::Ref<Transform> accretionGlare;
		cs::Ref<Material> accretionGlareMaterial;
		cs::Ref<Transform> photonRing, pulsarHalo;
		cs::Ref<Material> photonMaterial, pulsarHaloMaterial;
		cs::Ref<Mesh> discMesh, photonMesh;
		std::vector<float> discAngles, photonAngles, discRadial;
		/// <summary>The disc's Doppler lean last coloured in (-1: never), so its colours are only rebuilt when it changes.</summary>
		float lastDoppler = -1;
		/// <summary>The pulsar's core, a share of the radius the view frames it at: small and dazzling within its glare.</summary>
		static constexpr float PulsarCoreScale = 0.22f;
		void BuildFrontierBody(const cs::Ref<Transform>& parent);
		void ApplyFrontierBody(Vector3 relative, float radius, Vector3 cameraForward);
		cs::Ref<Transform> AddGlowQuad(const cs::Ref<Transform>& parent, const cs::String& name, Color color, float intensity, float falloff, cs::Ref<Material>& made);
		/// <summary>A flat band of rings (radius, glow-profile u) drawn with the Glow material: its glow peaks where u is 0.5
		/// and dies where u is 1, so the profile across the band is set by the rings. Both faces drawn.</summary>
		cs::Ref<Transform> AddGlowBand(const cs::Ref<Transform>& parent, const cs::String& name, const std::vector<float>& radii, const std::vector<float>& us,
			float intensity, float falloff, cs::Ref<Material>& made, cs::Ref<Mesh>& mesh, std::vector<float>& angles, std::vector<float>* radial);
		/// <summary>Colours a band: brighter and bluer toward its local +Y by <paramref name="lean"/> (0 even, 1 edge-on).</summary>
		static void ColourBand(const cs::Ref<Mesh>& mesh, const std::vector<float>& angles, const std::vector<float>* radial, float lean, Color hot, Color cool);

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

		/// <summary>
		/// US-T442: the free (god) camera inside the sphere, <paramref name="fade"/> 0 (outside, drawn as ever) to 1 (well
		/// in): its skin is drawn semi-transparent from within, thinning as the camera goes deeper, and its inside as a
		/// wireframe, so the rest of the game shows through. A star's corona is put away while inside it. Spheres only: a
		/// station, a void station or a void site draws as ever.
		/// </summary>
		void SetInterior(float fade);

		/// <summary>Whether it draws an inside for the god view (a visible sphere: a star, planet, dwarf planet or moon).</summary>
		bool HasInterior() const;

		static Vector3 ToUnity(Core::DVec3 ecliptic)
		{
			return Vector3(static_cast<float>(ecliptic.X), static_cast<float>(ecliptic.Z), static_cast<float>(ecliptic.Y));
		}

	private:
		static cs::Ref<Mesh>& sphereMesh();
		static cs::Ref<Mesh>& quadMesh();
	};
}
