// US-T293: the Society screen and the SpaceGod's hand in the galaxy's people (declared in UnitySpaceApp.Society.inl; the
// screen itself is UI/SocietyScreen, the hands CivHandCommands.cpp). The hand in co-op: a "civ" entry, "x <seat> civ <line>",
// carried out on its frame on every peer (UnitySpaceApp::CoopExecute), where the same module hand runs with the same ids on
// the same day, and every module rolls its outcomes from fixed hashes of ids and days, so every peer's galaxy moves alike.

#include "UnitySpaceApp.h"

#include "CivHand.h"
#include "Coop/CoopPeer.h"
#include "Economy/Settlement.h"
#include "Input/InputDevice.h" // US-T346
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

	void UnitySpaceApp::OpenSocietyAt(const CivPick& p)
	{
		if (!societyScreen) societyScreen = cs::New<UI::SocietyScreen>(this);
		societyScreen->Show(p);
		ShowSociety = true;
	}

	void UnitySpaceApp::DestroySocietyTextures()
	{
		if (societyScreen) societyScreen->DestroyOwnedTextures();
	}

	bool UnitySpaceApp::UpdateSocietyKeys()
	{
		UpdateSocietyWatch();
		UpdateSocietyTrends();
		if (!ShowSociety || societyScreen == nullptr) return false;
		if (societyScreen->Typing()) return true;
		// US-T346: the keys the screen steps and marks with (Up / Down, a plain C, Alt+Left / Right) are its own while held,
		// not the flight's under it (C is also "down" at the controls).
		using ::SpaceGod::Input::Key;
		const ::SpaceGod::Input::InputDevice& kb = ::SpaceGod::Input::InputDevice::current();
		auto down = [&](Key k) { return kb[k].isPressed || kb[k].wasPressedThisFrame; };
		bool shift = down(Key::LeftShift) || down(Key::RightShift), ctrl = down(Key::LeftCtrl) || down(Key::RightCtrl);
		bool alt = down(Key::LeftAlt) || down(Key::RightAlt);
		if (down(Key::UpArrow) || down(Key::DownArrow)) return true;
		if (alt && (down(Key::LeftArrow) || down(Key::RightArrow))) return true;
		// US-T354: and a plain S, which steps the list's sort (S is also "back" at the controls).
		return (down(Key::C) || down(Key::S)) && !shift && !ctrl && !alt;
	}

	const std::vector<UnitySpaceApp::SocietyTrendSample>* UnitySpaceApp::SocietyTrendOf(const cs::String& world) const
	{
		auto at = societyTrends.find(world);
		return at == societyTrends.end() || at->second.empty() ? nullptr : &at->second;
	}

	void UnitySpaceApp::UpdateSocietyTrends()
	{
		auto civ = Galaxy != nullptr && Clock != nullptr ? Civilization::Find(*Galaxy) : nullptr;
		if (civ == nullptr) return;
		double jd = Clock->Jd;
		// Another galaxy's people (a load, a new game): their worlds' past is not this one's.
		if (civ.get() != trendCiv)
		{
			trendCiv = civ.get();
			societyTrends.clear();
			trendNextJd = -1e18;
		}
		// A rewind: what was sampled after the day rewound to has not happened (yet).
		if (jd < trendNextJd - SocietyTrendDays)
		{
			for (auto& [name, samples] : societyTrends)
				samples.erase(std::remove_if(samples.begin(), samples.end(), [jd](const SocietyTrendSample& s) { return s.Jd > jd; }), samples.end());
			trendNextJd = jd;
		}
		if (jd < trendNextJd) return;
		trendNextJd = jd + SocietyTrendDays;
		for (const auto& c : civ->Worlds)
		{
			if (c == nullptr || c->Where == nullptr) continue;
			auto& samples = societyTrends[c->Where->Name];
			samples.push_back(SocietyTrendSample{ jd, c->Approval, c->Where->Unrest, c->Crime, c->Infected, c->Weariness });
			if (static_cast<int>(samples.size()) > SocietyTrendMax) samples.erase(samples.begin());
		}
	}

	bool UnitySpaceApp::IsWatched(const CivPick& p) const
	{
		return std::find(SocietyWatched.begin(), SocietyWatched.end(), p) != SocietyWatched.end();
	}

	void UnitySpaceApp::ToggleWatch(const CivPick& p)
	{
		if (p.IsNone() || p.Kind == CivPickKind::Trial) return;
		auto at = std::find(SocietyWatched.begin(), SocietyWatched.end(), p);
		auto seen = std::find_if(watchLastSeen.begin(), watchLastSeen.end(), [&](const std::pair<CivPick, int>& s) { return s.first == p; });
		if (seen != watchLastSeen.end()) watchLastSeen.erase(seen);
		if (at != SocietyWatched.end()) SocietyWatched.erase(at);
		else
		{
			SocietyWatched.push_back(p);
			watchLastSeen.emplace_back(p, LatestSocialEventId()); // US-T351: taken up now, so nothing about it is new yet
		}
	}

	int UnitySpaceApp::LatestSocialEventId() const
	{
		auto civ = Galaxy != nullptr ? Civilization::Find(*Galaxy) : nullptr;
		if (civ == nullptr) return 0;
		for (int i = civ->Events.Count() - 1; i >= 0; i--)
			if (civ->Events[i] != nullptr) return civ->Events[i]->Id;
		return 0;
	}

	int UnitySpaceApp::WatchSeenId(const CivPick& p) const
	{
		for (const auto& s : watchLastSeen)
			if (s.first == p) return s.second;
		return LatestSocialEventId();
	}

	void UnitySpaceApp::MarkWatchSeen(const CivPick& p)
	{
		if (!IsWatched(p)) return;
		int latestId = LatestSocialEventId();
		for (auto& s : watchLastSeen)
			if (s.first == p)
			{
				s.second = latestId;
				return;
			}
		watchLastSeen.emplace_back(p, latestId);
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
			if (watchCiv != nullptr)
			{
				SocietyWatched.clear();
				watchLastSeen.clear(); // US-T351
			}
			watchCiv = civ.get();
			watchSeenId = lastId;
			watchHeld = 0;
			watchToast.clear();
			return;
		}
		if (lastId < watchSeenId)
		{
			watchSeenId = lastId; // the feed went back (a rewind): look on from there
			for (auto& s : watchLastSeen)
				if (s.second > lastId) s.second = lastId; // US-T351: and so did what was seen
		}
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
