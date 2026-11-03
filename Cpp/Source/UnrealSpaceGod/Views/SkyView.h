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

		/// <summary>Picks up a finished load or bake and fades the sky in; once a frame.</summary>
		void Update(float dt);

		/// <summary>Lets go of the sky's texture, material and mesh (the game scene is going).</summary>
		void Dispose();

	private:
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
