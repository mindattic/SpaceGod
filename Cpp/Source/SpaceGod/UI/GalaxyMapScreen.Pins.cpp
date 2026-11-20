// US-T377: the galaxy map's pins. Up to nine systems pinned by slot: Ctrl+1..9 pins the system picked to that slot (again on
// the same system: unpins it), a plain 1..9 picks the pinned system and centres the map on it; a small panel at the map's
// bottom left lists them, each row a click to go there. Kept by name in the profile (galaxyMapPins), not the game save, so
// they outlive a load and nothing of them is sent in co-op or touches the simulation. Declarations in GalaxyMapScreen.h.

#include "UI/GalaxyMapScreen.h"

#include "ActiveProfile.h"
#include "UnitySpaceApp.h"
#include "Camera/StarTravel.h"
#include "Input/InputDevice.h"
#include "Persistence/Profiles.h"
#include "Systems/Galaxy.h"

#include <exception>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void GalaxyMapScreen::LoadPins()
	{
		pinsLoaded = true;
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile == nullptr) return;
			cs::String saved = GameSettings::Setting(profile, PinsSetting);
			// The slots in order, '|' between them, a free slot empty.
			int slot = 0;
			cs::String name;
			for (char c : saved)
			{
				if (c == '|')
				{
					if (slot < PinSlots) pins[slot] = name;
					slot++;
					name.clear();
				}
				else name += c;
			}
			if (slot < PinSlots) pins[slot] = name;
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Reading the galaxy map's pins failed: ", e.what())); }
	}

	void GalaxyMapScreen::SavePins()
	{
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile == nullptr) return;
			cs::String joined;
			for (int i = 0; i < PinSlots; i++)
			{
				if (i > 0) joined += '|';
				joined += pins[i];
			}
			GameSettings::SetSetting(profile, PinsSetting, joined);
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Saving the galaxy map's pins failed: ", e.what())); }
	}

	void GalaxyMapScreen::Pin(int slot)
	{
		if (!pinsLoaded) LoadPins();
		if (selected == nullptr || selectedStar.has_value())
		{
			app->ShowToast("Pick a star system to pin (a plain star on the charts cannot be pinned)");
			return;
		}
		cs::String name = selected->Name;
		if (pins[slot] == name)
		{
			pins[slot].clear();
			SavePins();
			app->ShowToast(cs::Cat("Unpinned ", name, " from ", slot + 1));
			return;
		}
		// One slot a system: pinning it again elsewhere moves it.
		for (int i = 0; i < PinSlots; i++)
			if (i != slot && pins[i] == name) pins[i].clear();
		pins[slot] = name;
		SavePins();
		app->ShowToast(cs::Cat("Pinned ", name, " to ", slot + 1, " (", slot + 1, " goes there on the galaxy map)"));
	}

	void GalaxyMapScreen::JumpToPin(int slot)
	{
		if (!pinsLoaded) LoadPins();
		if (pins[slot].empty())
		{
			app->ShowToast(cs::Cat("Nothing pinned to ", slot + 1, ": Ctrl+", slot + 1, " pins the system picked"));
			return;
		}
		auto system = app->Galaxy != nullptr ? app->Galaxy->Find(pins[slot]) : nullptr;
		if (system == nullptr)
		{
			app->ShowToast(cs::Cat(pins[slot], " is not in this galaxy (pinned in another game)"));
			return;
		}
		selected = system;
		selectedStar.reset();
		centreLy = system->PositionLy;
		pan = Vector2::zero();
	}

	bool GalaxyMapScreen::PinKeys(bool keysFree)
	{
		if (!keysFree || !app->ShowGalaxyMap || app->Travel->Active || finding) return false; // US-T402: not under comms or the dev menu
		using Key = ::SpaceGod::Input::Key;
		const ::SpaceGod::Input::InputDevice& kb = ::SpaceGod::Input::InputDevice::current();
		auto held = [&](Key a, Key b) { return kb[a].isPressed || kb[b].isPressed; };
		bool ctrl = held(Key::LeftCtrl, Key::RightCtrl);
		// Shift+digit is a helm ability (US-T332) and Alt+digit a weapon group: theirs, as before.
		if (held(Key::LeftShift, Key::RightShift) || held(Key::LeftAlt, Key::RightAlt)) return false;
		static constexpr Key digits[PinSlots] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
			Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9 };
		for (int i = 0; i < PinSlots; i++)
		{
			if (!kb[digits[i]].wasPressedThisFrame) continue;
			if (ctrl) Pin(i);
			else JumpToPin(i);
			return true;
		}
		// Ctrl held over the map: neither a mine dropped nor the craft slowed under it on the way to a Ctrl+digit (or Ctrl+F).
		return ctrl;
	}

	void GalaxyMapScreen::CentreOnHere()
	{
		// US-T393: where you are, in the middle (the zoom kept), as the system map's HERE does.
		auto here = app->ActiveSystem;
		if (here == nullptr)
		{
			app->ShowToast("No system around you to centre on");
			return;
		}
		centreLy = here->PositionLy;
		pan = Vector2::zero();
	}

	bool GalaxyMapScreen::HereKey(bool keysFree)
	{
		if (!keysFree || !app->ShowGalaxyMap || app->Travel->Active || finding) return false; // US-T402
		using Key = ::SpaceGod::Input::Key;
		const ::SpaceGod::Input::InputDevice& kb = ::SpaceGod::Input::InputDevice::current();
		if (!kb[Key::Home].wasPressedThisFrame) return false;
		auto held = [&](Key a, Key b) { return kb[a].isPressed || kb[b].isPressed; };
		if (held(Key::LeftShift, Key::RightShift) || held(Key::LeftCtrl, Key::RightCtrl) || held(Key::LeftAlt, Key::RightAlt)) return false;
		CentreOnHere();
		return true;
	}

	void GalaxyMapScreen::DrawPins(Rect canvas)
	{
		if (!pinsLoaded) LoadPins();
		int count = 0;
		for (const auto& p : pins)
			if (!p.empty()) count++;
		float w = Mathf::Min(240.0f, canvas.width - 16);
		float h = 26 + PinRowH * static_cast<float>(count == 0 ? 1 : count) + 4;
		pinsRect = Rect(canvas.x + 8, canvas.yMax() - 8 - h, w, h);
		GUI::Box(pinsRect, GUIContent::none(), Panel());
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(pinsRect.x + 10, pinsRect.y + 4, w - 20, 18), "PINS  \xC2\xB7  Ctrl+1..9 pins, 1..9 goes", small);
		float y = pinsRect.y + 26;
		if (count == 0)
		{
			GUI::Label(Rect(pinsRect.x + 10, y, w - 20, 18), "none yet: pick a system, then Ctrl+1", small);
			GUI::contentColor = old;
			return;
		}
		GUI::contentColor = old;
		for (int i = 0; i < PinSlots; i++)
		{
			if (pins[i].empty()) continue;
			bool here = selected != nullptr && !selectedStar.has_value() && selected->Name == pins[i];
			if (GUI::Button(Rect(pinsRect.x + 4, y, w - 8, PinRowH - 2), cs::Cat(i + 1, "   ", pins[i]), here ? buttonOn : button))
				JumpToPin(i);
			y += PinRowH;
		}
	}
}
