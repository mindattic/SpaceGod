// US-T252: the player's own options (PlayerOptions.h).

#include "PlayerOptions.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "MindAtticHeadless.h"
#include "Input/KeyBindings.h" // US-T249
#include "Settings/GameSetting.h"
#include "UI/Hud.h"
#include "UnityCompat/UnityGui.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		cs::Func<cs::String, double> Percent()
		{
			return cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); });
		}

		/// The stance palette into the HUD's own colours (its markers, edge arrows, target card and roster read them).
		void PaintHud()
		{
			UI::Hud::SetStanceColours(PlayerOptions::HostileColor(0.95f), PlayerOptions::FriendlyColor(0.95f), PlayerOptions::NeutralColor(0.9f));
		}
	}

	const cs::List<cs::String>& PlayerOptions::PlayerGroups()
	{
		// US-T249: Key bindings right under Controls (the nav indents it there).
		static const cs::List<cs::String> value{ Gameplay, SettingGroups::Graphics, SettingGroups::Audio, SettingGroups::Controls, Input::KeyBindings::Group, Accessibility };
		return value;
	}

	bool PlayerOptions::IsPlayerGroup(const cs::String& group) { return PlayerGroups().Contains(group); }

	// ---------------------------------------------------------------- Gameplay

	const cs::Ref<ChoiceSetting>& PlayerOptions::AutosaveInterval()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.autosaveInterval", Gameplay, "Autosave",
			cs::List<cs::String>{ "Off", "Every 2 minutes", "Every 5 minutes", "Every 10 minutes", "Every 15 minutes", "Every 30 minutes" }, 2,
			"How often the game saves itself while you play (it always saves on quitting and on the way back to the main menu).",
			"save autosave interval minutes");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::DistanceUnits()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.distanceUnits", Gameplay, "Distances",
			cs::List<cs::String>{ "km, then AU", "Always km", "AU when far" }, 0,
			"How distances are written: km up close and AU across a system, km everywhere, or AU from a tenth of the Moon's "
			"distance out (a fight's ranges stay in km).",
			"units km kilometres kilometers au astronomical metric");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::Notices()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("gameplay.notices", Gameplay, "Toasts and news",
			cs::List<cs::String>{ "Everything", "Important only", "Quiet" }, 0,
			"Everything; Important only keeps battles, captures and diplomacy on the news ticker and drops the autosave notice; "
			"Quiet hides the ticker too (the news screen keeps every report).",
			"notifications toast ticker news verbosity messages spam");
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::ConfirmQuit()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("gameplay.confirmQuit", Gameplay, "Confirm before quitting", true,
			"The pause menu's Main Menu and Quit ask before they leave the game.", "quit exit leave confirm ask");
		return value;
	}

	// ---------------------------------------------------------------- Accessibility

	const cs::Ref<RangeSetting>& PlayerOptions::UiScale()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.uiScale", Accessibility, "UI scale", 100, 75, 200, 5,
			"Every screen, menu and the HUD drawn larger or smaller.", Percent(), "interface size zoom big small hud menu dpi")
			->Applying([](double v) { GUI::uiScale = ScriptedRun() ? 1.0f : static_cast<float>(v / 100.0); });
		return value;
	}

	const cs::Ref<RangeSetting>& PlayerOptions::TextSize()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.textSize", Accessibility, "Text size", 100, 80, 150, 5,
			"Every piece of text drawn larger or smaller, on top of the UI scale.", Percent(), "font size big large readable")
			->Applying([](double v) { GUI::textScale = ScriptedRun() ? 1.0f : static_cast<float>(v / 100.0); });
		return value;
	}

	const cs::Ref<RangeSetting>& PlayerOptions::HudOpacity()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("access.hudOpacity", Accessibility, "HUD opacity", 100, 30, 100, 5,
			"How solid the HUD's overlays are drawn over the view (full screens and menus stay solid).", Percent(),
			"transparency transparent alpha fade overlay");
		return value;
	}

	const cs::Ref<ChoiceSetting>& PlayerOptions::StancePalette()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("access.stancePalette", Accessibility, "Stance colours",
			cs::List<cs::String>{ "Red, grey, green", "Colour-blind safe" }, 0,
			"Hostile, neutral and friendly in comms, markers, edge arrows and the roster: red, grey and green, or orange, grey and blue.",
			"colourblind colorblind color colour deuteranopia protanopia palette hostile friendly")
			->Applying([](int) { PaintHud(); });
		return value;
	}

	const cs::Ref<ToggleSetting>& PlayerOptions::ReduceFlashes()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("access.reduceFlashes", Accessibility, "Reduce flashes and shake", false,
			"Gate and drive flashes, damage pulses at the screen's edge and the hyperspace toll's heartbeat kept faint and still, "
			"and menus no longer slide in.", "photosensitive epilepsy flash strobe motion shake vignette pulse");
		return value;
	}

	void PlayerOptions::Declare()
	{
		AutosaveInterval(); DistanceUnits(); Notices(); ConfirmQuit();
		UiScale(); TextSize(); HudOpacity(); StancePalette(); ReduceFlashes();
	}

	// ---------------------------------------------------------------- reads

	float PlayerOptions::AutosaveSeconds()
	{
		static const float minutes[] = { 0, 2, 5, 10, 15, 30 };
		int i = AutosaveInterval()->Index;
		return i >= 0 && i < 6 ? minutes[i] * 60.0f : 300.0f;
	}

	PlayerOptions::Units PlayerOptions::DistanceMode() { return static_cast<Units>(DistanceUnits()->Index); }

	bool PlayerOptions::WriteAu(double km, double autoFromKm)
	{
		switch (DistanceMode())
		{
		case Units::Kilometres: return false;
		case Units::Au: return km >= 38440.0; // a tenth of the Moon's distance: past any fight's ranges
		default: return km >= autoFromKm;
		}
	}

	cs::String PlayerOptions::KmText(double km)
	{
		if (km >= 1e9) return cs::Cat(cs::Fmt(km / 1e9, "0.00"), " billion km");
		if (km >= 1e7) return cs::Cat(cs::Fmt(km / 1e6, "0.0"), " million km");
		return cs::Cat(cs::Fmt(km, "N0"), " km");
	}

	PlayerOptions::NoticeLevel PlayerOptions::NoticeMode() { return static_cast<NoticeLevel>(Notices()->Index); }

	float PlayerOptions::HudAlpha() { return cs::Math::Max(0.3f, cs::Math::Min(1.0f, static_cast<float>(HudOpacity()->Value / 100.0))); }

	bool PlayerOptions::ColourBlindStances() { return StancePalette()->Index == 1; }

	// Colour-blind safe: orange against sky blue (Okabe-Ito's, lifted for a dark screen), told apart under every common
	// form of colour blindness; neutral stays grey.
	Color PlayerOptions::HostileColor(float alpha) { return ColourBlindStances() ? Color(1.0f, 0.55f, 0.1f, alpha) : Color(1.0f, 0.4f, 0.35f, alpha); }

	Color PlayerOptions::FriendlyColor(float alpha) { return ColourBlindStances() ? Color(0.35f, 0.72f, 1.0f, alpha) : Color(0.45f, 0.95f, 0.6f, alpha); }

	Color PlayerOptions::NeutralColor(float alpha) { return ColourBlindStances() ? Color(0.86f, 0.86f, 0.86f, alpha) : Color(0.85f, 0.85f, 0.8f, alpha); }

	Color PlayerOptions::WaryColor(float alpha) { return ColourBlindStances() ? Color(0.95f, 0.9f, 0.35f, alpha) : Color(1.0f, 0.78f, 0.35f, alpha); }

	bool PlayerOptions::Calm() { return ReduceFlashes()->Value; }

	float PlayerOptions::FlashScale() { return Calm() ? 0.2f : 1.0f; }

	bool PlayerOptions::ScriptedRun()
	{
		static const bool value = []
		{
			if (FMindAtticHeadless::IsActive()) return true;
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false))
				if (token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase)
					|| token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
					return true;
			return false;
		}();
		return value;
	}
}
