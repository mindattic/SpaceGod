// US-T271: the Encyclopedia as the player meets it (declared in UnitySpaceApp.Encyclopedia.inl; the screen itself is
// UI/EncyclopediaScreen, the model Core/Lore/Encyclopedia). Display only: the knowledge reads the simulation, nothing reads it.

#include "UnitySpaceApp.h"

#include "GameLore.h"
#include "SimClock.h"
#include "Input/Gamepad.h"
#include "Input/KeyBindings.h"
#include "Lore/Encyclopedia.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Galaxy.h"
#include "UI/EncyclopediaScreen.h"

#include <vector>

namespace SpaceGod
{
	using namespace Core;

	EncyclopediaKnowledge& UnitySpaceApp::Knowledge()
	{
		if (Galaxy->Knowledge == nullptr) Galaxy->Knowledge = cs::New<EncyclopediaKnowledge>();
		return *Galaxy->Knowledge;
	}

	void UnitySpaceApp::UpdateKnowledge()
	{
		if (Galaxy == nullptr) return;
		const auto& log = Discoveries();
		bool changed = Galaxy.get() != knowledgeGalaxy || log.Version != knowledgeLog;
		if (!changed && ++knowledgeFrames < 180) return;
		knowledgeFrames = 0;
		knowledgeGalaxy = Galaxy.get();
		knowledgeLog = log.Version;
		// Also every few seconds when nothing new was found: the fleets in the systems visited move on.
		auto books = RecoveredLogKeys();
		knowledgeBooks = books.Count();
		Knowledge().Sweep(*Galaxy, &log, books);
	}

	void UnitySpaceApp::AddControlArticles(Encyclopedia& book)
	{
		using namespace ::SpaceGod::Input;
		// One article per group of the bindings page ("Flight: roll left" is the Flight group's), in the page's order.
		std::vector<cs::String> groups;
		std::vector<cs::List<EncyclopediaSection>> lines;
		std::vector<cs::String> keywords;
		for (int i = 0; i < static_cast<int>(KeyAction::Count); i++)
		{
			auto action = static_cast<KeyAction>(i);
			cs::String name = KeyBindings::Name(action);
			size_t colon = name.find(':');
			cs::String group = colon == cs::String::npos ? cs::String("Other") : name.substr(0, colon);
			cs::String label = colon == cs::String::npos ? name : cs::Strings::Trim(name.substr(colon + 1));
			if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') label[0] = static_cast<char>(label[0] - 'a' + 'A');
			size_t g = 0;
			while (g < groups.size() && groups[g] != group) g++;
			if (g == groups.size())
			{
				groups.push_back(group);
				lines.emplace_back();
				keywords.push_back(cs::String());
			}
			const auto& binding = KeyBindings::Binding(action);
			int context = KeyBindings::Context(action);
			cs::String where = context == KeyContext::Command ? cs::String("  (in command)") : context == KeyContext::Controls ? cs::String("  (at the controls)") : cs::String();
			lines[g].Add(EncyclopediaSection{ KeyBindings::Keys(action), cs::Cat(label, where, ": ", binding->Description) });
			keywords[g] = cs::Cat(keywords[g], " ", label, " ", binding->Keywords, " ", KeyBindings::Keys(action));
		}
		for (size_t g = 0; g < groups.size(); g++)
			book.AddControls(cs::Cat("keys-", groups[g]), cs::Cat(groups[g], " keys"),
				cs::Cat("The keys as they are bound now", Input::KeyBindings::AnyChanged() ? " (some rebound)" : "", "; rebind them under Settings > Controls > Key bindings"),
				lines[g], cs::Cat(groups[g], " ", keywords[g]));

		// The gamepad, as Settings > Controls binds it now.
		cs::List<EncyclopediaSection> pad;
		pad.Add(EncyclopediaSection{ "LS", "Left stick: thrust and strafe at the controls; the camera's swim in command." });
		pad.Add(EncyclopediaSection{ "RS", "Right stick: aim (the look the hull is pulled after); in command, look about. In menus it scrolls." });
		for (int i = 0; i < static_cast<int>(PadAction::Count); i++)
		{
			auto a = static_cast<PadAction>(i);
			PadButton b = Gamepad::ButtonFor(a);
			if (b == PadButton::None) continue;
			cs::String glyph = ToString(b);
			if (b == PadButton::LeftStick || b == PadButton::RightStick) glyph = cs::Cat(glyph, " click");
			pad.Add(EncyclopediaSection{ glyph, Gamepad::ActionName(a) });
		}
		pad.Add(EncyclopediaSection{ "D-pad", "At the controls, the reactor: left weapons, up shields, right engines, down an even share. In command: up / down time faster / slower, right / left the next / previous ship." });
		pad.Add(EncyclopediaSection{ "Menus", "D-pad or left stick moves the highlight, A presses, B backs out." });
		book.AddControls("gamepad", "Gamepad", "The pad's buttons as they are bound now; rebind them, the dead zone and the aim speed under Settings > Controls",
			pad, "gamepad pad controller stick trigger button xbox");
	}

	const Encyclopedia& UnitySpaceApp::EncyclopediaBook()
	{
		if (encyclopedia == nullptr) encyclopedia = cs::New<Encyclopedia>();
		if (Galaxy == nullptr) return *encyclopedia;
		const auto& log = Discoveries();
		const auto& known = Knowledge();
		auto books = RecoveredLogKeys();
		bool stale = encyclopediaStale || Galaxy.get() != encyclopediaGalaxy || log.Version != encyclopediaLog || known.Version != encyclopediaKnown
			|| books.Count() != encyclopediaBooks;
		if (!stale) return *encyclopedia;
		encyclopediaStale = false;
		encyclopediaGalaxy = Galaxy.get();
		encyclopediaLog = log.Version;
		encyclopediaKnown = known.Version;
		encyclopediaBooks = books.Count();

		auto& book = *encyclopedia;
		book.Clear();
		const auto& lore = GameLore();
		AddControlArticles(book);
		book.AddControlCard();
		book.AddGuides();
		book.AddLore(lore, books);
		book.AddDiscoveries(*Galaxy, log, lore, Clock != nullptr ? Clock->Jd : 0.0);
		book.AddCommerce(*Galaxy, &log);
		book.AddFactions(*Galaxy, lore);
		book.AddShips();
		book.Finish();
		encyclopediaBuilds++;
		return book;
	}

	bool UnitySpaceApp::OpenArticle(const cs::String& id)
	{
		if (Galaxy == nullptr) return false;
		ShowEncyclopedia = true;
		if (!encyclopediaScreen) encyclopediaScreen = cs::New<UI::EncyclopediaScreen>(this);
		const auto& book = EncyclopediaBook();
		auto a = book.Find(id);
		if (a != nullptr && Encyclopedia::IsKnown(*a, &Knowledge()))
		{
			encyclopediaScreen->Show(id);
			return true;
		}
		// Not (yet) known: the words of its slug, searched ("astronomy/planet-sol-earth" -> "planet sol earth").
		cs::String words = id.substr(id.find('/') == cs::String::npos ? 0 : id.find('/') + 1);
		for (auto& c : words)
			if (c == '-') c = ' ';
		encyclopediaScreen->Search(words);
		return false;
	}

	cs::List<cs::String> UnitySpaceApp::ArticleLiveStats(const EncyclopediaArticle& a)
	{
		if (a.EntityId.empty() || Galaxy == nullptr || Clock == nullptr) return {};
		auto e = Discoveries().Find(a.EntityId);
		if (e == nullptr) return {};
		return DiscoveryLog::Readings(*Galaxy, *e, Clock->Jd);
	}

	bool UnitySpaceApp::UpdateEncyclopediaKeys()
	{
		if (!ShowEncyclopedia)
		{
			encyclopediaWasShown = false;
			return false;
		}
		return encyclopediaScreen != nullptr && encyclopediaScreen->Typing();
	}

	void UnitySpaceApp::DrawEncyclopedia()
	{
		if (!encyclopediaScreen) encyclopediaScreen = cs::New<UI::EncyclopediaScreen>(this);
		if (!encyclopediaWasShown)
		{
			// Just opened: built afresh (a rebind, a market moved on), the filter ready to type in.
			encyclopediaWasShown = true;
			encyclopediaStale = true;
			encyclopediaScreen->Opened();
		}
		if (encyclopediaScreen->Draw(cs::Cat("Esc or ", Input::KeyBindings::Keys(Input::KeyAction::Encyclopedia), " to close")))
		{
			ShowEncyclopedia = false;
			encyclopediaScreen->Closed();
		}
	}

	void UnitySpaceApp::DestroyEncyclopediaTextures()
	{
		if (encyclopediaScreen) encyclopediaScreen->DestroyOwnedTextures();
	}
}
