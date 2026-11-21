// US-T441: the HUD's layout presets (declarations in UI/Hud.Drawer.inl, the player's view in docs/HUD.md, "Layouts").
// A preset is a whole drawer layout (HudDrawers::Save's JSON) under a name: five built in (Default, the layout the HUD starts
// with; Combat, Commerce, Piracy and Exploration, each a recipe on it) and the player's own, saved, renamed and deleted from
// the Layouts drawer; a built-in can be saved over (a copy of it the player keeps) and reset. They are kept per profile in
// settings.json's game block (game.hudLayouts, as the galaxy map's pins are: GalaxyMapScreen.Pins.cpp), so they outlive a
// load and nothing of them is in a save or sent in co-op. Layout 1-5 (rebindable, unbound to begin with) switch to the
// preset in each slot; the option "switch layout with the situation" (Gameplay, off to begin with) shows Combat in a fight,
// Commerce docked and the player's chosen preset otherwise.

#include "UI/Hud.h"

#include "ActiveProfile.h"
#include "PlayerOptions.h"
#include "UnitySpaceApp.h"
#include "Input/KeyBindings.h"
#include "UI/UiStyle.h"

#include "Ships.h"
#include "Combat/Battle.h"
#include "Settings/GameSetting.h"
#include "Shared/Json/JsonObject.h"
#include "Shared/Profiles/ProfileSettings.h"
#include "Shared/Profiles/ProfileStore.h"

#include <algorithm>
#include <cstdlib>
#include <exception>

using namespace SpaceGod::Core;
using SpaceGod::Shared::Json::JsonArray;
using SpaceGod::Shared::Json::JsonObject;
using SpaceGod::Shared::Json::JsonValue;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr const char* BlockKey = "hudLayouts";
		constexpr const char* DefaultName = "Default";
		constexpr int MaxNameLength = 24;
		constexpr const char* NameControl = "hud.layouts.name";
		/// How long a situation must hold before the layout follows it (no flicker as a fight blinks in and out).
		constexpr float SituationSettleS = 1.0f;
	}

	// ---------------------------------------------------------------- the presets

	const std::vector<cs::String>& Hud::BuiltinLayouts()
	{
		static const std::vector<cs::String> names = { DefaultName, "Combat", "Commerce", "Piracy", "Exploration" };
		return names;
	}

	bool Hud::IsBuiltinLayout(const cs::String& name)
	{
		const auto& b = BuiltinLayouts();
		return std::find(b.begin(), b.end(), name) != b.end();
	}

	cs::String Hud::CleanLayoutName(const cs::String& name)
	{
		cs::String t = cs::Strings::Trim(name);
		cs::String out;
		for (char c : t)
			if (static_cast<unsigned char>(c) >= 0x20) out += c;
		if (out.size() > static_cast<size_t>(MaxNameLength)) out = out.substr(0, MaxNameLength);
		return cs::Strings::Trim(out);
	}

	cs::Ref<JsonObject> Hud::LayoutsBlock()
	{
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile == nullptr) return nullptr;
			return profile->Settings()->Game()->GetOrAddObject(BlockKey);
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[SpaceGod] Reading the HUD's layouts failed: ", e.what()));
			return nullptr;
		}
	}

	void Hud::SaveLayoutsBlock()
	{
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile != nullptr && !profile->TrySaveSettings()) Debug::LogError("[SpaceGod] Saving the HUD's layouts failed (the disk refused)");
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Saving the HUD's layouts failed: ", e.what())); }
	}

	std::vector<cs::String> Hud::LayoutNames()
	{
		std::vector<cs::String> names = BuiltinLayouts();
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		if (presets != nullptr)
			for (const auto& k : presets->Keys())
				if (!IsBuiltinLayout(k) && presets->GetObject(k) != nullptr) names.push_back(k);
		return names;
	}

	cs::String Hud::ActiveLayout()
	{
		auto block = LayoutsBlock();
		cs::String name = block != nullptr ? block->GetString("active") : cs::String();
		auto names = LayoutNames();
		return std::find(names.begin(), names.end(), name) != names.end() ? name : cs::String(DefaultName);
	}

	bool Hud::LayoutOverridden(const cs::String& name)
	{
		if (!IsBuiltinLayout(name)) return false;
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		return presets != nullptr && presets->GetObject(name) != nullptr;
	}

	void Hud::ApplyBuiltinRecipe(const cs::String& name)
	{
		using HudDock::Edge;
		auto pin = [this](std::initializer_list<const char*> ids)
		{
			for (const char* id : ids)
			{
				drawers.Open(id);
				drawers.SetPinned(id, true);
			}
		};
		auto shut = [this](std::initializer_list<const char*> ids)
		{
			for (const char* id : ids) drawers.Close(id);
		};
		// One of the bottom drawer's four torn off its window of tabs and docked on the left edge, under the navigator's
		// windows, pulled out and pinned.
		auto toLeft = [this](const char* id, float h)
		{
			drawers.Detach(id, Rect(40, 120, 330, h));
			drawers.Dock(id, Edge::Left, 2);
			drawers.SetSize(id, Vector2(330, h));
			drawers.Open(id);
			drawers.SetPinned(id, true);
		};
		if (name == "Combat")
		{
			// The fight's: the reactor, the sensors, the target, the helm's abilities and the seat, the hunters, the status
			// line; what has no part in a fight closed to its tab.
			pin({ DrawerReactorId, DrawerSensorsId, DrawerTargetId, DrawerHelmId, DrawerSeatId, DrawerHuntersId, DrawerStatusId });
			shut({ DrawerOpportunitiesId, DrawerTickerId, DrawerInfluenceId, DrawerHarvestId });
		}
		else if (name == "Commerce")
		{
			// Trade's: the clock (a market moves with time), the navigator, the opportunities, the selection card (a port's
			// goods), the gas works and the status line. The market, the cargo hold, the contracts and the trade desk open by
			// their keys, where they always open.
			pin({ DrawerTopBarId, DrawerNavigatorId, DrawerOpportunitiesId, DrawerSelectionId, DrawerHarvestId, DrawerStatusId });
			shut({ DrawerHuntersId, DrawerTickerId });
		}
		else if (name == "Piracy")
		{
			// The hunt's: the sensors, the target, the selection card, the hunters on your trail, the status line, and the
			// ships in the area (the prey) down the left edge; the bounty board and comms open by their keys.
			pin({ DrawerSensorsId, DrawerTargetId, DrawerSelectionId, DrawerHuntersId, DrawerStatusId });
			shut({ DrawerOpportunitiesId, DrawerHarvestId, DrawerInfluenceId, DrawerTickerId });
			toLeft(DrawerShipsId, 300);
		}
		else if (name == "Exploration")
		{
			// Travel's: the clock and scale, the navigator, the selection card (what a world is), the sensors, the status line,
			// and the Navigation list (every system and place, its buttons the system map, the encyclopedia and society) down
			// the left edge.
			pin({ DrawerTopBarId, DrawerNavigatorId, DrawerSelectionId, DrawerSensorsId, DrawerStatusId });
			shut({ DrawerHuntersId, DrawerOpportunitiesId, DrawerHarvestId, DrawerInfluenceId }); // influence: the left edge's room for the list
			toLeft(DrawerNavigationId, 360);
		}
	}

	bool Hud::ApplyLayout(const cs::String& name, bool makeActive)
	{
		RegisterDrawers();
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		auto saved = presets != nullptr ? presets->GetObject(name) : nullptr;
		if (saved == nullptr && !IsBuiltinLayout(name)) return false;
		// What a key opened (and the Layouts drawer, which the player is using) keeps whether it is open.
		struct Kept { cs::String id; bool shown; };
		std::vector<Kept> kept;
		for (const auto& s : drawers.Specs())
			if (s.keyed || s.id == DrawerLayoutsId) kept.push_back({ s.id, drawers.Shown(s.id) });
		bool loaded = false;
		if (saved != nullptr) loaded = drawers.Load(saved);
		if (!loaded)
		{
			drawers.ResetLayout();
			ApplyBuiltinRecipe(name);
		}
		for (const auto& k : kept)
		{
			if (drawers.Shown(k.id) == k.shown) continue;
			if (k.shown) drawers.Open(k.id);
			else
			{
				// Closed only when its window holds nothing that is not kept (else its window stays as the preset has it).
				int w = drawers.WindowOf(k.id);
				bool alone = true;
				if (w >= 0)
					for (const auto& m : drawers.Windows()[w].members)
						if (std::none_of(kept.begin(), kept.end(), [&](const Kept& o) { return o.id == m; })) alone = false;
				if (alone) drawers.Close(k.id);
			}
		}
		layoutShown = name;
		if (makeActive && block != nullptr)
		{
			block->Set("active", name);
			SaveLayoutsBlock();
		}
		return true;
	}

	bool Hud::SaveLayout(const cs::String& raw)
	{
		cs::String name = CleanLayoutName(raw);
		auto block = LayoutsBlock();
		if (name.empty() || block == nullptr) return false;
		RegisterDrawers();
		block->GetOrAddObject("presets")->Set(name, drawers.Save());
		block->Set("active", name);
		layoutShown = name;
		SaveLayoutsBlock();
		return true;
	}

	bool Hud::RenameLayout(const cs::String& from, const cs::String& raw)
	{
		cs::String to = CleanLayoutName(raw);
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		if (to.empty() || presets == nullptr || to == from || IsBuiltinLayout(to) || presets->GetObject(to) != nullptr) return false;
		auto moved = presets->GetObject(from);
		if (moved == nullptr) return false;
		presets->Remove(from);
		presets->Set(to, moved);
		if (block->GetString("active") == from) block->Set("active", to);
		if (layoutShown == from) layoutShown = to;
		if (auto slots = block->GetArray("slots"))
			for (int i = 0; i < slots->Count(); i++)
				if (auto s = (*slots)[i].AsString(); s != nullptr && *s == from) (*slots)[i] = JsonValue(to);
		SaveLayoutsBlock();
		return true;
	}

	bool Hud::DeleteLayout(const cs::String& name)
	{
		if (IsBuiltinLayout(name)) return false;
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		if (presets == nullptr || !presets->Remove(name)) return false;
		if (block->GetString("active") == name) block->Set("active", DefaultName);
		if (auto slots = block->GetArray("slots"))
			for (int i = 0; i < slots->Count(); i++)
				if (auto s = (*slots)[i].AsString(); s != nullptr && *s == name) (*slots)[i] = JsonValue("");
		SaveLayoutsBlock();
		if (layoutShown == name) ApplyLayout(DefaultName, false);
		return true;
	}

	bool Hud::ResetLayoutPreset(const cs::String& name)
	{
		if (!IsBuiltinLayout(name)) return false;
		auto block = LayoutsBlock();
		auto presets = block != nullptr ? block->GetObject("presets") : nullptr;
		if (presets == nullptr || !presets->Remove(name)) return false;
		SaveLayoutsBlock();
		if (layoutShown == name) ApplyLayout(name, false);
		return true;
	}

	cs::String Hud::LayoutInSlot(int slot)
	{
		if (slot < 0 || slot >= LayoutSlots) return cs::String();
		auto block = LayoutsBlock();
		auto slots = block != nullptr ? block->GetArray("slots") : nullptr;
		cs::String name;
		// Never set: the built-ins in their order.
		if (slots == nullptr) name = BuiltinLayouts()[slot];
		else if (slot < slots->Count())
			if (auto s = (*slots)[slot].AsString()) name = *s;
		auto names = LayoutNames();
		return std::find(names.begin(), names.end(), name) != names.end() ? name : cs::String();
	}

	void Hud::SetLayoutSlot(int slot, const cs::String& name)
	{
		auto block = LayoutsBlock();
		if (block == nullptr || slot < 0 || slot >= LayoutSlots) return;
		cs::String names[LayoutSlots];
		for (int i = 0; i < LayoutSlots; i++) names[i] = LayoutInSlot(i);
		for (auto& n : names)
			if (n == name) n.clear(); // a preset in one slot only
		names[slot] = name;
		auto slots = cs::New<JsonArray>();
		for (const auto& n : names) slots->Add(JsonValue(n));
		block->Set("slots", slots);
		SaveLayoutsBlock();
	}

	void Hud::LayoutKey(int slot)
	{
		cs::String name = LayoutInSlot(slot);
		if (name.empty())
		{
			app->ShowToast(cs::Cat("Layout ", slot + 1, " holds no preset (pick one in the Layouts drawer)"));
			return;
		}
		if (ApplyLayout(name)) app->ShowToast(cs::Cat("HUD layout: ", name));
	}

	cs::String Hud::LayoutsJson()
	{
		auto block = LayoutsBlock();
		return block != nullptr ? block->ToJson() : cs::String("{}");
	}

	void Hud::RestoreLayoutsJson(const cs::String& json)
	{
		try
		{
			auto profile = ActiveProfile::Current();
			auto parsed = JsonObject::Parse(json);
			if (profile == nullptr || parsed == nullptr) return;
			profile->Settings()->Game()->Set(BlockKey, parsed);
			SaveLayoutsBlock();
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Putting the HUD's layouts back failed: ", e.what())); }
	}

	void Hud::LoadActiveLayout()
	{
		if (layoutsLoaded) return;
		layoutsLoaded = true;
		cs::String name = ActiveLayout();
		if (name != DefaultName || LayoutOverridden(name)) ApplyLayout(name, false);
		else layoutShown = name;
	}

	// ---------------------------------------------------------------- the situation

	cs::String Hud::LayoutSituation() const
	{
		auto ship = app->Piloted.Ship;
		if (ship != nullptr && !ship->Destroyed && ship->Battle != nullptr && !ship->Battle->Over) return "Combat";
		if (app->DockedAt != nullptr) return "Commerce";
		return cs::String();
	}

	void Hud::UpdateLayoutSituation(float dt)
	{
		if (!PlayerOptions::LayoutWithSituation()->Value)
		{
			// Turned off while a situation's preset showed: the chosen one back.
			if (!situationActed.empty())
			{
				situationActed.clear();
				ApplyLayout(ActiveLayout(), false);
			}
			situationSeen.clear();
			situationSince = 0;
			return;
		}
		cs::String now = LayoutSituation();
		if (now != situationSeen)
		{
			situationSeen = now;
			situationSince = 0;
		}
		situationSince += dt;
		if (situationSince < SituationSettleS || now == situationActed) return;
		situationActed = now;
		cs::String want = now.empty() ? ActiveLayout() : now;
		if (want != layoutShown) ApplyLayout(want, false);
	}

	// ---------------------------------------------------------------- the Layouts drawer

	void Hud::LayoutNotice(const cs::String& line)
	{
		layoutStatus = line;
		layoutStatusAt = Time::unscaledTime;
	}

	void Hud::DrawLayouts(Rect r)
	{
		namespace S = UiStyle;
		if (layoutField == nullptr)
		{
			layoutFieldTexture = S::FieldTexture();
			layoutField = S::FieldStyle(S::Type::Label, layoutFieldTexture);
		}
		auto names = LayoutNames();
		cs::String active = ActiveLayout();
		if (std::find(names.begin(), names.end(), layoutPicked) == names.end()) layoutPicked = layoutShown;
		auto old = GUI::contentColor;
		constexpr float RowH = 22, Gap = 3;
		float x = r.x, w = r.width, y = r.y + 2;

		// The presets, each with its Layout key's slot (a click steps it), its name and a USE button.
		int n = static_cast<int>(names.size());
		float listH = std::min(n * (RowH + 1), std::max(RowH * 3, r.height - 4 * (RowH + Gap) - 22));
		auto view = Rect(x, y, w, listH);
		auto content = Rect(0, 0, w - (n * (RowH + 1) > listH ? 16 : 0), n * (RowH + 1));
		cs::String slotOf[64];
		for (int s = 0; s < LayoutSlots; s++)
		{
			cs::String in = LayoutInSlot(s);
			for (int i = 0; i < n && i < 64; i++)
				if (names[i] == in) slotOf[i] = cs::Cat(s + 1);
		}
		layoutScroll = GUI::BeginScrollView(view, layoutScroll, content);
		for (int i = 0; i < n; i++)
		{
			const auto& name = names[i];
			float ry = i * (RowH + 1);
			float cw = content.width;
			// The slot: 1-5 or a dot; a click puts the preset in the next free slot round (past 5: none).
			cs::String slot = i < 64 ? slotOf[i] : cs::String();
			if (GUI::Button(Rect(0, ry, 26, RowH), slot.empty() ? cs::String("-") : slot, slot.empty() ? button : buttonActive))
			{
				int cur = slot.empty() ? -1 : std::atoi(slot.c_str()) - 1;
				int next = cur + 1;
				if (next >= LayoutSlots)
				{
					// Out of the slots: taken out of the one it had.
					SetLayoutSlot(cur, cs::String());
					LayoutNotice(cs::Cat(name, ": no Layout key"));
				}
				else
				{
					SetLayoutSlot(next, name);
					cs::String keys = Input::KeyBindings::Keys(static_cast<Input::KeyAction>(static_cast<int>(Input::KeyAction::Layout1) + next));
					LayoutNotice(cs::Cat(name, " on Layout ", next + 1, keys.empty() || keys == "None" ? cs::String(" (unbound: Settings > Controls)") : cs::Cat(" (", keys, ")")));
				}
			}
			cs::String rowText = name;
			if (LayoutOverridden(name)) rowText += "  (your copy)";
			else if (!IsBuiltinLayout(name)) rowText += "  (yours)";
			if (name == layoutShown) rowText = cs::Cat("> ", rowText);
			if (name == active && name != layoutShown) rowText += "  \xC2\xB7 chosen";
			float useW = 50;
			if (GUI::Button(Rect(30, ry, cw - 30 - useW - 4, RowH), FitText(rowText, small, cw - 30 - useW - 14), name == layoutPicked ? rowSelected : row))
			{
				layoutPicked = name;
				layoutTyping = false;
			}
			if (GUI::Button(Rect(cw - useW, ry, useW, RowH), "USE", name == layoutShown ? buttonActive : button))
			{
				layoutPicked = name;
				if (ApplyLayout(name)) LayoutNotice(cs::Cat("On screen: ", name));
			}
		}
		GUI::EndScrollView();
		y += listH + Gap + 2;

		// What the picked preset can have done: saved over with the layout on screen, renamed, deleted or reset.
		bool builtin = IsBuiltinLayout(layoutPicked);
		bool overridden = LayoutOverridden(layoutPicked);
		float bw = (w - 2 * Gap) / 3;
		if (GUI::Button(Rect(x, y, bw, RowH), "SAVE", button))
		{
			if (SaveLayout(layoutPicked)) LayoutNotice(builtin ? cs::Cat("Saved over ", layoutPicked, " (RESET brings the built-in back)") : cs::Cat("Saved ", layoutPicked));
		}
		bool canRename = !builtin || overridden;
		if (GUI::Button(Rect(x + bw + Gap, y, bw, RowH), "RENAME", layoutNameMode == 1 && layoutTyping ? buttonActive : button))
		{
			if (!canRename) LayoutNotice("A built-in keeps its name: SAVE AS a new one");
			else
			{
				layoutNameMode = 1;
				layoutName = layoutPicked;
				GUI::FocusControl(NameControl);
			}
		}
		if (builtin)
		{
			if (GUI::Button(Rect(x + 2 * (bw + Gap), y, bw, RowH), "RESET", overridden ? button : buttonActive))
			{
				if (ResetLayoutPreset(layoutPicked)) LayoutNotice(cs::Cat(layoutPicked, " back as it ships"));
				else LayoutNotice(cs::Cat(layoutPicked, " is as it ships"));
			}
		}
		else if (GUI::Button(Rect(x + 2 * (bw + Gap), y, bw, RowH), "DELETE", button))
		{
			cs::String gone = layoutPicked;
			if (DeleteLayout(gone)) LayoutNotice(cs::Cat("Deleted ", gone));
		}
		y += RowH + Gap;

		// The name field: SAVE AS a new preset, or (after RENAME) the picked one's new name. Enter does it, Esc leaves it.
		float okW = 84;
		GUI::SetNextControlName(NameControl);
		layoutName = GUI::TextField(Rect(x, y, w - okW - Gap, RowH), layoutName, MaxNameLength, layoutField);
		layoutTyping = GUI::GetNameOfFocusedControl() == NameControl;
		bool enter = false;
		if (layoutTyping && Event::current().type == EventType::KeyDown)
		{
			auto key = Event::current().keyCode;
			if (key == KeyCode::Return || key == KeyCode::KeypadEnter) enter = true;
			if (key == KeyCode::Escape)
			{
				GUIUtility::keyboardControl = 0;
				layoutTyping = false;
				layoutNameMode = 0;
				Event::current().Use();
			}
		}
		if (layoutName.empty() && !layoutTyping)
		{
			GUI::contentColor = S::Dim;
			GUI::Label(Rect(x + 8, y + 3, w - okW - 16, RowH - 4), "a name for this layout", small);
			GUI::contentColor = old;
		}
		bool renaming = layoutNameMode == 1;
		if (GUI::Button(Rect(x + w - okW, y, okW, RowH), renaming ? "RENAME" : "SAVE AS", button) || enter)
		{
			cs::String clean = CleanLayoutName(layoutName);
			if (clean.empty()) LayoutNotice("Type a name first");
			else if (renaming)
			{
				cs::String from = layoutPicked;
				if (RenameLayout(from, clean))
				{
					layoutPicked = clean;
					LayoutNotice(cs::Cat(from, " is now ", clean));
				}
				else LayoutNotice(IsBuiltinLayout(clean) ? cs::String("That is a built-in's name") : cs::Cat("A layout is already called ", clean));
				layoutNameMode = 0;
			}
			else if (IsBuiltinLayout(clean) && !IsBuiltinLayout(layoutPicked)) LayoutNotice("That is a built-in's name: pick it and SAVE to keep a copy");
			else if (SaveLayout(clean))
			{
				layoutPicked = clean;
				LayoutNotice(cs::Cat("Saved as ", clean));
			}
			layoutName.clear();
			GUIUtility::keyboardControl = 0;
			layoutTyping = false;
			if (enter) Event::current().Use();
		}
		y += RowH + Gap;

		// The option: switch with the situation.
		bool situational = PlayerOptions::LayoutWithSituation()->Value;
		if (GUI::Button(Rect(x, y, w, RowH), situational ? "WITH THE SITUATION: ON  (fight: Combat, docked: Commerce)" : "WITH THE SITUATION: OFF", situational ? buttonActive : button))
		{
			PlayerOptions::LayoutWithSituation()->Set(!situational);
			LayoutNotice(situational ? cs::String("The layout stays as you pick it") : cs::String("Combat in a fight, Commerce docked, else your chosen one"));
		}
		y += RowH + Gap;

		// The last thing done, or the hint.
		GUI::contentColor = S::Dim;
		cs::String line = Time::unscaledTime - layoutStatusAt < 5.0f ? layoutStatus
			: cs::String("USE: on screen  \xC2\xB7  SAVE: keep the screen  \xC2\xB7  number: its key");
		if (r.yMax() - y >= 14) GUI::Label(Rect(x, y, w, 18), FitText(line, small, w), small);
		GUI::contentColor = old;
	}
}
