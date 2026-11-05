#pragma once
// US-T145: the sky as two layers, the travel between them, and the jump's tunnel.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <atomic>
#include <memory>
#include <vector>

namespace SpaceGod::Core
{
	class SkyboxSpec;
}

namespace SpaceGod::Rendering
{
	struct SystemGrade;
}

namespace SpaceGod::Views
{
	/// <summary>How a system's sky is drawn (US-T145), compared in place with <c>sg.SkyMode</c>.</summary>
	enum class SkyMode : int
	{
		/// <summary>The system's painted sky on a cube at infinity (US-T132): one flat layer, no edge to fly through.</summary>
		Cube,

		/// <summary>The hero painting (a hand-painted sky sphere) at infinity, in every system: the art reference.</summary>
		Sphere,

		/// <summary>
		/// Two layers: the infinite one (the Milky Way, which never moves) behind the local one, the system's painting
		/// projected into a volume about the size of its outer rim. From inside it reads as the painting; toward the rim it
		/// thins, with depth to fly through; from outside it is a cloud wrapped round the star.
		/// </summary>
		Volume,
	};

	/// <summary>
	/// The sky's layers and the travel between them (US-T145). Owned by the app beside <see cref="SkyView"/> (the
	/// system's own sky), which it hides whenever it owns the backdrop:
	/// <list type="bullet">
	/// <item>the infinite layer: ESO's Milky Way panorama in its galactic frame, the band and bulge in their true
	/// directions, between the stars (warp, jumps, void stops) and behind the painted volume;</item>
	/// <item>the local layer: the painted volume (M_Nebula), the system's painting (or the hero painting in the hero system)
	/// raymarched inside a sphere of the system's outer rim, so leaving a system is flying out through its cloud;</item>
	/// <item>the jump stars: every star the drive can jump to from here, a bright point in its true direction over the
	/// painted sky, so the star seen is the star aimed at;</item>
	/// <item>the jump's tunnel (M_Warp, and the sky's own smear): streaks that build with the drive's charge; one sky is
	/// swapped for the next only while they are at full strength.</item>
	/// </list>
	/// Presentation only: nothing here touches the simulation.
	/// </summary>
	class SkyLayers final
	{
	public:
		/// <summary>The hero painting's catalogue id and its shipped panorama (Resources/Skyboxes).</summary>
		static constexpr const char* HeroSkyId = "jungle-jim-alien-space-nebula";
		static constexpr const char* HeroSkyPath = "Skyboxes/jungle_jim_alien_space_nebula_8k";
		static constexpr const char* MilkyWayPath = "Skyboxes/eso_milky_way_6k";

		/// <summary>The mode every system is drawn in (process-wide; <c>sg.SkyMode</c>).</summary>
		static SkyMode Mode();
		static void SetMode(SkyMode mode);
		static const char* ModeName(SkyMode mode);
		static bool ParseMode(const cs::String& text, SkyMode& mode);

		/// <summary>The hero painting as a catalogue sky ("Alien Space Nebula 1" by Jungle Jim, CC BY 4.0), registered once.</summary>
		static cs::Ref<Core::SkyboxSpec> HeroSpec();

		/// <summary>The hero painting's white-hot core, ecliptic (unit): the key light's heart.</summary>
		static Core::DVec3 HeroHeart();

		/// <summary>The light graded to the hero painting: its core fills the scene, violet and gold.</summary>
		static Rendering::SystemGrade HeroGrade(const cs::String& place);

		/// <summary>What the layers show this frame, from the app.</summary>
		struct Frame
		{
			/// <summary>The system in view (empty: none), whether it is the hero system and has a painted sky (not Sol).</summary>
			cs::String System;
			bool Hero = false, Wild = false;

			/// <summary>Between the stars (the FTL crossing and its void stops): the Milky Way only.</summary>
			bool Interstellar = false;

			/// <summary>The system's centre relative to the camera and its outer rim's radius, scene units (Unity axes).</summary>
			Vector3 CenterRelative;
			float RimRadius = 0;

			/// <summary>The jump's tunnel, 0..1, and the way it runs (Unity world axes, unit).</summary>
			float Warp = 0;
			Vector3 WarpAxis = Vector3(0, 0, 1);
		};

	private:
		struct Panorama
		{
			cs::String Key;
			cs::Ref<Texture2D> Texture;
			bool FromResources = false, Loading = false;
			cs::Ref<ResourceRequest> Request;
			int Frame = 0; // a Core::SkyFrame
		};

		cs::Ref<GameObject> root;
		cs::Ref<GameObject> farGo, volumeGo, warpGo, jumpGo;
		cs::Ref<MeshRenderer> farRenderer, volumeRenderer, warpRenderer, jumpRenderer;
		cs::Ref<Material> farMaterial, volumeMaterial, warpMaterial, jumpMaterial;
		cs::Ref<Mesh> cube, sphere, jumpMesh;
		Panorama milkyWay, hero, local;
		/// <summary>The system's painting being made into the volume, without its giant worlds (they are drawn as bodies).</summary>
		struct LocalBake
		{
			std::atomic<bool> Done{ false };
			std::vector<std::vector<uint8_t>> Levels;
			int Width = 0;
		};
		std::shared_ptr<LocalBake> localPending;
		cs::String localPendingFor;

		/// <summary>The painting's giant worlds and moons, as bodies out in the volume (they drift past as you fly).</summary>
		struct Giant
		{
			cs::Ref<GameObject> Go;
			cs::Ref<Material> Mat;
			cs::Ref<MeshRenderer> Renderer;
			Vector3 Direction;
			float AngularRadius = 0;
		};
		std::vector<Giant> giants;
		cs::String giantsFor;
		cs::Ref<Mesh> planetMesh;
		void BuildGiants(const cs::String& system);
		void ClearGiants();
		cs::String farShown;
		float farFade = 1;
		float lastSmear = -1;
		bool owns = false;

		static cs::Ref<Mesh> BuildCube();
		static cs::Ref<Mesh> BuildInwardSphere();
		void Load(Panorama& p, const cs::String& path, int frame);
		bool Ready(Panorama& p);
		void UpdateLocal(const Frame& f);
		void ShowFar(Panorama& p, bool instant);
		void Release(Panorama& p);

	public:
		SkyLayers(const cs::Ref<Transform>& parent);

		/// <summary>Whether the layers own the backdrop this frame (the system's own sky is hidden).</summary>
		bool OwnsBackdrop() const { return owns; }

		/// <summary>Whether the painted volume is up (Volume mode, in a painted system, its painting loaded).</summary>
		bool VolumeShown() const;

		/// <summary>Draws the layers for this frame.</summary>
		void Update(const Frame& f, float dt);

		/// <summary>The stars the drive can jump to from here: unit directions (Unity world axes) and colours (alpha: brightness).</summary>
		void SetJumpStars(const std::vector<Vector3>& directions, const std::vector<Color>& colors);
		void ShowJumpStars(bool shown);

		/// <summary>The smear the system's own sky should take this frame (0..1).</summary>
		static float SmearFor(float warp) { return warp <= 0.02f ? 0.0f : warp * 0.9f; }

		/// <summary>The volume's look (density, brightness, steps), for tuning from the console (sg.SkyVolume).</summary>
		static float& VolumeDensity();
		static float& VolumeBrightness();
		static float& VolumeSteps();

		void Dispose();
	};
}
