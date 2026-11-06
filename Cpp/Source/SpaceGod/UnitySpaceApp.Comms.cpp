// US-T137 comms panel and US-T138 named-pilot tracking (declarations in UnitySpaceApp.Comms.inl).

#include "UnitySpaceApp.h"

#include "Combat/Battle.h"
#include "Comms/Comms.h"
#include "Comms/Pilots.h"
#include "Coop/CoopPeer.h" // US-T140: the tracked body the same on every peer
#include "Embodiment/Embodiment.h"
#include "Session/ISession.h"
#include "Embodiment/StationSeat.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Input/InputDevice.h"
#include "ScaleModel.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "SimClock.h"
#include "UI/Hud.h"
#include "World.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		constexpr int CommsLogKept = 40;

		Color StanceColor(CommsStance s)
		{
			switch (s)
			{
			case CommsStance::Friendly: return Color(0.45f, 1.0f, 0.6f);
			case CommsStance::Neutral: return Color(0.8f, 0.88f, 0.95f);
			case CommsStance::Wary: return Color(1.0f, 0.78f, 0.35f);
			default: return Color(1.0f, 0.4f, 0.35f);
			}
		}
	}

	// ---------------------------------------------------------------- who speaks, from where

	CommsContext UnitySpaceApp::CommsHere()
	{
		CommsContext c;
		c.World = World();
		c.Jd = Clock->Jd;
		// Ranges at realistic scale are true; at game scale they are the distance as drawn (the scale model's mapping near the
		// speaker's frame body), so "10 klicks" matches what is on the screen.
		c.Ruler.Realistic = Scale()->Blend() < 0.5;
		if (IsPiloting() && Piloted.Ship != nullptr)
		{
			c.Speaker = Piloted.Ship;
			c.SpeakerFaction = Piloted.Ship->Faction;
			c.FrameBody = pilotFrameBody;
			c.OffsetKm = pilotOffsetKm();
			c.Forward = pilot().Forward;
			c.Up = pilot().Up;
			c.Piloted = true;
		}
		else if (auto seat = Seat())
		{
			auto subject = seat->Subject();
			c.Speaker = subject.Ship;
			if (auto station = cs::As<StationSeat>(seat)) c.SpeakerStation = station->Settlement;
			c.SpeakerFaction = seat->Faction();
			auto pose = seat->PoseAt(Clock->Jd);
			c.FrameBody = pose.FrameBody;
			c.OffsetKm = pose.OffsetKm;
			c.Forward = pose.Forward;
			c.Up = pose.Up;
			c.Piloted = true;
		}
		c.HelioKm = Ephemeris()->HelioAt(c.FrameBody, Clock->Jd) + c.OffsetKm;
		int frame = c.FrameBody;
		c.Ruler.DrawnKm = [this, frame](DVec3 a, DVec3 b)
		{
			auto sa = Scale()->MapNear(Ephemeris(), bodyScene, frame, a);
			auto sb = Scale()->MapNear(Ephemeris(), bodyScene, frame, b);
			return (sa - sb).Length() * ScaleModel::KmPerUnit;
		};
		return c;
	}

	cs::String UnitySpaceApp::CommsLabel(const CommsContext& c, const CommsParty& p)
	{
		if (p.IsStation()) return p.Name();
		if (p.Ship == nullptr) return "nobody";
		auto level = Comms::Seen(c, p.Ship);
		if (level == ContactLevel::None) level = ContactLevel::Blip;
		return level == ContactLevel::Identified ? cs::Cat("the ", p.Ship->Name) : Contact::Describe(p.Ship, level);
	}

	// ---------------------------------------------------------------- opening, closing, saying

	void UnitySpaceApp::OpenComms(const CommsParty& target)
	{
		auto c = CommsHere();
		if (!c.Piloted)
		{
			ShowToast("Comms: take a ship, a gun or a station first (P) -- whatever is said is said by the unit you hold");
			return;
		}
		commsOpen = true;
		commsContacts = Comms::Contacts(c);
		commsTalking = false;
		commsRow = 0;
		if (!target.IsNone())
		{
			commsTarget = target;
			commsTalking = true;
			for (int i = 0; i < commsContacts.Count(); i++)
				if (commsContacts[i] == target) commsRow = i;
		}
		// The hull locked at the helm, or selected, is the first one offered.
		else
		{
			auto want = pilotLockTarget.IsShip() ? pilotLockTarget.Ship : Selected.IsShip() ? Selected.Ship : nullptr;
			for (int i = 0; i < commsContacts.Count(); i++)
				if (want != nullptr && commsContacts[i].Ship == want) commsRow = i;
		}
		commsRefresh = 0.5f;
	}

	void UnitySpaceApp::CloseComms()
	{
		commsOpen = false;
		commsTalking = false;
		inputHandover = true; // keys still held from the panel wait to be let go before the craft takes them
	}

	void UnitySpaceApp::CommsSay(CommsVerb verb)
	{
		auto c = CommsHere();
		if (verb == CommsVerb::SignOff)
		{
			auto r = Comms::Say(c, commsTarget, verb);
			for (const auto& l : r.Lines) commsLog.Add(l);
			commsTalking = false;
			return;
		}
		// Solo, the panel says it straight away; the command (Core::CommsCommand, "comms <player> <target> <verb>") carries the
		// same two numbers for a co-op peer or a replay, and Comms::Apply gives the same answer there.
		auto r = Comms::Apply(c, commsTarget.Key(), static_cast<int>(verb));
		for (const auto& l : r.Lines) commsLog.Add(l);
		while (commsLog.Count() > CommsLogKept) commsLog.RemoveAt(0);
		if (!r.Toast.empty()) ShowToast(r.Toast);
		if (r.Battle != nullptr) CloseComms(); // a fight has started: the hands go back to the helm
		commsContacts = Comms::Contacts(c);
	}

	bool UnitySpaceApp::CommsShowcase(int step, cs::String& failed)
	{
		failed = cs::String();
		if (step == 0)
		{
			// The lawful hull out in space with the most ships in hailing range of it, flown at its helm.
			cs::Ref<Ship> best;
			int bestCount = 0;
			for (const auto& s : World()->Fleet->Ships())
			{
				if (s == nullptr || s->Destroyed || s->GateDeparted || s->Planetside() || s->Battle != nullptr || s->Craft != nullptr) continue;
				if (s->Faction == nullptr || s->Faction->IsPirate || s->Phase == ShipPhase::Lane) continue;
				CommsContext c;
				c.World = World();
				c.Speaker = s;
				c.SpeakerFaction = s->Faction;
				c.Jd = Clock->Jd;
				c.HelioKm = World()->Sensors->HelioOf(s, Clock->Jd);
				int n = 0;
				for (const auto& p : Comms::Contacts(c, 30))
					if (p.Ship != nullptr) n++;
				if (n > bestCount)
				{
					bestCount = n;
					best = s;
				}
			}
			if (best == nullptr)
			{
				failed = "no hull with anyone in hailing range";
				return false;
			}
			EnterPiloting(Target::OfShip(best));
			return true;
		}
		if (!IsPiloting())
		{
			failed = "the helm was not taken";
			return false;
		}
		OpenComms();
		CommsParty pick;
		for (const auto& p : commsContacts)
			if (p.Ship != nullptr && Comms::StanceOf(CommsHere(), p) != CommsStance::Hostile)
			{
				pick = p;
				break;
			}
		if (pick.IsNone() && commsContacts.Count() > 0) pick = commsContacts[0];
		if (pick.IsNone())
		{
			failed = "nobody in hailing range";
			return false;
		}
		for (int i = 0; i < commsContacts.Count(); i++)
			if (commsContacts[i] == pick) commsRow = i;
		commsTarget = pick;
		commsTalking = true;
		CommsSay(CommsVerb::Hail);
		CommsSay(CommsVerb::AskAdvice);
		CommsSay(CommsVerb::AskStory);
		return true;
	}

	// ---------------------------------------------------------------- the held unit's pilot (US-T138)

	void UnitySpaceApp::TrackEmbodiedPilot()
	{
		auto world = World();
		if (world == nullptr || world->Law == nullptr) return;
		cs::Ref<Ship> held;
		if (IsPiloting()) held = Piloted.Ship;
		else if (Coop::CoopPeer::Active())
		{
			// US-T140: in co-op the register (and the SpaceGod's Influence its acts earn) is world state, so every peer
			// tracks the same body: the helm (every peer flies it), else the first seat held in player order, never this
			// machine's own seat.
			for (const auto& h : session->State()->Held())
				if (h.Seat != nullptr && h.Seat->Subject().Ship != nullptr)
				{
					held = h.Seat->Subject().Ship;
					break;
				}
		}
		else if (auto seat = Seat()) held = seat->Subject().Ship;
		if (held != nullptr && (held->Destroyed || held->GateDeparted)) held = nullptr;
		auto chars = Characters::Of(world);
		double jd = Clock->Jd;
		if (held != commsHeldShip)
		{
			// Left: whatever the body did is its pilot's now, and they will tell it as their own choice.
			if (commsHeldShip != nullptr) chars->Released(world, commsHeldShip, jd);
			commsHeldShip = held;
			commsHeldBattle = nullptr;
			commsBetrayalNoted = false;
			if (held != nullptr) chars->Embodied(world, held, jd);
		}
		if (held == nullptr) return;
		auto pilotP = chars->Find(held);
		if (pilotP == nullptr) return;
		auto battle = held->Battle;
		if (battle != commsHeldBattle)
		{
			// Out of a fight with the hull whole: one more they lived through.
			if (commsHeldBattle != nullptr && battle == nullptr) chars->SurvivedBattle(pilotP, commsHeldBattle, jd);
			commsHeldBattle = battle;
			commsBetrayalNoted = false;
		}
		if (battle == nullptr) return;
		// Turning on their own: the held hull fighting a unit of its own faction on the other side.
		auto me = battle->UnitOf(held);
		if (!commsBetrayalNoted && me != nullptr)
			for (const auto& u : battle->Units)
				if (u != me && u->Present() && u->Faction == held->Faction && u->Side != me->Side)
				{
					commsBetrayalNoted = true;
					auto whom = u->Ship != nullptr ? (chars->Find(u->Ship) != nullptr ? chars->Find(u->Ship)->Name : cs::Cat("the crew of the ", u->Ship->Name))
						: cs::Cat("their own ", held->Faction != nullptr ? held->Faction->Name : cs::String("side"));
					chars->Betrayal(pilotP, whom, jd);
					break;
				}
		// Whoever the held hull has locked in a fight is someone it has fought.
		if (pilotLockTarget.IsShip() && pilotLockTarget.Ship != nullptr && pilotLockTarget.Ship->Battle == battle && chars->Find(pilotLockTarget.Ship) == nullptr)
			chars->Meet(world, pilotLockTarget.Ship, MeetKind::Fought, jd);
	}

	// ---------------------------------------------------------------- the frame

	bool UnitySpaceApp::UpdateComms()
	{
		TrackEmbodiedPilot();
		const Input::InputDevice& kb = Input::InputDevice::current();
		auto pressed = [&](Input::Key k) { return kb[k].wasPressedThisFrame; };
		bool shift = kb[Input::Key::LeftShift].isPressed || kb[Input::Key::RightShift].isPressed;
		bool toggle = pressed(Input::Key::H) && shift;
		auto c = CommsHere();

		// A hull that has struck its colours to the held unit's side hails on its own.
		auto hailing = c.Piloted ? Comms::SurrenderHail(c) : nullptr;
		if (hailing != nullptr && hailing != commsHailNoted)
		{
			commsHailNoted = hailing;
			auto offer = Comms::SurrenderOffer(c, hailing);
			auto who = hailing->Ship != nullptr && Characters::Of(World())->Find(hailing->Ship) != nullptr
				? Characters::Of(World())->Find(hailing->Ship)->Name : hailing->Name();
			commsLog.Add({ who, offer, false });
			ShowToast(cs::Cat("INCOMING HAIL: the ", hailing->Name(), " strikes its colours  (Shift+H to answer)"));
			if (commsOpen && hailing->Ship != nullptr)
			{
				commsTarget = CommsParty::OfShip(hailing->Ship);
				commsTalking = true;
			}
		}
		if (hailing == nullptr) commsHailNoted = nullptr;

		if (!commsOpen)
		{
			if (!toggle) return false;
			OpenComms(hailing != nullptr && hailing->Ship != nullptr ? CommsParty::OfShip(hailing->Ship) : CommsParty());
			return true; // the key is spent (it is H: the help screen does not open under it)
		}
		if (toggle || !c.Piloted)
		{
			CloseComms();
			return true;
		}

		// Clicks from the last GUI pass.
		if (commsClickedContact >= 0 && commsClickedContact < commsContacts.Count())
		{
			commsRow = commsClickedContact;
			commsTarget = commsContacts[commsRow];
			commsTalking = true;
		}
		int sayIndex = commsClickedVerb;
		commsClickedContact = commsClickedVerb = -1;

		commsRefresh -= Time::unscaledDeltaTime;
		if (commsRefresh <= 0)
		{
			commsRefresh = 0.5f;
			commsContacts = Comms::Contacts(c);
		}
		if (commsRow >= commsContacts.Count()) commsRow = std::max(0, commsContacts.Count() - 1);

		if (pressed(Input::Key::Escape))
		{
			if (commsTalking) commsTalking = false;
			else CloseComms();
			return true;
		}
		if (!commsTalking)
		{
			if (pressed(Input::Key::DownArrow)) commsRow = std::min(commsRow + 1, std::max(0, commsContacts.Count() - 1));
			if (pressed(Input::Key::UpArrow)) commsRow = std::max(0, commsRow - 1);
			if ((pressed(Input::Key::Enter) || pressed(Input::Key::RightArrow)) && commsRow < commsContacts.Count())
			{
				commsTarget = commsContacts[commsRow];
				commsTalking = true;
				sayIndex = 0; // opening a channel is a hail
			}
		}
		else if (pressed(Input::Key::LeftArrow) || pressed(Input::Key::Backspace)) commsTalking = false;
		if (commsTalking)
		{
			const Input::Key digits[] = { Input::Key::Digit1, Input::Key::Digit2, Input::Key::Digit3, Input::Key::Digit4, Input::Key::Digit5,
				Input::Key::Digit6, Input::Key::Digit7, Input::Key::Digit8, Input::Key::Digit9 };
			for (int i = 0; i < 9; i++)
				if (pressed(digits[i])) sayIndex = i;
			auto verbs = Comms::Verbs(c, commsTarget);
			if (sayIndex >= 0 && sayIndex < verbs.Count()) CommsSay(verbs[sayIndex]);
		}
		return true;
	}

	// ---------------------------------------------------------------- drawing

	void UnitySpaceApp::DrawComms()
	{
		if (!commsOpen || ScreenCleared) return;
		EnsureCommsStyles();
		auto c = CommsHere();
		float w = std::min(static_cast<float>(Screen::width) - 32, 980.0f);
		float h = std::min(static_cast<float>(Screen::height) - 140, 470.0f);
		float x = (Screen::width - w) * 0.5f;
		float y = Screen::height - h - 96;
		auto box = Rect(x, y, w, h);
		GUI::Box(box, GUIContent::none(), commsPanel);
		if (!UiRects.Contains(box)) UiRects.Add(box);

		auto me = c.Speaker != nullptr ? Characters::Of(World())->Find(c.Speaker) : nullptr;
		cs::String speaker = me != nullptr ? cs::Cat(me->Styled(), ", the ", c.Speaker->Name)
			: c.SpeakerStation != nullptr ? cs::Cat(c.SpeakerStation->Name, " control") : cs::String("no one");
		GUI::Label(Rect(x + 16, y + 10, 200, 22), "COMMS", commsTitle);
		GUI::Label(Rect(x + 96, y + 14, w - 112, 18), cs::Cat("speaking as ", speaker, c.SpeakerFaction != nullptr ? cs::Cat(" (", c.SpeakerFaction->Name, ")") : cs::String(),
			"   ·   ranges ", c.Ruler.Realistic ? "true" : "as drawn"), commsSmall);

		// The contacts, down the left.
		float listW = 330, top = y + 40, rowH = 24;
		auto mouse = Event::current().mousePosition;
		if (commsContacts.Count() == 0) GUI::Label(Rect(x + 16, top + 4, listW - 16, 18), "Nobody in hailing range.", commsSmall);
		for (int i = 0; i < commsContacts.Count() && top + (i + 1) * rowH < y + h - 30; i++)
		{
			const auto& p = commsContacts[i];
			auto r = Rect(x + 10, top + i * rowH, listW, rowH - 2);
			bool picked = i == commsRow;
			if (picked) GUI::Box(r, GUIContent::none(), commsRowPicked);
			else if (r.Contains(mouse)) GUI::Box(r, GUIContent::none(), commsRowStyle);
			auto stance = Comms::StanceOf(c, p);
			GUI::contentColor = StanceColor(stance);
			GUI::Label(Rect(r.x + 8, r.y + 3, listW - 120, 18), CommsLabel(c, p), commsText);
			GUI::contentColor = Color::white();
			GUI::Label(Rect(r.x + listW - 112, r.y + 4, 108, 18), c.Ruler.Range(c.HelioKm, Comms::HelioOf(c, p)), commsSmall);
			if (GUI::Button(r, GUIContent::none(), GUIStyle::none())) commsClickedContact = i;
		}

		// The channel, on the right: who, what can be said, and the log.
		float rx = x + listW + 24, rw = w - listW - 40;
		float ry = top;
		if (commsTalking && !commsTarget.IsNone())
		{
			auto stance = Comms::StanceOf(c, commsTarget);
			auto them = commsTarget.Ship != nullptr ? Characters::Of(World())->Find(commsTarget.Ship) : nullptr;
			cs::String head = them != nullptr ? cs::Cat(them->Styled(), them->Callsign.empty() ? cs::String() : cs::Cat(" \"", them->Callsign, "\""), "  ·  ", CommsLabel(c, commsTarget))
				: CommsLabel(c, commsTarget);
			GUI::contentColor = StanceColor(stance);
			GUI::Label(Rect(rx, ry, rw, 20), head, commsTitle);
			GUI::contentColor = Color::white();
			ry += 22;
			auto f = commsTarget.Faction();
			cs::String sub = cs::Cat(f != nullptr ? f->Name : cs::String("unknown"), "  ·  ", cs::Strings::ToLowerInvariant(ToString(stance)));
			if (them != nullptr) sub = cs::Cat(sub, "  ·  age ", them->AgeAt(c.Jd), ", from ", them->Birthplace, "  ·  next: ", Careers::NextStep(them, c.Jd));
			GUI::Label(Rect(rx, ry, rw, 18), sub, commsSmall);
			ry += 24;
			auto verbs = Comms::Verbs(c, commsTarget);
			float bx = rx, bw = (rw - 8) / 3;
			for (int i = 0; i < verbs.Count(); i++)
			{
				auto br = Rect(bx + (i % 3) * (bw + 4), ry + (i / 3) * 28, bw, 24);
				if (GUI::Button(br, cs::Cat(i + 1, "  ", VerbLabel(verbs[i])), commsButton)) commsClickedVerb = i;
			}
			ry += ((verbs.Count() + 2) / 3) * 28 + 6;
		}
		else
		{
			GUI::Label(Rect(rx, ry, rw, 20), "Pick who to hail", commsTitle);
			ry += 26;
		}

		// The log, newest at the bottom, wrapped, as much as fits.
		float bottom = y + h - 28;
		float lineY = bottom;
		for (int i = commsLog.Count() - 1; i >= 0; i--)
		{
			const auto& l = commsLog[i];
			auto text = cs::Cat(l.Who, ":  ", l.Text);
			auto& style = l.FromSpeaker ? commsUs : commsThem;
			float lh = style->CalcHeight(GUIContent(text), rw);
			if (lineY - lh < ry) break;
			lineY -= lh + 3;
			GUI::Label(Rect(rx, lineY, rw, lh), text, style);
		}

		GUI::Label(Rect(x + 16, y + h - 22, w - 32, 18),
			commsTalking ? "1-9 or click to speak   ·   Left / Backspace other contacts   ·   Esc back   ·   Shift+H close   ·   the craft holds its heading meanwhile"
			: "Up / Down pick   ·   Enter or click to hail   ·   Esc or Shift+H close   ·   colour: their stance toward you",
			commsHint);
	}

	void UnitySpaceApp::EnsureCommsStyles()
	{
		if (commsStylesReady) return;
		commsStylesReady = true;
		// The HUD's look (the dev menu's palette), a touch warmer: a radio, not a console.
		commsPanelTex = UI::Hud::Solid(Color(0.03f, 0.05f, 0.07f, 0.93f), Color(0.45f, 0.85f, 1.0f, 0.7f));
		commsRowTex = UI::Hud::Solid(Color(0.2f, 0.45f, 0.6f, 0.22f), Color::clear());
		commsRowPickedTex = UI::Hud::Solid(Color(0.2f, 0.55f, 0.75f, 0.5f), Color(0.45f, 0.85f, 1.0f, 0.6f));
		commsButtonTex = UI::Hud::Solid(Color(0.1f, 0.15f, 0.2f, 0.9f), Color(0.3f, 0.5f, 0.6f, 0.5f));
		commsButtonHotTex = UI::Hud::Solid(Color(0.1f, 0.3f, 0.42f, 0.95f), Color(0.45f, 0.85f, 1.0f, 0.9f));
		commsHintTex = UI::Hud::Solid(Color(0.03f, 0.05f, 0.08f, 0.55f), Color(0.3f, 0.5f, 0.6f, 0.4f));

		commsPanel = cs::New<GUIStyle>();
		commsPanel->normal.background = commsPanelTex;
		commsPanel->border = RectOffset(2, 2, 2, 2);
		commsTitle = cs::New<GUIStyle>();
		commsTitle->fontSize = 15;
		commsTitle->fontStyle = FontStyle::Bold;
		commsTitle->normal.textColor = Color::white();
		commsTitle->clipping = TextClipping::Clip;
		commsText = cs::New<GUIStyle>();
		commsText->fontSize = 13;
		commsText->normal.textColor = Color::white();
		commsText->clipping = TextClipping::Clip;
		commsSmall = cs::New<GUIStyle>();
		commsSmall->fontSize = 11;
		commsSmall->normal.textColor = UI::Hud::Dim;
		commsSmall->clipping = TextClipping::Clip;
		commsThem = cs::New<GUIStyle>();
		commsThem->fontSize = 13;
		commsThem->wordWrap = true;
		commsThem->normal.textColor = Color(0.85f, 0.93f, 1.0f);
		commsUs = cs::New<GUIStyle>(*commsThem);
		commsUs->normal.textColor = UI::Hud::Accent;
		commsRowStyle = cs::New<GUIStyle>();
		commsRowStyle->normal.background = commsRowTex;
		commsRowPicked = cs::New<GUIStyle>();
		commsRowPicked->normal.background = commsRowPickedTex;
		commsRowPicked->border = RectOffset(1, 1, 1, 1);
		commsButton = cs::New<GUIStyle>();
		commsButton->fontSize = 12;
		commsButton->alignment = TextAnchor::MiddleLeft;
		commsButton->padding = RectOffset(10, 6, 0, 0);
		commsButton->border = RectOffset(2, 2, 2, 2);
		commsButton->clipping = TextClipping::Clip;
		commsButton->normal.background = commsButtonTex;
		commsButton->normal.textColor = Color::white();
		commsButton->hover.background = commsButtonHotTex;
		commsButton->hover.textColor = Color::white();
		commsButton->active.background = commsButtonHotTex;
		commsButton->active.textColor = UI::Hud::Accent;
		commsHint = cs::New<GUIStyle>();
		commsHint->fontSize = 11;
		commsHint->normal.textColor = UI::Hud::Dim;
		commsHint->clipping = TextClipping::Clip;
		(void)commsHintTex;
	}

	void UnitySpaceApp::DestroyCommsTextures()
	{
		commsPanelTex = commsRowTex = commsRowPickedTex = commsButtonTex = commsButtonHotTex = commsHintTex = nullptr;
		commsStylesReady = false;
	}
}
