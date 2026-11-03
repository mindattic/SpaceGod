#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/AssetStreamer.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	class StarSystem;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Loads textures and ship models in the background (U.SPC-T414): never a <c>Resources.Load</c> on the frame that
	/// wants the asset. A view asks with <see cref="Request{T}"/> and draws a stand-in (a tinted sphere, an icon) until
	/// its callback brings the real thing; a system about to be entered, or a gate being flown toward, is warmed
	/// (<see cref="WarmSystem"/>) so its assets are already in memory when it is reached. Assets of a system left behind
	/// are let go of (<see cref="ReleaseExcept"/>) once nothing still drawn uses them.
	/// </summary>
	class AssetStreamer
	{
		struct Entry
		{
			cs::Ref<ResourceRequest> Request;
			UnityCompat::ResourceAsset Asset;
			bool Done = false;
			cs::List<cs::Action<const UnityCompat::ResourceAsset&>> Waiting;
			bool HasWaiting = false;
		};

	public:
		/// <summary>Keeps <paramref name="path"/> in memory for good (an asset every system is drawn with), starting it
		/// loading.</summary>
		static void Pin(const cs::String& path);

		/// <summary>How many loads are still on their way.</summary>
		static inline int InFlight = 0;

		/// <summary>Whether <paramref name="path"/> (under Resources) is already in memory, starting it loading if not.</summary>
		static bool Ready(const cs::String& path) { return Get(path)->Done; }

		/// <summary>The asset at <paramref name="path"/> if it is in memory (null while loading, or if there is none),
		/// starting it loading if it is not.</summary>
		template <class T> static cs::Ref<T> Loaded(const cs::String& path) { return As<T>(Get(path)->Asset); }

		/// <summary>Starts <paramref name="path"/> loading in the background, if it is not already.</summary>
		static void Warm(const cs::String& path) { Get(path); }

		/// <summary>
		/// Hands the asset at <paramref name="path"/> to <paramref name="ready"/>: at once if it is in memory, otherwise
		/// on the frame it arrives (with null if there is no such asset). A view torn down meanwhile must check it is
		/// still alive in its callback.
		/// </summary>
		template <class T> static void Request(const cs::String& path, const cs::Action<cs::Ref<T>>& ready)
		{
			auto e = Get(path);
			if (e->Done)
			{
				if (ready) ready(As<T>(e->Asset));
				return;
			}
			if (ready)
			{
				e->HasWaiting = true;
				e->Waiting.Add([ready](const UnityCompat::ResourceAsset& o) { ready(As<T>(o)); });
			}
		}

		/// <summary>The Resources path of a body's colour map and of its relief map (null where it has none).</summary>
		static cs::String TexturePath(const cs::String& name) { return !name.empty() ? "Textures/" + name : cs::String(); }

		/// <summary>The Resources path of a ship model.</summary>
		static cs::String ShipPath(const cs::String& model) { return "Ships/" + model; }

		/// <summary>
		/// Starts everything <paramref name="system"/> is drawn with loading in the background: its bodies' maps and the
		/// models of the ships in it. Called as the view heads for it (a gate flown toward, a trip begun), so it is in
		/// memory by the time it is reached.
		/// </summary>
		static void WarmSystem(const cs::Ref<Core::StarSystem>& system);

		/// <summary>Every asset <paramref name="system"/> is drawn with.</summary>
		static cs::List<cs::String> SystemPaths(const cs::Ref<Core::StarSystem>& system);

		/// <summary>
		/// Lets go of every texture loaded here that is not in <paramref name="keep"/> (the systems still drawn or
		/// warmed): unloaded from memory and loaded again in the background if ever wanted. Models are kept: they are
		/// shared by every system and small.
		/// </summary>
		static void ReleaseExcept(const cs::HashSet<cs::String>& keep);

		/// <summary>A one-pixel texture of <paramref name="color"/>: the stand-in for a map still on its way.</summary>
		static cs::Ref<Texture2D> Swatch(Color color);

	private:
		template <class T> static cs::Ref<T> As(const UnityCompat::ResourceAsset& a)
		{
			if (auto p = std::get_if<cs::Ref<T>>(&a)) return *p;
			return nullptr;
		}

		/// <summary>What sort of asset lives at <paramref name="path"/>: a model under Ships/, otherwise a map.</summary>
		static UnityCompat::ResourceType TypeAt(const cs::String& path);

		static cs::Ref<Entry> Get(const cs::String& path);
		static void Complete(const cs::String& path, const cs::Ref<Entry>& e);
		static cs::Dictionary<cs::String, cs::Ref<Entry>>& Entries();
		static cs::HashSet<cs::String>& Pinned();
	};
}
