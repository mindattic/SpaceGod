// US-T293: the Society screen and the SpaceGod's hand in the galaxy's people (declared in UnitySpaceApp.Society.inl; the
// screen itself is UI/SocietyScreen, the hands CivHandCommands.cpp). The hand in co-op: a "civ" entry, "x <seat> civ <line>",
// carried out on its frame on every peer (UnitySpaceApp::CoopExecute), where the same module hand runs with the same ids on
// the same day, and every module rolls its outcomes from fixed hashes of ids and days, so every peer's galaxy moves alike.

#include "UnitySpaceApp.h"

#include "CivHand.h"
#include "Coop/CoopPeer.h"
#include "Input/KeyBindings.h"
#include "SimClock.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Galaxy.h"
#include "UI/SocietyScreen.h"

#include "HAL/PlatformTime.h"

#include <algorithm>

namespace SpaceGod
{
	using namespace Core;
	using Coop::CoopPeer;

	cs::String UnitySpaceApp::CivHandAsk(const cs::String& line, bool& sent, cs::String& done)
	{
		sent = false;
		done.clear();
		// One line of words, whatever it came with (an entry's frame is a line: no newline may ride in it).
		auto words = CivHands::Words(line);
		if (words.empty()) return "no verb";
		words[0] = cs::Strings::ToLowerInvariant(words[0]);
		if (!CivHands::IsHand(words[0])) return cs::Cat("'", words[0], "' is not one of the SpaceGod's hands");
		cs::String clean;
		for (const auto& w : words) clean = cs::Cat(clean, clean.empty() ? "" : " ", w);
		if (Galaxy == nullptr || Clock == nullptr) return "no game running";
		// In co-op: to the party, carried out on its frame on every peer (and here, by this same call, from CoopExecute).
		if (CoopSends("civ", clean))
		{
			sent = true;
			return cs::String();
		}
		auto civ = Civilization::Find(*Galaxy);
		if (civ == nullptr) return "the galaxy's people have not woken yet (let a day pass)";
		cs::String why = CivHands::Apply(*Galaxy, *civ, clean, Clock->Jd, done);
		if (why.empty()) ShowToast(done);
		return why;
	}

	void UnitySpaceApp::CoopCivHand(CoopPeer& peer, int seat, const cs::String& line)
	{
		bool sent = false;
		cs::String done;
		cs::String why = CivHandAsk(line, sent, done);
		// The asker hears why not (another player's toasts are muted here, as for every entry carried out).
		if (!why.empty()) ShowToast(cs::Cat("The SpaceGod's hand is refused: ", why));
		CoopPeer::Log(cs::Cat(peer.NameOf(seat), " (player ", seat + 1, ") lays the SpaceGod's hand '", line, "' on day ", cs::Fmt(Clock != nullptr ? Clock->Jd : 0.0, "0.00000"), ": ",
			why.empty() ? done : cs::Cat("refused, ", why), " at frame ", peer.Frame()));
	}

	void UnitySpaceApp::DrawSociety()
	{
		if (!societyScreen) societyScreen = cs::New<UI::SocietyScreen>(this);
		if (societyScreen->Draw(cs::Cat("Esc or ", Input::KeyBindings::Keys(Input::KeyAction::Society), " to close"))) ShowSociety = false;
	}

	void UnitySpaceApp::DestroySocietyTextures()
	{
		if (societyScreen) societyScreen->DestroyOwnedTextures();
	}

	bool UnitySpaceApp::UpdateSocietyKeys()
	{
		UpdateSocietyWatch();
		return ShowSociety && societyScreen != nullptr && societyScreen->Typing();
	}

	bool UnitySpaceApp::IsWatched(const CivPick& p) const
	{
		return std::find(SocietyWatched.begin(), SocietyWatched.end(), p) != SocietyWatched.end();
	}

	void UnitySpaceApp::ToggleWatch(const CivPick& p)
	{
		if (p.IsNone() || p.Kind == CivPickKind::Trial) return;
		auto at = std::find(SocietyWatched.begin(), SocietyWatched.end(), p);
		if (at != SocietyWatched.end()) SocietyWatched.erase(at);
		else SocietyWatched.push_back(p);
	}

	void UnitySpaceApp::UpdateSocietyWatch()
	{
		static constexpr double ToastGapSeconds = 4;
		auto civ = Galaxy != nullptr ? Civilization::Find(*Galaxy) : nullptr;
		if (civ == nullptr) return;
		int n = civ->Events.Count();
		int lastId = n > 0 && civ->Events[n - 1] != nullptr ? civ->Events[n - 1]->Id : 0;
		// Another galaxy's people (a load, a new game): their ids name other people, so the list starts over.
		if (civ.get() != watchCiv)
		{
			if (watchCiv != nullptr) SocietyWatched.clear();
			watchCiv = civ.get();
			watchSeenId = lastId;
			watchHeld = 0;
			watchToast.clear();
			return;
		}
		if (lastId < watchSeenId) watchSeenId = lastId; // the feed went back (a rewind): look on from there
		if (!SocietyWatched.empty() && lastId > watchSeenId)
		{
			bool newest = true;
			for (int i = n - 1; i >= 0; i--)
			{
				const auto& e = civ->Events[i];
				if (e == nullptr) continue;
				if (e->Id <= watchSeenId) break;
				bool about = false;
				for (const auto& p : SocietyWatched)
					if (UI::SocietyScreen::About(*e, p)) { about = true; break; }
				if (!about) continue;
				// The newest is told (over any held back); the rest are counted with it.
				if (newest) watchToast = Narrator::Render(*e);
				newest = false;
				watchHeld++;
			}
		}
		watchSeenId = lastId;
		if (watchHeld == 0) return;
		// Looking at the Society screen already: what it shows is the news.
		if (ShowSociety) { watchHeld = 0; watchToast.clear(); return; }
		double now = FPlatformTime::Seconds();
		if (now - watchToastAt < ToastGapSeconds) return;
		ShowToast(cs::Cat("Watched: ", watchToast, watchHeld > 1 ? cs::Cat("  (+", watchHeld - 1, " more: ", Input::KeyBindings::Keys(Input::KeyAction::Society), ")") : cs::String()));
		watchToastAt = now;
		watchHeld = 0;
		watchToast.clear();
	}
}
