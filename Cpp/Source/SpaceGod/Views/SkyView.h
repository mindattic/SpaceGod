#pragma once
// Port of Unity.SpaceGod Runtime/Views/SkyView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

#include "Async/Async.h"

#include <atomic>

namespace SpaceGod::Core
{
	class SkyboxMemory;
	class SkyboxSpec;
	class GameSetting;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The skybox behind the system in view (U.SPC-T595): one of <see cref="SkyboxCatalog"/>'s panoramas on a unit cube
	/// pushed to the far plane (Unity.SpaceGod/Sky), drawn just before the point-cloud starfield, which it hides when the
	/// sky has stars of its own. Every system shows the sky <see cref="Memory"/> keeps for it; <see cref="Cycle"/> (Shift+K,
	/// numpad *) moves it on. Nothing loads on the frame: a shipped panorama loads asynchronously from Resources, a
	/// dropped-in one is decoded on a worker thread (Unity's UnityWebRequestTexture), and a seeded sky is baked on worker
	/// threads; the old sky stays up until the new one is ready, which then fades in.
	/// </summary>
	class SkyView final
	{
	public:
		/// <summary>The folder the player drops their own panoramas into (2:1 .jpg or .png): under the game's data folder.</summary>
		static cs::String DropInFolder();

	private:
		static constexpr float FadeSeconds = 0.6f;
		static constexpr const char* MainTexId = "_MainTex";
		static constexpr const char* BrightnessId = "_Brightness";
		static constexpr const char* AxisXId = "_AxisX";
		static constexpr const char* AxisYId = "_AxisY";
		static constexpr const char* AxisZId = "_AxisZ";

	public:
		cs::Ref<Core::SkyboxMemory> Memory;

	private:
		cs::Ref<GameObject> root;
		cs::Ref<MeshRenderer> renderer;
		cs::Ref<Material> material;
		cs::Ref<Mesh> mesh;
		cs::Ref<GameObject> starfield;

		cs::String systemName;
		cs::Ref<Core::SkyboxSpec> wanted;
		cs::String wantedKey, shownKey;
		cs::Ref<Texture2D> texture;
		bool textureFromResources = false;
		float fade = 1;
		int request = 0;

		cs::Ref<ResourceRequest> pendingResource;
		/// <summary>A dropped-in panorama decoding on a worker (Unity's UnityWebRequestTexture).</summary>
		TFuture<UnityCompat::TextureFactory::DecodedImage> pendingFile;
		bool pendingFileActive = false;
		TFuture<cs::List<uint8_t>> pendingBake;
		bool pendingBakeActive = false;
		int pendingBakeWidth = 0;
		std::shared_ptr<cs::List<int>> pendingBakeMips;
		std::shared_ptr<std::atomic<bool>> bakeCancel;
		int onBrightness = 0, onResolution = 0;
		bool suppressed = false; // US-T145
		bool starfieldWanted = true, rendererWanted = false; // US-T145: what the sky on screen asks for, suppressed or not

	public:
		SkyView(const cs::Ref<Transform>& parent, const cs::Ref<GameObject>& starfield);

		/// <summary>The sky the system in view shows (or is loading).</summary>
		cs::Ref<Core::SkyboxSpec> Current() const { return wanted; }

		/// <summary>Registers every panorama in <see cref="DropInFolder"/> with the catalogue (once, at start); returns how many.</summary>
		static int DiscoverDropIns();

		/// <summary>Shows the sky <paramref name="system"/> keeps (called whenever the system in view changes).</summary>
		void Show(const cs::String& system);

		/// <summary>Gives the system in view the next sky (<paramref name="step"/> back for a negative one) and keeps it; returns it.</summary>
		cs::Ref<Core::SkyboxSpec> Cycle(int step = 1);

		/// <summary>
		/// US-T133: starts the sky <paramref name="system"/> keeps loading (or baking) now, off screen, in a slot of its own,
		/// so that <see cref="Show"/> for it later swaps it in at once with no fade: a system streamed in ahead of arrival
		/// has its sky ready before it is seen. One system is held ahead at a time; asking for another drops the last. A
		/// star not yet built into a system is asked for by its name (the seeded sky is the name's).
		/// </summary>
		void Prefetch(const cs::String& system);

		/// <summary>Whether the sky <paramref name="system"/> keeps is ready to show at once (held ahead, or on screen).</summary>
		bool PrefetchReady(const cs::String& system) const;

		/// <summary>Lets go of the sky held ahead for <paramref name="system"/> (no longer headed for).</summary>
		void DropPrefetch(const cs::String& system);

		/// <summary>Picks up a finished load or bake and fades the sky in; once a frame.</summary>
		void Update(float dt);

		/// <summary>
		/// US-T145: hides the sky (and the point stars it shows) while another layer owns the backdrop (SkyLayers: the
		/// Milky Way between the stars, a hero sky sphere, the painted volume's far layer). It keeps loading and fading as
		/// usual underneath, so lifting it shows the system's sky at once.
		/// </summary>
		void Suppress(bool value);
		bool Suppressed() const { return suppressed; }

		/// <summary>US-T145: smears the sky into a tunnel round <paramref name="axis"/> (Unity world axes, unit), 0..1: the jump drive.</summary>
		void SetSmear(float amount, Vector3 axis);

		/// <summary>US-T145: the key of the sky on screen (changes whenever another sky is swapped in).</summary>
		const cs::String& ShownKey() const { return shownKey; }

		/// <summary>Lets go of the sky's texture, material and mesh (the game scene is going).</summary>
		void Dispose();

	private:
		/// <summary>US-T133: a sky loading or baked ahead of the system it belongs to being shown.</summary>
		struct Ahead
		{
			cs::String Key, System;
			cs::Ref<Core::SkyboxSpec> Spec;
			cs::Ref<Texture2D> Texture;
			bool FromResources = false, Ready = false, Failed = false;
			cs::Ref<ResourceRequest> Resource;
			TFuture<UnityCompat::TextureFactory::DecodedImage> File;
			bool FileActive = false;
			TFuture<cs::List<uint8_t>> Bake;
			bool BakeActive = false;
			int BakeWidth = 0;
			std::shared_ptr<cs::List<int>> Mips;
			std::shared_ptr<std::atomic<bool>> Cancel;
		};
		std::shared_ptr<Ahead> ahead;

		/// <summary>The key a sky is loaded under for a system (a seeded sky is the system's own, at the present size).</summary>
		static cs::String KeyFor(const cs::Ref<Core::SkyboxSpec>& spec, const cs::String& system);
		void UpdateAhead();
		/// <summary>The sky held ahead goes on screen (at once, no fade, if <paramref name="instant"/>).</summary>
		void TakeAhead(bool instant);
		void DropAhead();

		void Apply(const cs::Ref<Core::SkyboxSpec>& spec);
		void OnResourceLoaded(int id, const cs::Ref<Core::SkyboxSpec>& spec, const cs::Ref<ResourceRequest>& req);
		static cs::Ref<Texture2D> Upload(const cs::List<uint8_t>& data, int width, const cs::List<int>& mips);
		void Swap(const cs::Ref<Texture2D>& tex, bool fromResources, const cs::Ref<Core::SkyboxSpec>& spec);
		void ApplyBrightness();
		void CancelPending();
		void Release();
		static void Free(const cs::Ref<Texture2D>& tex, bool fromResources);
		static cs::Ref<Mesh> BuildCube();
	};
}
