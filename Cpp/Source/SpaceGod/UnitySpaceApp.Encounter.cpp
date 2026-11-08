// US-T239: an encounter as a Prose conversation on the comms panel (declarations in UnitySpaceApp.Comms.inl; the rules in
// Core's Lore/Encounters.h; what writers write in docs/PROSE.md, "Encounters").
//
// The conversation only talks. Its lines are filled with the game's own numbers, its choices are offered only while the verb
// their tag names is on offer (Comms::Verbs), and a tagged choice says that verb through Comms::Apply, the same call the plain
// button makes, so what happens (a trade, a threat obeyed or answered with guns, terms taken) is the simulation's. Only the
// panel's log reads differently: the player's line is the choice's words, and "$outcome" lets the text react to the outcome.

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Comms/Comms.h"
#include "Fleet.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "GameLore.h"
#include "Law/Law.h"
#include "Lore/Encounters.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// The most nodes one step walks through without a choice (a text that loops on itself stops here).
		constexpr int MaxNodesPerStep = 24;

		bool EndOfTalk(const cs::String& next) { return next.empty() || next[0] == '$'; }
	}

	void UnitySpaceApp::EncounterEnd()
	{
		encounterText = nullptr;
		encounterKey = cs::String();
		encounterNodeId = cs::String();
		encounterParty = CommsParty();
	}

	cs::String UnitySpaceApp::EncounterWho(const CommsContext& c, bool player)
	{
		auto chars = World() != nullptr ? Characters::Of(World()) : nullptr;
		if (player)
		{
			auto me = c.Speaker != nullptr && chars != nullptr ? chars->Find(c.Speaker) : nullptr;
			if (me != nullptr) return me->Name;
			if (c.Speaker != nullptr) return c.Speaker->Name;
			return c.SpeakerStation != nullptr ? cs::Cat(c.SpeakerStation->Name, " Control") : cs::String("you");
		}
		auto them = encounterParty.Ship != nullptr && chars != nullptr ? chars->Find(encounterParty.Ship) : nullptr;
		if (them != nullptr) return them->Name;
		return encounterParty.IsStation() ? cs::Cat(encounterParty.Station->Name, " Control") : encounterParty.Name();
	}

	cs::String UnitySpaceApp::EncounterFill(const CommsContext& c, const cs::String& line)
	{
		auto values = Encounters::Values(c, encounterParty);
		values["system"] = ActiveSystem != nullptr ? ActiveSystem->Name : cs::String();
		// {entity:<id>} reads the thing's own name when it is one the panel can rebuild the id of (LoreKeys): the people met,
		// the factions, the active system's bodies, ports and hulls. Anything else reads its id's words.
		auto world = World();
		const cs::String system = ActiveSystem != nullptr ? ActiveSystem->Name : cs::String();
		auto entityName = [world, system, this](const cs::String& id) -> cs::String
		{
			if (world == nullptr) return cs::String();
			if (id == LoreKeys::System(system)) return system;
			if (world->Law != nullptr && world->Law->Pilots != nullptr)
				for (const auto& p : world->Law->Pilots->All)
					if (p != nullptr && LoreKeys::Pilot(p->Name) == id) return p->Name;
			for (const auto& f : world->Fleet->Factions())
				if (f != nullptr && LoreKeys::Faction(f->Name) == id) return f->Name;
			for (const auto& s : world->Economy->Settlements)
				if (s != nullptr && LoreKeys::Settlement(system, s->Name) == id) return s->Name;
			for (const auto& b : Catalog()->Bodies())
				if (b->IsVisible() && LoreKeys::Body(b->Kind, system, b->Name) == id) return b->Name;
			for (const auto& s : world->Fleet->Ships())
				if (s != nullptr && !s->Destroyed && LoreKeys::Ship(system, s->Name) == id) return s->Name;
			return cs::String();
		};
		return Encounters::Fill(line, values, entityName);
	}

	bool UnitySpaceApp::EncounterStart(const CommsContext& c, const CommsParty& to, cs::List<CommsLine>& lines, CommsLine* greeting)
	{
		EncounterEnd();
		if (to.IsNone()) return false;
		auto kind = Encounters::KindOf(c, to);
		const cs::String system = ActiveSystem != nullptr ? ActiveSystem->Name : cs::String();
		auto pilots = World() != nullptr && World()->Law != nullptr ? World()->Law->Pilots : nullptr;
		auto pilot = to.Ship != nullptr && pilots != nullptr ? pilots->Find(to.Ship) : nullptr;
		auto faction = to.Faction();
		auto keys = Encounters::Keys(kind, pilot != nullptr ? LoreKeys::Pilot(pilot->Name) : cs::String(),
			faction != nullptr ? LoreKeys::Faction(faction->Name) : cs::String());
		const LoreText* text = Encounters::Find(GameLore(), keys);
		if (text == nullptr) return false;
		EncounterNode open;
		if (!Encounters::Node(*text, Encounters::Start(*text), open)) return false;
		encounterText = text;
		encounterKey = text->Key;
		encounterParty = to;
		encounterNodeId = open.Id;
		bool player = Encounters::IsPlayerSpeaker(open.Speaker, pilot != nullptr && c.Speaker == to.Ship ? LoreKeys::Pilot(pilot->Name) : cs::String());
		cs::String said = EncounterFill(c, open.Line);
		if (greeting != nullptr && !player)
		{
			// The opening line is their answer to the hail: it stands in for the generated flavour; the rider stays (US-T238).
			greeting->Text = Comms::WithRider(said, greeting->Rider);
		}
		else if (!said.empty()) lines.Add({ EncounterWho(c, player), said, player });
		if (open.Choices.Count() == 0)
		{
			if (EndOfTalk(open.Next)) EncounterEnd();
			else EncounterGo(c, open.Next, lines);
		}
		return true;
	}

	void UnitySpaceApp::EncounterGo(const CommsContext& c, const cs::String& id, cs::List<CommsLine>& lines)
	{
		cs::String at = id;
		for (int step = 0; step < MaxNodesPerStep && encounterText != nullptr; step++)
		{
			EncounterNode node;
			if (EndOfTalk(at) || !Encounters::Node(*encounterText, at, node))
			{
				EncounterEnd();
				return;
			}
			encounterNodeId = node.Id;
			bool player = Encounters::IsPlayerSpeaker(node.Speaker, cs::String());
			cs::String said = EncounterFill(c, node.Line);
			if (!said.empty()) lines.Add({ EncounterWho(c, player), said, player });
			if (node.Choices.Count() > 0) return; // the player's turn
			at = node.Next;
		}
		EncounterEnd();
	}

	bool UnitySpaceApp::EncounterNow(const CommsContext& c, EncounterNode& node, cs::List<int>& offered)
	{
		offered.Clear();
		if (encounterText == nullptr) return false;
		// The talk is with one party: picking someone else on the panel (or them leaving the list) ends it.
		if (!commsTalking || !(commsTarget == encounterParty) || !Encounters::Node(*encounterText, encounterNodeId, node))
		{
			EncounterEnd();
			return false;
		}
		offered = Encounters::Offered(node, Comms::Verbs(c, encounterParty));
		return true;
	}

	void UnitySpaceApp::EncounterChoose(int index)
	{
		auto c = CommsHere();
		EncounterNode node;
		cs::List<int> offered;
		if (!EncounterNow(c, node, offered)) return;
		if (index == offered.Count())
		{
			// Past the text's choices: back to the plain verbs, the conversation left where it was.
			EncounterEnd();
			return;
		}
		if (index < 0 || index > offered.Count()) return;
		const EncounterChoice& ch = node.Choices[offered[index]];
		const cs::String words = EncounterFill(c, ch.Text);
		cs::List<CommsLine> lines;
		CommsOutcome outcome = CommsOutcome::None;
		if (ch.HasAction && ch.Action == CommsVerb::SignOff)
		{
			lines.Add({ EncounterWho(c, true), words, true });
			for (const auto& l : lines) commsLog.Add(l);
			EncounterEnd();
			CommsSay(CommsVerb::SignOff);
			return;
		}
		if (ch.HasAction)
		{
			// The game does it: the same command the verb's button sends (Core::CommsCommand's two numbers), the same answer.
			auto r = Comms::Apply(c, encounterParty.Key(), static_cast<int>(ch.Action));
			outcome = r.Outcome;
			bool replaced = false;
			for (auto& l : r.Lines)
				if (l.FromSpeaker)
				{
					l.Text = words; // the player's words are the choice's; what came of it is the simulation's
					replaced = true;
					break;
				}
			if (!replaced) lines.Add({ EncounterWho(c, true), words, true });
			for (const auto& l : r.Lines) lines.Add(l);
			if (!r.Toast.empty()) ShowToast(r.Toast);
			commsContacts = Comms::Contacts(c);
			if (r.Battle != nullptr)
			{
				for (const auto& l : lines) commsLog.Add(l);
				while (commsLog.Count() > 40) commsLog.RemoveAt(0);
				EncounterEnd();
				CloseComms(); // a fight has started: the hands go back to the helm
				return;
			}
		}
		else lines.Add({ EncounterWho(c, true), words, true });

		cs::String next = ch.Next;
		if (next == "$outcome") next = Encounters::OutcomeNode(outcome);
		EncounterGo(c, next, lines);
		for (const auto& l : lines) commsLog.Add(l);
		while (commsLog.Count() > 40) commsLog.RemoveAt(0);
	}
}
