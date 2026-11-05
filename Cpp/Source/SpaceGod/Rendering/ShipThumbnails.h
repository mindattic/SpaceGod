#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/ShipThumbnails.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	class Faction;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Renders a small offscreen snapshot of each <see cref="ShipClass"/>'s hull -- the same model a
	/// real ship uses (<see cref="ShipModels.Build"/>), just a bare throwaway <see cref="Ship"/> never
	/// added to any <see cref="Fleet"/> -- for the spawn screen's picker grid (<see cref="UI.Hud"/>).
	/// Renders straight to a render target via a manual scene capture (Unity's Camera.Render) rather than
	/// an enabled camera, so it never competes with or needs to disable the game's own camera (unlike
	/// <see cref="ModelViewer"/>, whose camera draws to the screen itself). Cached per class and livery:
	/// each one is built and rendered once, not every frame.
	/// </summary>
	/// <remarks>Unreal: the capture shows only the preview's components (Unity's ModelViewer layer); the texture is a
	/// UTextureRenderTarget2D wrapped as a Texture2D (its Native). US-T116: drawn a few frames after it is first asked for
	/// (the texture shows the backdrop until then), since a capture in the frame its preview is built draws it black.</remarks>
	class ShipThumbnails
	{
	public:
		/// <summary>A cached snapshot of <paramref name="cls"/>'s hull in <paramref name="faction"/>'s livery, rendering it the first time asked.</summary>
		static cs::Ref<Texture> For(Core::ShipClass cls, const cs::Ref<Core::Faction>& faction);

		/// <summary>Drops every cached snapshot (e.g. if a faction's livery colour could change later).</summary>
		static void Clear();

		/// <summary>US-T116: whether snapshots asked for are still on their way (they take a few frames).</summary>
		static bool Pending();

		/// <summary>US-T116: the thumbnails of <paramref name="classes"/> side by side, <paramref name="columns"/> to a row,
		/// saved to the image <paramref name="file"/> (the lineup's thumbnails sheet). Asks for any not made yet and returns
		/// false until they are all drawn; then saves and returns true.</summary>
		static bool SaveSheet(const cs::List<Core::ShipClass>& classes, const cs::Ref<Core::Faction>& faction, const FString& file, int columns);

	private:
		static constexpr int Size = 192;
		static cs::Ref<Texture> Render(Core::ShipClass cls, const cs::Ref<Core::Faction>& faction);
	};
}
