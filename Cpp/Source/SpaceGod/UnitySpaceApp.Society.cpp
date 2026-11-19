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
#include "Systems/Galaxy.h"
#include "UI/SocietyScreen.h"

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
		return ShowSociety && societyScreen != nullptr && societyScreen->Typing();
	}
}
