#pragma once

// The host of the Unity-style GUI (UnityGui.h) inside Unreal: what Unity's engine does around every MonoBehaviour's
// OnGUI. A full-screen, hit-test-invisible Slate widget over the game viewport runs, at paint time each frame,
// Unity's event loop over the registered OnGUI handlers (a Layout pass before every event; one pass per input event
// that came in since the last frame; then Layout + Repaint) and draws the Repaint pass with Slate: boxes (9-slice
// for a style's border), textures, text in Roboto at Unity's pixel em, clip zones for groups / scroll views / clipped
// text, render transforms for GUI.matrix. Input comes from a Slate input pre-processor that never consumes anything
// (the game reads the same keys and decides with UnitySpaceApp.UiRects whether the pointer is over the UI, as the
// original does), and that also feeds UI::UiInput.
//
// Usage (the owners of OnGUI: UnitySpaceApp's actor, MainMenu's host, ModelViewer, AsteroidBelt):
//     GuiHost::Register(this, [this] { OnGUI(); });   // once, e.g. after Awake
//     GuiHost::Unregister(this);                       // in OnDestroy
// The widget is (re)installed on the game viewport automatically (a map load clears viewport widgets).

#include "UnityCompat/UnityGui.h"

#include <functional>

namespace SpaceGod::UnityCompat
{
	struct GuiHost
	{
		/// Adds an OnGUI handler; lower `order` runs first in each pass (Unity: GUI.depth / script order). Re-registering
		/// an owner replaces its handler.
		static void Register(const void* owner, std::function<void()> onGui, int order = 0);
		static void Unregister(const void* owner);

		/// Releases an Unreal texture made from a Texture2D's pixels (Texture2D's destructor calls it).
		static void ReleaseNativeTexture(void* native);

		/// Removes the overlay and the input processor (module shutdown).
		static void Shutdown();
	};
}

namespace SpaceGod
{
	using UnityCompat::GuiHost;
}
