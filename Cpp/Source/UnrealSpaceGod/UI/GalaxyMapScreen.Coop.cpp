// US-T47: the C# track's Runtime/UI/GalaxyMapScreen.Coop.cs in C++: the galaxy map's strategy (COMMAND) and envoy
// (DIPLOMACY) screens and the polity hail (F12) through the co-op frames. Declarations in GalaxyMapScreen.h. The ported
// screen calls in here only at lines marked US-T47: in single-player every call is false and the original goes on.

#include "UI/GalaxyMapScreen.h"

#include "UnitySpaceApp.h"
#include "Coop/CoopPeer.h"
#include "Coop/CoopWire.h"

#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"

using namespace SpaceGod::Core;
using SpaceGod::Coop::CoopPeer;
namespace Wire = SpaceGod::Coop::Wire;

namespace SpaceGod::UI
{
	namespace
	{
		// ---------------------------------------------------------------- keys every peer reads back the same

		template <class T>
		int IndexOf(const cs::List<cs::Ref<T>>& list, const cs::Ref<T>& item)
		{
			for (int i = 0; i < list.Count(); i++)
				if (list[i] == item) return i;
			return -1;
		}

		template <class T>
		cs::Ref<T> At(const cs::List<cs::Ref<T>>& list, const cs::String& k)
		{
			int i = Wire::ParseI(k);
			return i >= 0 && i < list.Count() ? list[i] : nullptr;
		}

		/// A polity by its place in Polities::Factions (-1: none).
		cs::String KF(UnitySpaceApp* app, const cs::Ref<Faction>& f) { return Wire::I(f == nullptr ? -1 : IndexOf(app->Galaxy->Polities->Factions(), f)); }
		cs::Ref<Faction> F(UnitySpaceApp* app, const cs::String& k) { return At(app->Galaxy->Polities->Factions(), k); }

		/// A system's world by its place in Polities::Worlds (-1: none).
		cs::String KW(UnitySpaceApp* app, const cs::Ref<World>& w) { return Wire::I(w == nullptr ? -1 : IndexOf(app->Galaxy->Polities->Worlds(), w)); }
		cs::Ref<World> W(UnitySpaceApp* app, const cs::String& k) { return At(app->Galaxy->Polities->Worlds(), k); }

		/// A ship as "<world>:<index in its fleet>" ("-": none, or inside a gate).
		cs::String KS(UnitySpaceApp* app, const cs::Ref<Ship>& s)
		{
			auto w = s != nullptr ? app->Galaxy->Polities->Command->WorldOf(s) : nullptr;
			int i = w != nullptr ? IndexOf(w->Fleet->Ships(), s) : -1;
			return w == nullptr || i < 0 ? cs::String("-") : cs::Cat(KW(app, w), ":", Wire::I(i));
		}

		cs::Ref<Ship> S(UnitySpaceApp* app, const cs::String& k)
		{
			auto c = k.find(':');
			if (c == cs::String::npos) return nullptr;
			auto w = W(app, k.substr(0, c));
			return w != nullptr ? At(w->Fleet->Ships(), k.substr(c + 1)) : nullptr;
		}

		template <class T>
		cs::Ref<T> Next(const cs::List<cs::Ref<T>>& all, const cs::Ref<T>& current)
		{
			for (int i = 0; i < all.Count(); i++)
				if (all[i] == current) return all[(i + 1) % all.Count()];
			return all.Count() > 0 ? all[0] : nullptr;
		}
	}

	bool GalaxyMapScreen::CoopSendsText(UnitySpaceApp* app, const cs::String& verb, const cs::String& args)
	{
		if (CoopPeer::Current() == nullptr) return false;
		return app->CoopSends(verb, args);
	}

	// ---------------------------------------------------------------- the speaker

	bool GalaxyMapScreen::CoopSpeaks(const cs::Ref<Faction>& f)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return false;
		if (app->Galaxy->Polities->Command->Player == f) return true; // already the party's
		if (speakSent == f && peer->Frame() - speakSentAt < 120) return true;
		if (!CoopSendsText(app, "speak", KF(app, f))) return false;
		speakSent = f;
		speakSentAt = peer->Frame();
		return true;
	}

	void GalaxyMapScreen::CoopAdoptSpeaker(const cs::List<cs::Ref<Faction>>& states)
	{
		auto player = app->Galaxy->Polities->Command->Player;
		if (CoopPeer::Current() == nullptr || player == nullptr || player == commanded || !states.Contains(player)) return;
		if (speakSent != nullptr && speakSent != player && CoopPeer::Current()->Frame() - speakSentAt < 120) return; // ours is on its way
		commanded = player;
		envoyWith = nullptr;
		warArmed = false;
	}

	cs::Ref<PolityDirectives> GalaxyMapScreen::CoopDirectives(const cs::Ref<StrategicCommand>& command, const cs::Ref<Faction>& f)
	{
		if (CoopPeer::Current() == nullptr) return command->Of(f);
		auto d = command->Find(f);
		return d != nullptr ? d : cs::New<PolityDirectives>(f);
	}

	// ---------------------------------------------------------------- the buttons, sent

	bool GalaxyMapScreen::CoopSendsProduction(const cs::Ref<ProductionFocus>& p)
	{
		return CoopSendsText(app, "production", cs::Cat(KF(app, commanded), " ", Wire::I(IndexOf(ProductionFocus::All(), p))));
	}

	bool GalaxyMapScreen::CoopSendsBuild(const cs::Ref<BuildPlan>& p)
	{
		return CoopSendsText(app, "build", cs::Cat(KF(app, commanded), " ", Wire::I(IndexOf(BuildPlan::All(), p))));
	}

	bool GalaxyMapScreen::CoopSendsResearch(const cs::Ref<ResearchTrack>& t)
	{
		return CoopSendsText(app, "research", cs::Cat(KF(app, commanded), " ", Wire::I(t == nullptr ? -1 : IndexOf(ResearchTrack::All(), t))));
	}

	bool GalaxyMapScreen::CoopSendsOrder(ObjectiveKind kind, const cs::Ref<StarSystem>& target, const cs::Ref<World>& from, const cs::Ref<Ship>& charge)
	{
		return CoopSendsText(app, "objective", cs::Cat(KF(app, commanded), " ", Wire::I(static_cast<int>(commandScope)), " ", KS(app, commandGroup), " ", KW(app, from),
			" ", Wire::I(static_cast<int>(kind)), " ", KW(app, target->World), " ", KS(app, charge)));
	}

	bool GalaxyMapScreen::CoopSendsClear(const cs::Ref<World>& from)
	{
		return CoopSendsText(app, "standdown", cs::Cat(KF(app, commanded), " ", Wire::I(static_cast<int>(commandScope)), " ", KS(app, commandGroup), " ", KW(app, from)));
	}

	bool GalaxyMapScreen::CoopSendsAnswer(const cs::Ref<Proposal>& p, bool accept)
	{
		return CoopSendsText(app, "answer", cs::Cat(Wire::I(IndexOf(app->Galaxy->Polities->Statecraft->Inbox(), p)), " ", accept ? "1" : "0"));
	}

	bool GalaxyMapScreen::CoopSendsBreak(const cs::Ref<Pact>& p)
	{
		return CoopSendsText(app, "breakpact", cs::Cat(Wire::I(IndexOf(app->Galaxy->Polities->Statecraft->Pacts(), p)), " ", KF(app, commanded)));
	}

	bool GalaxyMapScreen::CoopSendsPropose(const cs::Ref<EnvoyOption>& o, const cs::Ref<Faction>& them)
	{
		return CoopSendsText(app, "propose", cs::Cat(Wire::I(IndexOf(Deal::All(), o->Deal)), " ", KF(app, commanded), " ", KF(app, them), " ", Wire::D(o->Amount)));
	}

	bool GalaxyMapScreen::CoopSendsGift(const cs::Ref<Faction>& them, double amount)
	{
		return CoopSendsText(app, "gift", cs::Cat(KF(app, commanded), " ", KF(app, them), " ", Wire::D(amount)));
	}

	bool GalaxyMapScreen::CoopSendsWar(const cs::Ref<Faction>& them)
	{
		return CoopSendsText(app, "declare", cs::Cat(KF(app, commanded), " ", KF(app, them)));
	}

	bool GalaxyMapScreen::CoopSendsHail(UnitySpaceApp* app, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to)
	{
		return CoopSendsText(app, "hail", cs::Cat(KF(app, from), " ", KF(app, to)));
	}

	// ---------------------------------------------------------------- carried out, on every peer

	bool GalaxyMapScreen::CoopExecute(UnitySpaceApp* app, const cs::String& verb, const std::vector<cs::String>& a)
	{
		const auto& polities = app->Galaxy->Polities;
		const auto& command = polities->Command;
		const auto& statecraft = polities->Statecraft;
		double jd = app->Clock->Jd;
		if (verb == "speak")
		{
			command->Player = F(app, a.at(0));
			if (app->CoopActingHere()) speakSent = nullptr;
			return true;
		}
		if (verb == "production")
		{
			auto f = F(app, a.at(0));
			int i = Wire::ParseI(a.at(1));
			if (f != nullptr && i >= 0 && i < ProductionFocus::All().Count()) command->SetProduction(f, ProductionFocus::All()[i]);
			return true;
		}
		if (verb == "build")
		{
			auto f = F(app, a.at(0));
			int i = Wire::ParseI(a.at(1));
			if (f != nullptr && i >= 0 && i < BuildPlan::All().Count()) command->SetBuild(f, BuildPlan::All()[i]);
			return true;
		}
		if (verb == "research")
		{
			auto f = F(app, a.at(0));
			int i = Wire::ParseI(a.at(1));
			if (f != nullptr && i < ResearchTrack::All().Count()) command->SetResearch(f, i < 0 ? nullptr : ResearchTrack::All()[i]);
			return true;
		}
		if (verb == "objective")
		{
			auto f = F(app, a.at(0));
			auto scope = static_cast<CommandScope>(Wire::ParseI(a.at(1)));
			auto group = S(app, a.at(2));
			auto from = W(app, a.at(3));
			if (from == nullptr) from = app->ActiveSystem->World;
			auto kind = static_cast<ObjectiveKind>(Wire::ParseI(a.at(4)));
			auto target = W(app, a.at(5));
			auto charge = S(app, a.at(6));
			int given = 0;
			cs::String why = command->Order(f, scope, group, from, kind, target, jd, given, -1, charge);
			app->ShowToast(!why.empty() ? why : cs::Cat(given, (given == 1 ? " group: " : " groups: "), cs::Strings::ToLowerInvariant(command->Describe(kind, target, -1, charge))));
			return true;
		}
		if (verb == "standdown")
		{
			auto from = W(app, a.at(3));
			if (from == nullptr) from = app->ActiveSystem->World;
			int n = command->Clear(F(app, a.at(0)), static_cast<CommandScope>(Wire::ParseI(a.at(1))), S(app, a.at(2)), from);
			app->ShowToast(n == 0 ? cs::String("No group there was under orders") : cs::Cat(n, (n == 1 ? " group stands down" : " groups stand down")));
			return true;
		}
		if (verb == "answer")
		{
			auto p = At(statecraft->Inbox(), a.at(0));
			if (p == nullptr)
			{
				app->ShowToast("That offer no longer stands");
				return true;
			}
			if (a.at(1) == "1") app->ShowToast(statecraft->Answer(p, true, jd) ? cs::Cat("Agreed: ", p->Describe()) : cs::String("That offer no longer stands"));
			else
			{
				statecraft->Answer(p, false, jd);
				app->ShowToast(cs::Cat("Declined: ", p->Describe()));
			}
			return true;
		}
		if (verb == "breakpact")
		{
			auto p = At(statecraft->Pacts(), a.at(0));
			auto by = F(app, a.at(1));
			if (p == nullptr || by == nullptr) return true;
			statecraft->Break(p, by, jd);
			app->ShowToast(cs::Cat("You break the ", p->Describe(), ": they will not forget it"));
			return true;
		}
		if (verb == "propose")
		{
			auto deal = At(Deal::All(), a.at(0));
			auto from = F(app, a.at(1));
			auto them = F(app, a.at(2));
			if (deal == nullptr || from == nullptr || them == nullptr) return true;
			cs::Ref<Appraisal> answer;
			cs::String why = statecraft->Propose(deal, from, them, jd, answer, Wire::ParseD(a.at(3)));
			app->ShowToast(!why.empty() ? why : (answer == nullptr ? cs::String("Put to them: they will answer") : cs::Cat((answer->Accepted() ? "Agreed: " : "Refused: "), answer->Main())));
			return true;
		}
		if (verb == "gift")
		{
			auto by = F(app, a.at(0));
			auto them = F(app, a.at(1));
			if (by == nullptr || them == nullptr) return true;
			cs::String refused = statecraft->Gift(by, them, Wire::ParseD(a.at(2)), jd);
			app->ShowToast(!refused.empty() ? refused : cs::Cat("The ", them->Name, " receives your gift"));
			return true;
		}
		if (verb == "declare")
		{
			auto by = F(app, a.at(0));
			auto them = F(app, a.at(1));
			if (by == nullptr || them == nullptr) return true;
			cs::String why = statecraft->DeclareWar(by, them, jd);
			app->ShowToast(!why.empty() ? why : cs::Cat("The ", by->Name, " is at war with the ", them->Name));
			return true;
		}
		if (verb == "hail")
		{
			auto from = F(app, a.at(0));
			auto to = F(app, a.at(1));
			cs::String answer = statecraft->Hail(from, to, jd);
			app->ShowToast(answer);
			CoopPeer::Log(cs::Cat("hail: ", answer));
			if (app->CoopActingHere() && Statecraft::Party(to))
			{
				app->EnvoyTo = to;
				app->ShowGalaxyMap = true;
			}
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- the smoke run's clicks (CoopSmoke)

	void GalaxyMapScreen::CoopSmokeSpeak(const cs::Ref<Faction>& f)
	{
		commanded = f;
		commandGroup = nullptr;
		if (!CoopSpeaks(f)) app->Galaxy->Polities->Command->Player = f;
	}

	void GalaxyMapScreen::CoopSmokeProduction()
	{
		const auto& command = app->Galaxy->Polities->Command;
		auto next = Next(ProductionFocus::All(), CoopDirectives(command, commanded)->Production);
		if (!CoopSendsProduction(next)) command->SetProduction(commanded, next);
	}

	void GalaxyMapScreen::CoopSmokeExplore(const cs::Ref<StarSystem>& target)
	{
		commandScope = CommandScope::Polity;
		Give(app->Galaxy->Polities->Command, ObjectiveKind::Explore, target, nullptr, app->Clock->Jd);
	}

	void GalaxyMapScreen::CoopSmokeGift(const cs::Ref<Faction>& them)
	{
		double gift = Statecraft::SuggestedGift(commanded);
		if (CoopSendsGift(them, gift)) return;
		cs::String refused = app->Galaxy->Polities->Statecraft->Gift(commanded, them, gift, app->Clock->Jd);
		app->ShowToast(!refused.empty() ? refused : cs::Cat("The ", them->Name, " receives your gift"));
	}
}
