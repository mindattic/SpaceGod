// US-T138: the template story teller, the default and the fallback for every beat of a named pilot's life (Comms/Pilots.h).
//
// A large pool of pregenerated sentences, per beat, told of the pilot (third person) and in their own voice (first person,
// for a hail), and the motives the rationalization rule gives the acts a pilot owns once the SpaceGod has left them. A beat's
// seed picks the sentence, so a beat always reads the same. Placeholders: {name} {first} {they} {them} {their} {They}
// {Their} {callsign} {age} and any of the beat's own slots ({place} {faction} {ship} {class} {battle} {rival} {victim}
// {good} {whom} {title} {crime} {why} {tonnes}).

#include "Comms/Pilots.h"

#include <cstdlib>
#include <vector>

namespace SpaceGod::Core
{
	namespace
	{
		using Pool = std::vector<const char*>;

		struct BeatPools
		{
			Pool Third, First;
		};

		const BeatPools& PoolsFor(LifeEventKind k)
		{
			static const BeatPools born{
				{ "{name} was born on {place}.", "{name} grew up on {place}, under {faction}'s flag.", "{place} raised {name}; {they} left it as soon as {they} could.",
				  "Born on {place}, {name} learned to fly before {they} learned to drive.", "{name} came into the world on {place}, the youngest of five.",
				  "{place} was home to {name} once. Nobody there would know {them} now.", "{name} was born on {place} in a bad year for its harvests.",
				  "{name} spent a childhood on {place} watching the traffic lights climb out of the well." },
				{ "I was born on {place}.", "{place}'s where I grew up. {faction} country.", "I'm from {place}, for what that's worth.",
				  "Born on {place}. Couldn't wait to get off it.", "I grew up on {place}, counting the ships going over.",
				  "{place}. That's home, or it was." } };
			static const BeatPools joined{
				{ "At eighteen {name} signed on with {faction}.", "{name} joined {faction} young and never looked back.", "{faction} took {name} on as crew.",
				  "{name} went to work for {faction}, hauling whatever needed hauling.", "{name} talked {their} way aboard a {faction} hull at seventeen.",
				  "{faction}'s recruiters found {name} on the docks; {they} said yes before they finished." },
				{ "I signed on with {faction} the day I was old enough.", "{faction} gave me my first berth.", "I've worked for {faction} my whole life.",
				  "Joined {faction} young. Best and worst choice I ever made.", "I talked my way aboard a {faction} ship and stayed." } };
			static const BeatPools turned{
				{ "{name} left {place} with nothing and fell in with {faction}.", "{name} turned pirate after a contract went bad, and found {faction}.",
				  "{faction} took {name} in when the lawful lanes would not.", "Debts on {place} drove {name} out to the dark rocks and {faction}.",
				  "{name} walked off a freighter at a haven one day and never walked back; {faction} became {their} crew.",
				  "{name} chose {faction} over a cell on {place}." },
				{ "I left {place} with empty pockets. {faction} filled them.", "The law never did a thing for me. {faction} did.",
				  "A bad contract, then a worse debt, then {faction}. That's the short of it.", "I walked off a hauler at a haven and never looked back.",
				  "{faction} asked fewer questions than the navy." } };
			static const BeatPools flew{
				{ "{name} flew the {class} {ship}.", "{name} took the helm of the {ship}, a {class}.", "For a while {name} flew the {ship}.",
				  "{name} knew every rattle of the {ship}'s hull.", "The {ship} was {name}'s for a season or two.", "{name} learned the {class} trade in the {ship}." },
				{ "I flew the {ship} for a while. Good {class}.", "The {ship}. Knew her like my own hands.", "I had the {ship}'s helm, a {class}.",
				  "I cut my teeth on the {ship}.", "The {ship} and I go back." } };
			static const BeatPools survived{
				{ "{name} came through {battle} aboard the {ship}.", "{name} lived through {battle}; plenty didn't.", "{battle} left scars on the {ship}, and on {name}.",
				  "{name} still talks about {battle}, if pressed.", "At {battle} {name} kept the {ship} flying when it should have broken.",
				  "{name} was in the thick of {battle} and came out of it." },
				{ "I was at {battle}. Don't ask.", "{battle}. I made it out. Not everyone did.", "We came through {battle}, the {ship} and me.",
				  "You hear about {battle}? I was there.", "I still dream about {battle} some nights." } };
			static const BeatPools lost{
				{ "{name} got out of the {ship} in a pod when she went up.", "The {ship} was lost under {name}; {they} walked away.",
				  "{name} watched the {ship} burn from a lifepod.", "{name} lost the {ship} and half {their} friends with her." },
				{ "I lost the {ship}. Got out in a pod.", "The {ship} went up under me. I still don't know how I got clear.",
				  "I watched the {ship} burn from a pod window.", "Lost the {ship}. Lost good people with her." } };
			static const BeatPools grudge{
				{ "{name} has never forgiven {rival}.", "{name} carries a grudge against {rival}.", "Say the name {rival} near {name} and watch {their} face.",
				  "{name} would cross the system to settle things with {rival}.", "{rival} wronged {name} once. {name} remembers.", "{name} keeps a list. {rival} is on it." },
				{ "{rival}. I've got a score to settle there.", "Don't talk to me about {rival}.", "If I ever get {rival} in my sights...",
				  "{rival} owes me, and I collect.", "I don't forget what {rival} did." } };
			static const BeatPools crime{
				{ "{faction} has {name} on record for {crime}.", "{name} was charged with {crime} against {victim}.", "{name}'s record shows {crime}.",
				  "{faction} posted a bounty on {name} for {crime}." },
				{ "{faction} says I'm guilty of {crime}. They would.", "There's a little matter of {crime} on my record.",
				  "{crime}, they call it. I call it business.", "I've a bounty for {crime}. Don't let it worry you." } };
			static const BeatPools promoted{
				{ "{name} made {title}.", "{name} rose to {title}.", "They made {name} {title}, and {they} earned it.", "{name} was named {title}.",
				  "{name} became {title} before anyone expected." },
				{ "They made me {title}.", "I'm {title} now. Took long enough.", "Made {title}. Still getting used to it.", "{title}. That's me now." } };
			static const BeatPools demoted{
				{ "{name} was broken to {title}.", "{name} lost {their} rank.", "{name} fell to {title}." },
				{ "They broke me to {title}.", "Lost my rank. Long story.", "Back to {title}. It happens." } };
			static const BeatPools detained{
				{ "{name} was taken alive and turned in to {faction} at {place}.", "{name} was locked up in {place}'s detention centre.",
				  "{faction} held {name} at {place}.", "{name} went to the cells at {place}." },
				{ "They took me alive and handed me to {faction} at {place}.", "I did my time at {place}.", "{place}'s cells. I know every crack in the ceiling.",
				  "{faction} locked me up at {place}. Fair enough, maybe." } };
			static const BeatPools rehab{
				{ "{name} was let into {place}'s rehabilitation programme.", "{name} turned a corner at {place}.", "{place}'s counsellors got through to {name}.",
				  "{name} worked off {their} sentence at {place}, and meant it." },
				{ "Rehabilitation at {place}. I took it seriously.", "Somebody at {place} gave me a chance. I took it.", "I got straight at {place}." } };
			static const BeatPools graduated{
				{ "{name} came out of fighter school at {place} a {title}.", "{name} graduated from flight school at {place}.",
				  "{name} won {their} wings at {place}.", "{name} finished top half of {their} class at {place}." },
				{ "I graduated fighter school at {place}.", "Got my wings at {place}.", "Flight school at {place}. Hardest thing I ever did." } };
			static const BeatPools command{
				{ "{name} was given command of the {ship}, a {class} of {their} own.", "{name} took command of the {class} {ship}.",
				  "The {ship} became {name}'s command.", "{name} walked onto the bridge of the {ship} as its captain." },
				{ "They gave me the {ship}. My own {class}.", "I command the {ship} now.", "The {ship}'s mine. Captain's chair and all.",
				  "From the cells to the bridge of the {ship}. Who'd have thought." } };
			static const BeatPools betrayal{
				{ "{name} turned on {whom}.", "{name} betrayed {whom}.", "{name} put a knife in {whom}'s back.", "{name} sold out {whom}.",
				  "{name} broke with {whom}, guns first." },
				{ "I turned on {whom}.", "I did for {whom}.", "{whom} had it coming. I made sure it came.", "I broke with {whom}." } };
			static const BeatPools robbed{
				{ "{name} took a cargo of {good} off the {victim}.", "{name} robbed the {victim} of {tonnes} t of {good}.", "{name} relieved the {victim} of its {good}.",
				  "The {victim} gave up its {good} to {name}.", "{name} held up the {victim} for its {good}." },
				{ "I took the {victim}'s {good}.", "The {victim} handed over {tonnes} t of {good}. To me.", "I relieved the {victim} of some {good}.",
				  "I robbed the {victim}. Clean job." } };
			static const BeatPools wasRobbed{
				{ "{name} was robbed by {rival}.", "{rival} took {name}'s cargo of {good}.", "{name} gave up {their} {good} to {rival}.",
				  "{name} handed {rival} the {good} rather than die for it." },
				{ "{rival} robbed me.", "I gave {rival} my {good}. Rather that than a funeral.", "{rival} took my cargo. I haven't forgotten." } };
			static const BeatPools surrendered{
				{ "{name} struck {their} colours to {rival}.", "{name} surrendered to {rival} rather than die.", "{name} hailed {rival} for terms." },
				{ "I struck my colours to {rival}.", "I surrendered to {rival}. Live to fly another day.", "I asked {rival} for terms." } };
			static const BeatPools spared{
				{ "{name} let the {victim} go.", "{name} spared the {victim}.", "{name} showed the {victim} mercy." },
				{ "I let the {victim} go.", "I spared the {victim}.", "I gave the {victim} its life back." } };
			static const BeatPools threatened{
				{ "{name} threatened the {victim}.", "{name} hailed the {victim} with guns warm.", "{name} told the {victim} to hand over its cargo or else." },
				{ "I told the {victim} to pay up.", "I put the fear into the {victim}.", "I warned the {victim} off." } };
			static const BeatPools traded{
				{ "{name} traded {good} with the {victim}.", "{name} did a deal with the {victim} for {good}.", "{name} bought {good} off the {victim}." },
				{ "I did business with the {victim}.", "Bought some {good} off the {victim}.", "A fair trade with the {victim}." } };
			static const BeatPools met{ { "{name} first crossed paths with the lanes' strangers aboard the {ship}." }, { "We met aboard the {ship}." } };
			static const BeatPools owned{
				{ "{name} did what {they} did.", "{name} made a choice that day." },
				{ "I made a choice.", "I did what I had to." } };
			static const BeatPools crowned{
				{ "{name} was hailed {title} by the clans.", "The clans bent the knee to {name}, {title}.", "{name} rules the dark rocks now: {title}.",
				  "Every raider in the system answers to {name}, {title}." },
				{ "The clans call me {title}.", "{title}. They kneel, or they float.", "I rule the dark rocks now." } };
			static const BeatPools salvaged{
				{ "{name} tractored {tonnes} t of {good} out of the drift where the {victim} died.", "{name} hauled the {victim}'s spilled {good} aboard.",
				  "{name} swept the {victim}'s cargo pods into {their} hold.", "{name} picked {good} out of the wreckage of the {victim}." },
				{ "I tractored the {victim}'s {good} aboard. Finders keepers.", "{tonnes} t of {good}, drifting free. Not anymore.",
				  "The {victim} won't miss its {good}. I pulled it in.", "Dead ships don't need cargo." } };
			static const BeatPools discovered{
				{ "{name} was the first to chart {place}, from the {ship}.", "{place} went on the maps because {name} flew the {ship} out to it.",
				  "Nobody had laid eyes on {place} before {name} did.", "{name}'s survey of {place} sold to the cartographers under {their} own name.",
				  "The charts still credit {place} to {name}." },
				{ "I was first to {place}. Nobody can take that off me.", "{place}? I charted it. Look it up.",
				  "The {ship} and I found {place} before anyone.", "First eyes on {place} were mine." } };

			switch (k)
			{
			case LifeEventKind::Born: return born;
			case LifeEventKind::Joined: return joined;
			case LifeEventKind::TurnedPirate: return turned;
			case LifeEventKind::FlewShip: return flew;
			case LifeEventKind::SurvivedBattle: return survived;
			case LifeEventKind::LostShip: return lost;
			case LifeEventKind::Grudge: return grudge;
			case LifeEventKind::Crime: return crime;
			case LifeEventKind::Promoted: return promoted;
			case LifeEventKind::Demoted: return demoted;
			case LifeEventKind::Detained: return detained;
			case LifeEventKind::Rehabilitated: return rehab;
			case LifeEventKind::Graduated: return graduated;
			case LifeEventKind::GivenCommand: return command;
			case LifeEventKind::Betrayal: return betrayal;
			case LifeEventKind::Robbed: return robbed;
			case LifeEventKind::WasRobbed: return wasRobbed;
			case LifeEventKind::Surrendered: return surrendered;
			case LifeEventKind::Spared: return spared;
			case LifeEventKind::Threatened: return threatened;
			case LifeEventKind::Traded: return traded;
			case LifeEventKind::Met: return met;
			case LifeEventKind::OwnedAct: return owned;
			case LifeEventKind::CrownedKing: return crowned;
			case LifeEventKind::Salvaged: return salvaged;
			case LifeEventKind::Discovered: return discovered;
			}
			return owned;
		}

		// ---------------------------------------------------------------- motives (the rationalization rule)
		// By what was done, then by what drives the pilot most. Always certain, always their own: never a word of doubt.

		enum class Drive { Greed, Spite, Courage, Honour, Warmth, Count };

		const Pool& MotivesFor(LifeEventKind k, Drive d)
		{
			static const Pool robGreed{ "A hold full of {good} and nobody watching. I took my chance.", "Credits don't earn themselves. That {good} was worth a fortune.",
				"I saw the {victim} wallowing along fat with {good} and thought: why not me?" };
			static const Pool robSpite{ "The {victim}'s owners have bled this system dry for years. I bled them back.", "{victim} had it coming, the way their sort treat people like me.",
				"They looked down on us from the day I signed on. Now they know my name." };
			static const Pool robCourage{ "Somebody had to show the lanes aren't theirs alone.", "I wanted to see if I had the nerve. I did." };
			static const Pool robHonour{ "The crew hadn't been paid in months. I put food on the table.", "That cargo was stolen from the colonies long before I took it." };
			static const Pool robWarmth{ "Nobody got hurt. That's how you do it right.", "My people needed it more than the {victim}'s shareholders." };

			static const Pool threatGreed{ "A warning shot's cheaper than a fight, and it pays the same.", "Talk first. Talk's cheap and the {victim} was rich." };
			static const Pool threatSpite{ "The {victim} needed taking down a peg.", "I wanted the {victim} to feel what it's like for once." };
			static const Pool threatCourage{ "You don't get respect asking nicely.", "I meant every word. They knew it." };
			static const Pool threatHonour{ "I gave them a fair warning, which is more than most would.", "Better a threat than a broadside. I'm not a butcher." };
			static const Pool threatWarmth{ "Scaring them off saved lives on both sides.", "I'd rather they ran than died." };

			static const Pool betrayGreed{ "{whom} kept the best of every haul. I'd had enough.", "{whom} was never going to share. So I took my share." };
			static const Pool betraySpite{ "I'd had enough of {whom}'s cowardice.", "{whom} had it coming for years. I just got there first." };
			static const Pool betrayCourage{ "{whom} would have got us all killed. Somebody had to act.", "{whom} lost the nerve for it. I hadn't." };
			static const Pool betrayHonour{ "{whom} broke faith first. I only finished it.", "What {whom} was doing was wrong, and I stopped it." };
			static const Pool betrayWarmth{ "I did it for the crew. {whom} never cared what happened to them.", "Somebody had to look after the youngsters. {whom} wouldn't." };

			static const Pool fightGreed{ "There was a bounty on them. Business is business.", "Winners take the salvage." };
			static const Pool fightSpite{ "They'd have done the same to me, and they'd have enjoyed it.", "I've owed that lot a beating for years." };
			static const Pool fightCourage{ "I don't run. Never have.", "Somebody had to hold the line, so I held it." };
			static const Pool fightHonour{ "It was a fair fight and I won it.", "They fired first. I finished it." };
			static const Pool fightWarmth{ "I had people to protect.", "Every shot I fired was for my crew." };

			static const Pool tradeGreed{ "I knew a bargain when I saw one.", "Buy low. Always buy low." };
			static const Pool tradeSpite{ "I made them pay through the nose. They can afford it." };
			static const Pool tradeCourage{ "I trust my gut on a deal." };
			static const Pool tradeHonour{ "A fair price is a fair price.", "My word's good. So was the deal." };
			static const Pool tradeWarmth{ "Good people, that crew. Glad to help them out." };

			static const Pool mercyGreed{ "A dead ship pays no ransom.", "They're worth more alive." };
			static const Pool mercySpite{ "Let them live with it. That's worse." };
			static const Pool mercyCourage{ "No glory in shooting a ship that's struck." };
			static const Pool mercyHonour{ "You don't fire on a ship that's struck its colours. Not ever.", "There are rules, even out here." };
			static const Pool mercyWarmth{ "There were families on that ship.", "Mercy costs nothing." };

			static const Pool anyGreed{ "It paid. That's all that matters." };
			static const Pool anySpite{ "They had it coming." };
			static const Pool anyCourage{ "Somebody had to." };
			static const Pool anyHonour{ "It was the right thing to do." };
			static const Pool anyWarmth{ "I did it for my people." };

			auto pick = [d](const Pool& g, const Pool& s, const Pool& c, const Pool& h, const Pool& w) -> const Pool&
			{
				switch (d)
				{
				case Drive::Greed: return g;
				case Drive::Spite: return s;
				case Drive::Courage: return c;
				case Drive::Honour: return h;
				default: return w;
				}
			};
			switch (k)
			{
			case LifeEventKind::Robbed: return pick(robGreed, robSpite, robCourage, robHonour, robWarmth);
			case LifeEventKind::Threatened: return pick(threatGreed, threatSpite, threatCourage, threatHonour, threatWarmth);
			case LifeEventKind::Betrayal: return pick(betrayGreed, betraySpite, betrayCourage, betrayHonour, betrayWarmth);
			case LifeEventKind::SurvivedBattle:
			case LifeEventKind::OwnedAct: return pick(fightGreed, fightSpite, fightCourage, fightHonour, fightWarmth);
			case LifeEventKind::Traded: return pick(tradeGreed, tradeSpite, tradeCourage, tradeHonour, tradeWarmth);
			case LifeEventKind::Spared: return pick(mercyGreed, mercySpite, mercyCourage, mercyHonour, mercyWarmth);
			default: return pick(anyGreed, anySpite, anyCourage, anyHonour, anyWarmth);
			}
		}

		Drive Strongest(const cs::Ref<NamedPilot>& p)
		{
			double v[] = { p->Greed, p->Spite, p->Courage, p->Honour, p->Warmth };
			int best = 0;
			for (int i = 1; i < 5; i++)
				if (v[i] > v[best]) best = i;
			return static_cast<Drive>(best);
		}

		cs::String Fill(const cs::String& text, const cs::Ref<NamedPilot>& p, const cs::Ref<LifeEvent>& e)
		{
			cs::String out;
			out.reserve(text.size() + 32);
			for (size_t i = 0; i < text.size(); i++)
			{
				if (text[i] != '{')
				{
					out += text[i];
					continue;
				}
				auto close = text.find('}', i);
				if (close == cs::String::npos)
				{
					out += text.substr(i);
					break;
				}
				auto key = text.substr(i + 1, close - i - 1);
				i = close;
				bool she = p->Pronoun == 0;
				cs::String v;
				if (key == "name")
				{
					// In full (with the callsign) where their story starts; by first name after that, as people tell it.
					bool first = e == nullptr || p->Story.Count() == 0 || p->Story[0] == e;
					auto space = p->Name.find(' ');
					if (!first) v = p->Name.substr(0, space);
					else v = p->Callsign.empty() || space == cs::String::npos ? p->Name : cs::Cat(p->Name.substr(0, space), " \"", p->Callsign, "\"", p->Name.substr(space));
				}
				else if (key == "first") v = p->Name.substr(0, p->Name.find(' '));
				else if (key == "they") v = she ? "she" : "he";
				else if (key == "They") v = she ? "She" : "He";
				else if (key == "them") v = she ? "her" : "him";
				else if (key == "their") v = she ? "her" : "his";
				else if (key == "Their") v = she ? "Her" : "His";
				else if (key == "callsign") v = p->Callsign;
				else if (key == "age") v = cs::Str(p->AgeAt(e != nullptr ? e->Jd : p->MetJd));
				else if (e != nullptr) v = e->Slot(key);
				if (v.empty())
				{
					// A missing detail reads as something vague rather than a gap.
					if (key == "faction") v = "the free traders";
					else if (key == "place") v = "a world nobody remembers";
					else if (key == "ship" || key == "victim") v = "a ship";
					else if (key == "rival" || key == "whom") v = "someone";
					else if (key == "good") v = "cargo";
					else if (key == "battle") v = "a fight off the lanes";
					else if (key == "class") v = "hull";
					else if (key == "title") v = Careers::Title(p->Track, p->Rank);
					else if (key == "tonnes") v = "a few";
				}
				out += v;
			}
			return out;
		}
	}

	int TemplateStoryTeller::PoolSize()
	{
		int n = 0;
		for (int k = 0; k <= static_cast<int>(LifeEventKind::Discovered); k++)
		{
			const auto& pools = PoolsFor(static_cast<LifeEventKind>(k));
			n += static_cast<int>(pools.Third.size() + pools.First.size());
		}
		for (auto k : { LifeEventKind::Robbed, LifeEventKind::Threatened, LifeEventKind::Betrayal, LifeEventKind::OwnedAct, LifeEventKind::Traded,
				 LifeEventKind::Spared, LifeEventKind::Born })
			for (int d = 0; d < static_cast<int>(Drive::Count); d++) n += static_cast<int>(MotivesFor(k, static_cast<Drive>(d)).size());
		return n;
	}

	cs::String TemplateStoryTeller::Motive(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& act)
	{
		if (pilot == nullptr || act == nullptr) return cs::String();
		const auto& pool = MotivesFor(act->Kind, Strongest(pilot));
		if (pool.empty()) return cs::String();
		auto key = act->Slot("motive");
		uint32_t pickSeed = key.empty() ? static_cast<uint32_t>(act->Seed) : static_cast<uint32_t>(std::strtoul(key.c_str(), nullptr, 10));
		return Fill(pool[Characters::Mix(pickSeed) % pool.size()], pilot, act);
	}

	cs::String TemplateStoryTeller::Tell(const StoryBeat& beat)
	{
		auto p = beat.Pilot;
		auto e = beat.Event;
		if (p == nullptr || e == nullptr) return cs::String();
		const auto& pools = PoolsFor(e->Kind);
		const auto& pool = beat.FirstPerson && !pools.First.empty() ? pools.First : pools.Third;
		if (pool.empty()) return cs::String();
		auto line = Fill(pool[static_cast<size_t>(e->Seed) % pool.size()], p, e);
		// An act done in their body and owned since: told as their own, with their reason, and nothing to doubt.
		if (e->Owned && !p->Held && !e->Slot("motive").empty())
		{
			auto why = Motive(p, e);
			if (!why.empty()) line = beat.FirstPerson ? cs::Cat(line, " ", why) : cs::Cat(line, " As ", p->Pronoun == 0 ? "she" : "he", " tells it: \"", why, "\"");
		}
		return line;
	}
}
