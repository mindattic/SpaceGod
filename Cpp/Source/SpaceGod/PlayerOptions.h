#pragma once
// US-T252: the player's own options, as against the design tunables the game's systems declare. Two settings groups of
// their own -- Gameplay (autosave interval, distance units, how chatty toasts and the news ticker are, a confirmation
// before leaving) and Accessibility (UI scale, text size, HUD opacity, a colour-blind safe stance palette, fewer screen
// flashes) -- declared here as every setting is (GameSettingsHost registers this source), kept per profile like any
// other, and read by the places they touch through the helpers below. The settings menu (UI/PauseMenu) lists the player
// groups first (PlayerGroups) and folds the rest under "Advanced (simulation)".

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class ToggleSetting;
	class RangeSetting;
}

namespace SpaceGod
{
	class PlayerOptions
	{
	public:
		/// The two groups of player options (Core's SettingGroups names the design ones).
		static constexpr const char* Gameplay = "Gameplay";
		static constexpr const char* Accessibility = "Accessibility";

		/// The groups the settings menu lists first, in its order (its own Display page goes after Graphics); every other
		/// group is design tuning, folded under "Advanced (simulation)".
		static const cs::List<cs::String>& PlayerGroups();
		static bool IsPlayerGroup(const cs::String& group);

		// ---------------------------------------------------------------- Gameplay

		/// Off, or 2 / 5 / 10 / 15 / 30 real minutes between autosaves (5 before the player changes it).
		static const cs::Ref<Core::ChoiceSetting>& AutosaveInterval();
		/// How long distances are written: km then AU as the readout sees fit, always km, or AU wherever it is not a fight's range.
		static const cs::Ref<Core::ChoiceSetting>& DistanceUnits();
		/// Toasts and the news ticker: everything, important only, or quiet.
		static const cs::Ref<Core::ChoiceSetting>& Notices();
		/// The pause menu's Main Menu and Quit ask first.
		static const cs::Ref<Core::ToggleSetting>& ConfirmQuit();

		// ---------------------------------------------------------------- Accessibility

		/// Every IMGUI screen and the HUD drawn this much larger (75% to 200%).
		static const cs::Ref<Core::RangeSetting>& UiScale();
		/// Every font this much larger (80% to 150%), on top of the UI scale.
		static const cs::Ref<Core::RangeSetting>& TextSize();
		/// The HUD's overlays drawn at this opacity (30% to 100%); full screens and menus stay solid.
		static const cs::Ref<Core::RangeSetting>& HudOpacity();
		/// Hostile, neutral and friendly in red / grey / green, or a colour-blind safe orange / grey / blue.
		static const cs::Ref<Core::ChoiceSetting>& StancePalette();
		/// Screen washes, edge pulses and the hyperspace toll's heartbeat kept faint and still; transitions do not move.
		static const cs::Ref<Core::ToggleSetting>& ReduceFlashes();

		/// Builds every one of these settings (the declaration source GameSettingsHost registers).
		static void Declare();

		// ---------------------------------------------------------------- reads

		/// Real seconds between autosaves; 0 when they are off.
		static float AutosaveSeconds();

		enum class Units : int { Auto = 0, Kilometres = 1, Au = 2 };
		static Units DistanceMode();
		/// Whether a readout that would switch to AU at <paramref name="autoFromKm"/> writes <paramref name="km"/> in AU.
		static bool WriteAu(double km, double autoFromKm);
		/// A distance in km with no AU: whole km, "12.3 million km" from ten million, "1.23 billion km" from a billion.
		static cs::String KmText(double km);

		enum class NoticeLevel : int { Everything = 0, Important = 1, Quiet = 2 };
		static NoticeLevel NoticeMode();

		/// The HUD's opacity, 0.3 to 1.
		static float HudAlpha();

		/// The stance palette's colours (the HUD's markers, arrows and roster; comms).
		static bool ColourBlindStances();
		static Color HostileColor(float alpha = 1);
		static Color FriendlyColor(float alpha = 1);
		static Color NeutralColor(float alpha = 1);
		/// Comms' "wary" between neutral and hostile.
		static Color WaryColor(float alpha = 1);

		/// Reduce flashes is on.
		static bool Calm();
		/// What a flash or screen wash is multiplied by: 1, or a faint share of it when Reduce flashes is on.
		static float FlashScale();

		/// A run that is driven and photographed (the smoke, shots, audits, the demo, captures, headless) draws at 100% scale and
		/// text whatever the profile says, so every shot is alike.
		static bool ScriptedRun();
	};
}
