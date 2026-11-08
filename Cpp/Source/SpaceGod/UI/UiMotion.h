#pragma once

// UiMotion (US-T255): the one transition layer every menu, screen and HUD panel moves with, so nothing snaps on.
//
//   - Panels:  a panel's draw opens a Scope with its id; the first frame it is drawn (or the first after a gap) starts
//              its clock, and for PanelSeconds it eases in: faded (GUI::fade) and risen PanelSlide px into place
//              (GUI::matrix). A page change of a menu is a new id, so the new page fades in as well.
//   - Toasts:  Enter(age) is the same ease for anything that carries its own age (a toast, a news report).
//   - Buttons: GUI::hoverSeconds eases a button's hover background in and out (UnityGui's DrawControl).
//   - Scenes:  FadeOutThen(load) fades the screen to black, runs the load, and holds black until the next scene has
//              arrived (SceneArrived, from Bootstrap); then it fades back in. FadeFromBlack is a cut that fades in
//              (a quickload, whose load is in place).
//
// Every clock is real time (FPlatformTime), so a paused game's menus still move. Nothing moves when the profile asks
// for reduced motion (CommonSettings::ReduceMotion), nor in a scripted run (the smoke, the UI shots, the views, a
// capture or the demo tour, a headless run), so screenshot audits always capture settled frames.

#include "Cs/Cs.h"

#include <functional>

#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::UI::UiMotion
{
	/// How long a panel takes to ease in, and how far it rises as it does.
	inline constexpr float PanelSeconds = 0.18f;
	inline constexpr float PanelSlide = 10.0f;
	/// A toast's (or a fresh news report's) ease in.
	inline constexpr float ToastSeconds = 0.25f;
	inline constexpr float ToastSlide = 12.0f;
	/// A button lighting up under the pointer.
	inline constexpr float HoverSeconds = 0.12f;
	/// Each half of a scene change's fade through black.
	inline constexpr float SceneSeconds = 0.35f;

	/// Whether anything moves this frame (off under reduced motion and in scripted runs).
	bool Enabled();

	/// Real seconds, the clock every transition here runs on.
	double Now();

	/// Ease-out cubic of t clamped to 0..1.
	float EaseOut(float t);

	/// The eased 0..1 of a panel `id` drawn this frame: 0 the first frame it is drawn (or after it was not drawn for a
	/// couple of frames), 1 once `seconds` have passed; always 1 when motion is off.
	float Appear(const char* id, float seconds = PanelSeconds);

	/// The eased 0..1 of something `ageSeconds` old (a toast): 1 when motion is off.
	float Enter(float ageSeconds, float seconds = ToastSeconds);

	/// While alive, everything drawn is faded by `t` and risen into place by (1 - t) * slide px.
	class Scope
	{
	public:
		/// A panel's own transition: Appear(id).
		explicit Scope(const char* id, float slide = PanelSlide);
		/// Any eased 0..1 (Enter, or an Appear already asked).
		Scope(float t, float slide);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		void Apply(float t, float slide);
		float oldFade = 1;
		GuiMatrix oldMatrix;
	};

	/// Fades the screen to black, then runs `load` (a scene change), holding black until SceneArrived. Runs it at once
	/// when motion is off; a second call while one is under way is dropped (one trip at a time).
	void FadeOutThen(std::function<void()> load);

	/// The screen cut to black, fading back in (a load done in place, as a quickload).
	void FadeFromBlack();

	/// A scene has loaded (Bootstrap): installs the overlay that draws the fades and, if a fade is holding black for
	/// this scene, starts it back in.
	void SceneArrived();
}
