#pragma once

// CreditsScreen (US-T246): the rolling credits, opened from the main menu's Credits and the pause menu's.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::UI
{
	/// The Credits screen (US-T246): the whole screen, the game's credits (Core::Credits: the team, the original game, the
	/// engine, every third-party pack, sky, map, data set and library with its licence) rolling slowly up a centred column,
	/// from below the screen to past its top, then round again. The mouse wheel, a drag, Up / Down, Page Up / Page Down and
	/// Home / End read at the player's own pace (the roll waits a few seconds after, then carries on from there); Space
	/// pauses and resumes it. Back (or the owner's Esc) leaves.
	class CreditsScreen final
	{
	public:
		/// Draws the screen; true once the player has clicked Back (Esc is the owner's to handle).
		bool Draw(const cs::String& closeHint);

		/// Back to the top of the roll, rolling: the owner calls it as it opens the screen.
		void Restart();

		/// The screen's own textures go with whatever owns it (U.SPC-T366): DontSave outlives the scene otherwise.
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, ColumnW = 720, SectionGap = 46, EntryGap = 16, RollSpeed = 42, ResumeAfter = 4;

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, gameTitle, tagline, section, name, detail, licence, small, button;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture;

		/// How far the roll has gone (content pixels), the real time it last moved, and until when it waits for the player
		/// (after a scroll of theirs); paused by Space.
		float roll = 0;
		float lastTime = -1;
		float waitUntil = 0;
		bool paused = false;

		/// The column's laid-out height at the width it was measured for (measured again when the screen's width changes).
		float contentH = 0, measuredW = -1;

		void HandleKeys(float viewH, float maxRoll);
		float Measure(float w);
		float DrawColumn(float w, bool draw);
		void EnsureStyles();
	};
}
