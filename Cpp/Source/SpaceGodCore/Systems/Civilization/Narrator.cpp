// US-T274/US-T275: words for the Civilization layer's events (see Narrator.h).

#include "Systems/Civilization/Narrator.h"

#include "Economy/Settlement.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

#include <initializer_list>

namespace SpaceGod::Core
{
	namespace
	{
		Narrator::LineSource& Source()
		{
			static Narrator::LineSource source;
			return source;
		}

		cs::List<cs::String> L(std::initializer_list<const char*> lines)
		{
			cs::List<cs::String> r;
			for (auto* s : lines) r.Add(s);
			return r;
		}

		void ReplaceAll(cs::String& s, const cs::String& what, const cs::String& with)
		{
			if (what.empty()) return;
			std::size_t at = 0;
			while ((at = s.find(what, at)) != cs::String::npos)
			{
				s.replace(at, what.size(), with);
				at += with.size();
			}
		}

		cs::String Kebab(const char* name)
		{
			cs::String r;
			for (const char* p = name; *p; ++p)
			{
				char c = *p;
				if (c >= 'A' && c <= 'Z')
				{
					if (!r.empty()) r += '-';
					r += static_cast<char>(c - 'A' + 'a');
				}
				else r += c;
			}
			return r;
		}
	}

	void Narrator::SetSource(LineSource source) { Source() = std::move(source); }
	bool Narrator::HasSource() { return static_cast<bool>(Source()); }

	cs::String Narrator::Slug(SocialKind k) { return Kebab(ToString(k)); }
	cs::String Narrator::Key(SocialKind k) { return cs::Cat("spacegod/social-", Slug(k)); }
	cs::String Narrator::NamesKey(OrgKind k) { return cs::Cat("spacegod/names-", Kebab(ToString(k))); }

	cs::String Narrator::Money(double credits)
	{
		double a = cs::Math::Abs(credits);
		if (a >= 1e9) return cs::Cat(cs::Fmt(credits / 1e9, "0.#"), " billion credits");
		if (a >= 1e6) return cs::Cat(cs::Fmt(credits / 1e6, "0.#"), " million credits");
		return cs::Cat(cs::Fmt(credits, "#,0"), " credits");
	}

	FeedKind Narrator::FeedOf(SocialKind k)
	{
		switch (k)
		{
			case SocialKind::Friendship: case SocialKind::FallingOut: case SocialKind::Affair: case SocialKind::BoardPost:
				return FeedKind::Message;
			case SocialKind::Endorsement: case SocialKind::AdWar: case SocialKind::TouristBoom:
				return FeedKind::Commercial;
			case SocialKind::Rumour: case SocialKind::Propaganda:
				return FeedKind::Spam;
			default:
				return FeedKind::News;
		}
	}

	bool Narrator::Notable(SocialKind k)
	{
		switch (k)
		{
			case SocialKind::Scandal: case SocialKind::FeudDeclared: case SocialKind::HouseSplit: case SocialKind::CelebrityScandal:
			case SocialKind::ElectionWon: case SocialKind::ElectionRigged: case SocialKind::Coup: case SocialKind::CoupFailed:
			case SocialKind::SuccessionCrisis: case SocialKind::Verdict: case SocialKind::PeaceSummit: case SocialKind::SummitFailed:
			case SocialKind::IdeologyShift: case SocialKind::Riot: case SocialKind::Uprising: case SocialKind::Sabotage:
			case SocialKind::SpyCaught: case SocialKind::PactLeaked: case SocialKind::TechStolen: case SocialKind::PeaceForced:
			case SocialKind::Conscription: case SocialKind::MercsTurned: case SocialKind::AdmiralRises: case SocialKind::PrisonersExchanged:
			case SocialKind::SyndicateRises: case SocialKind::PrisonBreak: case SocialKind::Assassination: case SocialKind::FraudExposed:
			case SocialKind::CrimeWave: case SocialKind::Crackdown: case SocialKind::StrikeCalled: case SocialKind::Refugees:
			case SocialKind::Takeover: case SocialKind::SharesCrash: case SocialKind::BankRun: case SocialKind::LoanDefault:
			case SocialKind::PatentStolen: case SocialKind::FaithFounded: case SocialKind::Schism: case SocialKind::HolyWar:
			case SocialKind::Championship: case SocialKind::MonumentRaised: case SocialKind::Outbreak: case SocialKind::Quarantine:
			case SocialKind::Cured: case SocialKind::Disaster: case SocialKind::Relief: case SocialKind::Famine: case SocialKind::FoodAid:
			case SocialKind::Investigation: case SocialKind::JournalistSilenced: case SocialKind::BabyBoom:
				return true;
			default:
				return false;
		}
	}

	cs::List<cs::String> Narrator::Templates(SocialKind k)
	{
		switch (k)
		{
			case SocialKind::Birth: return L({ "{house} welcomes a child on {world}.", "A child is born to {house} of {world}." });
			case SocialKind::Death: return L({ "{who} of {house} has died on {world}.", "{world} mourns {who}." });
			case SocialKind::Wedding: return L({ "{who} weds {whom}, joining {house} and {otherhouse}.", "{house} and {otherhouse} are joined: {who} marries {whom}." });
			case SocialKind::BusinessFounded: return L({ "{business} opens its doors on {world}.", "{house} founds {business} on {world}." });
			case SocialKind::Bankruptcy: return L({ "{business} goes to the wall on {world}.", "{business} of {world} is wound up, its creditors unpaid." });
			case SocialKind::Buyout: return L({ "{business} is bought out by a rival on {world}.", "{otherhouse} swallows {business}." });
			case SocialKind::Scandal: return L({ "Scandal on {world}: {who} took {money} from {business}.", "{world}'s governor {who} is out over {business}'s bribes." });

			case SocialKind::Friendship: return L({ "Good to have met you, {whom}. Let's not leave it so long next time.", "{whom}, you're a true friend. Drinks on me when I'm next on {world}." });
			case SocialKind::Rivalry: return L({ "{who} and {whom} of {world} are no longer on speaking terms.", "Bad blood on {world}: {who} and {whom} cross swords." });
			case SocialKind::FallingOut: return L({ "Don't write to me again, {whom}.", "After what you did, {whom}, we're finished." });
			case SocialKind::Mood: return L({ "The mood on {world} turns against {faction}.", "{world} grows restless under {faction}." });

			case SocialKind::FeudDeclared: return L({ "{house} swears a feud against {otherhouse} on {world}.", "Blood feud: {house} and {otherhouse} of {world} are at daggers drawn." });
			case SocialKind::FeudSabotage: return L({ "Fire at {business}: {house} blames {otherhouse}.", "{business} is sabotaged on {world}; few doubt {otherhouse} is behind it." });
			case SocialKind::FeudEnded: return L({ "{house} and {otherhouse} make their peace on {world}.", "The feud between {house} and {otherhouse} is over." });
			case SocialKind::InheritanceDispute: return L({ "{who} contests the will of {house}: the family fights over {money}.", "{house} is torn over who inherits." });
			case SocialKind::HouseSplit: return L({ "{house} splits in two: {who} takes a share of {money} and founds {otherhouse}.", "The {house} fortune is divided; {otherhouse} rises from the quarrel." });
			case SocialKind::Affair: return L({ "Meet me at the usual place, {whom}. Nobody needs to know.", "{whom} - I can't stop thinking about you." });
			case SocialKind::Divorce: return L({ "{who} and {whom} divorce, and {house} and {otherhouse} part on cold terms.", "The marriage of {who} and {whom} is over." });
			case SocialKind::HiddenHeir: return L({ "A secret child of {house} comes forward on {world}: {who}.", "{who} claims to be the hidden heir of {house}." });
			case SocialKind::Graduation: return L({ "{who} of {house} graduates from {org}.", "{org} sends {who} out into the galaxy with honours." });
			case SocialKind::SchoolFounded: return L({ "{org} opens on {world}.", "{world} has a new academy: {org}." });
			case SocialKind::CelebrityRise: return L({ "{who} is the name on every lip on {world}.", "Everyone is talking about {who}." });
			case SocialKind::CelebrityScandal: return L({ "{who} falls from grace on {world}.", "The fans turn on {who}." });
			case SocialKind::Endorsement: return L({ "{who} says: nothing beats {business}.", "As used by {who}: {business}." });

			case SocialKind::IdeologyShift: return L({ "{faction} turns hawkish: the war party has the upper hand.", "The mood in {faction} shifts; its rulers change course." });
			case SocialKind::PartyFounded: return L({ "{org} is founded on {world} to fight for the vote.", "A new party, {org}, enters {faction}'s politics." });
			case SocialKind::ElectionCalled: return L({ "{faction} goes to the polls.", "An election is called in {faction}." });
			case SocialKind::ElectionWon: return L({ "{org} wins {faction}'s election with {pct} of the vote.", "Landslide in {faction}: {org} takes power." });
			case SocialKind::ElectionRigged: return L({ "Fraud alleged: {org}'s victory in {faction} was bought.", "Ballots burned on {world}: {faction}'s election is called a sham." });
			case SocialKind::Coup: return L({ "Coup in {faction}: {who} seizes power on {world}.", "{faction}'s government falls to the guns of {who}." });
			case SocialKind::CoupFailed: return L({ "A coup in {faction} is put down; {who} is arrested.", "Plotters fail on {world}: {who}'s coup collapses." });
			case SocialKind::SuccessionCrisis: return L({ "{faction} is leaderless: rival claimants circle the seat.", "Succession crisis in {faction}." });
			case SocialKind::TrialOpened: return L({ "{who} goes on trial on {world}.", "The case against {who} opens on {world}." });
			case SocialKind::Verdict: return L({ "{who} is found guilty on {world}.", "Verdict on {world}: {who} will serve time." });
			case SocialKind::JudgeBribed: return L({ "{who} walks free on {world}; the judge's new villa raises eyebrows.", "Acquitted: {who}. Some say the verdict was bought." });
			case SocialKind::PeaceSummit: return L({ "Peace: {faction} and {rival} sign at a summit on {world}.", "Leaders of {faction} and {rival} meet on {world} and end their war." });
			case SocialKind::SummitFailed: return L({ "Talks between {faction} and {rival} collapse on {world}.", "No peace: the {world} summit breaks up in recriminations." });
			case SocialKind::PollPublished: return L({ "Poll: {pct} of {world} back {faction}.", "Latest survey: {faction}'s approval on {world} stands at {pct}." });

			case SocialKind::Protest: return L({ "Crowds fill the streets of {world} against {faction}.", "Protest on {world}." });
			case SocialKind::Riot: return L({ "Riots on {world}: shops burned, {faction}'s offices stormed.", "{world} burns as protests turn to riots." });
			case SocialKind::CellFormed: return L({ "Rebels organise on {world}: {org} vows to throw off {faction}.", "Whispers on {world} of an armed cell, {org}." });
			case SocialKind::Sabotage: return L({ "Bomb on {world}: {org} strikes at {faction}.", "Sabotage on {world}; {org} claims it." });
			case SocialKind::Uprising: return L({ "{world} rises against {faction}.", "Uprising on {world}: {org} takes the streets." });
			case SocialKind::SpyCaught: return L({ "A spy for {rival} is caught on {world}.", "{faction} unmasks an agent of {rival}: {who}." });
			case SocialKind::PactLeaked: return L({ "Leaked: {faction}'s secret dealings with {rival}.", "Spies expose {faction}'s pact with {rival}." });
			case SocialKind::TechStolen: return L({ "{rival}'s agents steal {faction}'s research.", "Plans stolen: {faction}'s labs breached by {rival}." });
			case SocialKind::WarWeariness: return L({ "{world} is sick of {faction}'s war.", "Enough: {world} wants the war over." });
			case SocialKind::Conscription: return L({ "{faction} calls up its reserves; {world} sends its young to war.", "Conscription on {world}." });
			case SocialKind::PeaceForced: return L({ "Under pressure from home, {faction} sues {rival} for peace.", "War-weary {faction} makes peace with {rival}." });
			case SocialKind::MercsHired: return L({ "{org} is hired by {house} for {money}.", "{faction} hires {org} for its war." });
			case SocialKind::MercsTurned: return L({ "{org} changes sides, bought by {rival}.", "Betrayal: {org} turns its guns on {faction}." });
			case SocialKind::AdmiralRises: return L({ "{who} is made admiral of {faction}'s fleet.", "{faction}'s hero {who} takes command." });
			case SocialKind::PrisonersExchanged: return L({ "{faction} and {rival} exchange {amount} prisoners.", "Prisoners come home: {faction} and {rival} trade captives." });

			case SocialKind::SyndicateRises: return L({ "{org} takes over the underworld of {world}.", "A new syndicate, {org}, rules the back streets of {world}." });
			case SocialKind::Protection: return L({ "{business} pays {org} for 'protection'.", "{org} squeezes {business} on {world}." });
			case SocialKind::SmugglingRun: return L({ "Smugglers slip past {faction}'s embargo into {world}.", "Contraband pours into {world}." });
			case SocialKind::SmugglerCaught: return L({ "Smugglers seized at {world}.", "{faction} catches a smuggling ring at {world}." });
			case SocialKind::PrisonOpened: return L({ "{faction} opens a penal colony at {world}.", "Convicts are shipped to {world}." });
			case SocialKind::PrisonBreak: return L({ "Mass breakout at {world}: {amount} convicts escape.", "Prison break on {world}." });
			case SocialKind::Assassination: return L({ "{who} is murdered on {world}; {house} is suspected.", "Assassination on {world}: {who} is dead." });
			case SocialKind::AssassinationFoiled: return L({ "A plot against {who} is foiled on {world}.", "{who} survives an attempt on their life." });
			case SocialKind::Fraud: return L({ "{business} reports record profits.", "{business}: never a better time to invest." });
			case SocialKind::FraudExposed: return L({ "{business} exposed: its books were lies; investors lose {money}.", "Fraud at {business} on {world}." });
			case SocialKind::CrimeWave: return L({ "Crime wave on {world}.", "{world}'s streets are no longer safe." });
			case SocialKind::Crackdown: return L({ "{faction} cracks down on crime on {world}.", "Police sweep {world}'s underworld." });

			case SocialKind::UnionFormed: return L({ "The workers of {world} organise: {org} is born.", "{org} signs up {world}'s dockers and miners." });
			case SocialKind::WageDeal: return L({ "{org} wins a pay rise on {world}.", "Wage deal on {world}." });
			case SocialKind::StrikeCalled: return L({ "{org} calls {world} out on strike.", "{world} downs tools." });
			case SocialKind::Refugees: return L({ "{amount} refugees flee {world}.", "Refugees from {world} crowd the docks of their neighbours." });
			case SocialKind::Takeover: return L({ "Hostile takeover: {otherhouse} seizes control of {business}.", "{business} falls to a raid on its shares." });
			case SocialKind::SharesCrash: return L({ "Shares in {business} crash.", "Panic selling: {business} loses half its value." });
			case SocialKind::BankFounded: return L({ "{org} opens on {world}.", "{world} has a new bank: {org}." });
			case SocialKind::LoanDefault: return L({ "{business} defaults on {money} owed to {org}.", "{org} writes off {business}'s loans." });
			case SocialKind::BankRun: return L({ "Run on {org}: depositors besiege its doors.", "{org} fails; savers lose everything." });
			case SocialKind::PremiumsRise: return L({ "Shipping insurers raise premiums around {system}.", "Piracy drives insurance costs up at {world}." });
			case SocialKind::AdWar: return L({ "{business}: accept no imitations.", "Why pay more? {business} beats the rest." });
			case SocialKind::LandmarkFound: return L({ "{world} becomes famous for its sights.", "The wonders of {world} draw travellers." });
			case SocialKind::TouristBoom: return L({ "Visit {world}: see the wonders for yourself.", "{world} - the trip of a lifetime." });
			case SocialKind::PatentFiled: return L({ "{business} patents a new invention.", "{business} files a breakthrough patent." });
			case SocialKind::PatentStolen: return L({ "{business}'s patent is stolen by rivals.", "Industrial espionage: {business}'s secrets leak." });

			case SocialKind::FaithFounded: return L({ "A new faith, {org}, is preached on {world}.", "{who} founds {org} on {world}." });
			case SocialKind::Conversion: return L({ "{org} wins converts on {world}.", "{world} turns to {org}." });
			case SocialKind::Schism: return L({ "Schism: {otherorg} breaks from {org}.", "{org} splits; the dissenters found {otherorg}." });
			case SocialKind::Pilgrimage: return L({ "Pilgrims of {org} travel to {world}.", "{who} sets out on pilgrimage to {world}." });
			case SocialKind::HolyWar: return L({ "{org} preaches war against {rival}.", "The faithful of {faction} call for war on {rival}." });
			case SocialKind::Festival: return L({ "{world} celebrates its festival.", "Festival season on {world}." });
			case SocialKind::MatchPlayed: return L({ "{org} beats {otherorg}.", "Result: {org} over {otherorg}." });
			case SocialKind::MatchRigged: return L({ "Match-fixing: {org}'s win over {otherorg} was bought.", "Betting scandal engulfs {org}." });
			case SocialKind::Championship: return L({ "{org} are champions!", "{world} celebrates as {org} win the league." });
			case SocialKind::TrendSweeps: return L({ "Everyone on {world} is wearing it.", "The new craze sweeps {world}." });
			case SocialKind::CulturalDrift: return L({ "{world} drifts from {faction}'s ways.", "On {world} they talk differently now." });
			case SocialKind::MonumentRaised: return L({ "{world} raises a monument.", "A monument stands on {world} to remember." });
			case SocialKind::Chronicle: return L({ "Historians record what happened on {world}.", "A chronicle of {world} is published." });
			case SocialKind::Rumour: return L({ "They say {who} is ruined. You heard it here first.", "Word is {faction} is about to fall." });
			case SocialKind::Propaganda: return L({ "{faction} stands strong. {rival} lies.", "Trust {faction}. Fear {rival}." });

			case SocialKind::Outbreak: return L({ "Outbreak on {world}: a fever spreads.", "Plague on {world}." });
			case SocialKind::Spread: return L({ "The sickness reaches {world}.", "The fever spreads to {world}." });
			case SocialKind::Quarantine: return L({ "{world} is sealed under quarantine.", "No ships may dock at {world}: quarantine." });
			case SocialKind::Cured: return L({ "{world} is free of the fever.", "The quarantine on {world} lifts." });
			case SocialKind::HospitalBuilt: return L({ "{world} opens a new hospital.", "A hospital rises on {world}." });
			case SocialKind::MedicineShortage: return L({ "Medicine runs short on {world}.", "Pharmacies empty on {world}." });
			case SocialKind::BabyBoom: return L({ "Peace brings a baby boom to {world}.", "{world}'s nurseries overflow." });
			case SocialKind::Ageing: return L({ "{world} grows old: more retire than are born.", "An ageing {world} worries for its future." });
			case SocialKind::Disaster: return L({ "Disaster strikes {world}.", "Catastrophe on {world}." });
			case SocialKind::Relief: return L({ "{faction} sends aid to stricken {world}.", "Relief ships reach {world}." });
			case SocialKind::Famine: return L({ "Famine on {world}.", "{world} goes hungry." });
			case SocialKind::FoodAid: return L({ "{faction} ships food to starving {world}.", "Grain convoys reach {world}." });

			case SocialKind::OutletFounded: return L({ "{org} goes on air from {world}.", "{house} launches {org}." });
			case SocialKind::SlantedStory: return L({ "{org} reports on {world}.", "{org}: the real story on {world}." });
			case SocialKind::BoardPost: return L({ "Can't believe what's happening on {world}.", "Anyone else on {world} fed up?", "Stay safe out there, {world}." });
			case SocialKind::Investigation: return L({ "{org} investigates: {who} exposes the truth about {business}.", "{who} of {org} digs into {business}." });
			case SocialKind::JournalistSilenced: return L({ "Journalist {who} found dead on {world}.", "{who}, who was investigating {business}, has vanished." });
			case SocialKind::CharityFounded: return L({ "{house} founds {org} on {world}.", "{org}, a new charity, opens on {world}." });
			case SocialKind::Donation: return L({ "{org} gives {money} to the poor of {world}.", "{house} donates {money} through {org}." });
			default: return L({ "Something happened on {world}." });
		}
	}

	cs::String Narrator::Fill(const cs::String& line, const SocialEvent& e, bool prose)
	{
		cs::String s = line;
		ReplaceAll(s, "{who}", e.Who != nullptr ? e.Who->FullName() : cs::String("someone"));
		ReplaceAll(s, "{whom}", e.Whom != nullptr ? e.Whom->FullName() : cs::String("a friend"));
		ReplaceAll(s, "{house}", e.House != nullptr ? cs::Cat("House ", e.House->Surname) : cs::String("a family"));
		ReplaceAll(s, "{otherhouse}", e.OtherHouse != nullptr ? cs::Cat("House ", e.OtherHouse->Surname) : cs::String("another family"));
		ReplaceAll(s, "{business}", e.Business != nullptr ? e.Business->Name : cs::String("a business"));
		ReplaceAll(s, "{org}", e.Org != nullptr ? (prose ? OrgName(*e.Org) : e.Org->Name) : cs::String("an organisation"));
		ReplaceAll(s, "{otherorg}", e.OtherOrg != nullptr ? (prose ? OrgName(*e.OtherOrg) : e.OtherOrg->Name) : cs::String("another"));
		ReplaceAll(s, "{outlet}", e.RunBy != nullptr ? (prose ? OrgName(*e.RunBy) : e.RunBy->Name) : cs::String("the press"));
		ReplaceAll(s, "{world}", e.Where != nullptr ? e.Where->Name : (e.System != nullptr ? e.System->Name : cs::String("a distant world")));
		ReplaceAll(s, "{system}", e.System != nullptr ? e.System->Name : cs::String("deep space"));
		ReplaceAll(s, "{faction}", e.Faction != nullptr ? e.Faction->Name : cs::String("the authorities"));
		ReplaceAll(s, "{rival}", e.Rival != nullptr ? e.Rival->Name : cs::String("their enemies"));
		ReplaceAll(s, "{money}", Money(e.Amount));
		ReplaceAll(s, "{amount}", cs::Fmt(e.Amount, "#,0"));
		ReplaceAll(s, "{pct}", cs::Cat(cs::Fmt(e.Amount * 100, "0"), "%"));
		return s;
	}

	cs::String Narrator::Render(const SocialEvent& e, bool prose)
	{
		cs::List<cs::String> pool;
		if (prose && Source()) pool = Source()(Key(e.Kind));
		if (pool.Count() == 0) pool = Templates(e.Kind);
		cs::String line = Fill(pool[static_cast<int>(e.Seed % static_cast<std::uint32_t>(pool.Count()))], e, prose);
		if (e.Unverified) line = cs::Cat("Unconfirmed: ", line);
		if (e.RunBy != nullptr && e.Slant != 0)
		{
			auto slantKey = cs::String(e.Slant > 0 ? "spacegod/social-slant-for" : "spacegod/social-slant-against");
			cs::List<cs::String> tags;
			if (prose && Source()) tags = Source()(slantKey);
			if (tags.Count() == 0)
				tags = e.Slant > 0 ? L({ " A triumph for {faction}.", " {faction} stands vindicated.", " Good news for {faction}." })
					: L({ " Another blow to {faction}.", " Questions mount for {faction}.", " {faction} has much to answer for." });
			line = cs::Cat(prose ? OrgName(*e.RunBy) : e.RunBy->Name, ": ", line, Fill(tags[static_cast<int>(Civ::Mix(e.Seed) % static_cast<std::uint32_t>(tags.Count()))], e, prose));
		}
		return line;
	}

	cs::String Narrator::OrgName(const Org& o)
	{
		if (Source())
		{
			auto names = Source()(NamesKey(o.Kind));
			if (names.Count() > 0)
			{
				cs::String n = names[static_cast<int>(o.NameSeed % static_cast<std::uint32_t>(names.Count()))];
				ReplaceAll(n, "{world}", o.Seat != nullptr ? o.Seat->Name : cs::String(""));
				ReplaceAll(n, "{house}", o.Owner != nullptr ? o.Owner->Surname : cs::String(""));
				return n;
			}
		}
		return o.Name;
	}

	cs::String Narrator::FallbackName(OrgKind k, std::uint32_t h, const cs::String& place, const cs::String& surname)
	{
		using Civ::Pick;
		std::uint32_t h2 = Civ::Mix(h);
		switch (k)
		{
			case OrgKind::Party: return cs::Cat(Pick({ "The ", "The ", "The United " }, h2), Pick({ "Liberty", "Unity", "Homeworld", "Progress", "Order", "Frontier", "Commonwealth", "Labour", "Concord", "Vanguard" }, h), Pick({ " Party", " League", " Front", " Alliance", " Movement" }, h2 >> 8));
			case OrgKind::Faith: return cs::Cat(Pick({ "The Church of ", "The Way of ", "The Children of ", "The Covenant of ", "The Order of ", "The Assembly of " }, h), Pick({ "the Long Dark", "the Burning Star", "the Silent Void", "the First Light", "the Turning Wheel", "the Deep Tide", "the Hollow Sun", "the Last Shore" }, h2));
			case OrgKind::Syndicate: return cs::Cat(Pick({ "The ", "The ", "" }, h2), Pick({ "Black", "Red", "Silent", "Iron", "Velvet", "Ninth", "Grey", "Golden" }, h), " ", Pick({ "Hand", "Lotus", "Circle", "Ledger", "Knife", "Tide", "Company", "Brotherhood" }, h2 >> 8));
			case OrgKind::Union: return cs::Cat(place, Pick({ " Dockers' Union", " Miners' Federation", " Workers' Guild", " Spacers' Brotherhood", " Labour Council" }, h));
			case OrgKind::Bank: return cs::Cat(Pick({ "Bank of ", "First Bank of ", "", "" }, h2), Pick({ place.c_str(), surname.c_str() }, h), Pick({ "", "", " Savings & Loan", " Credit", " Trust" }, h >> 8));
			case OrgKind::Outlet: return cs::Cat(place, Pick({ " Herald", " Signal", " Courier", " Observer", " Broadcast", " Clarion", " Dispatch" }, h));
			case OrgKind::Team: return cs::Cat(place, " ", Pick({ "Comets", "Rangers", "Hammers", "Wolves", "Strikers", "Corsairs", "Titans", "Nova", "Meteors", "Drifters" }, h));
			case OrgKind::Charity: return cs::Cat("The ", surname, Pick({ " Foundation", " Trust", " Relief Fund", " Mission" }, h));
			case OrgKind::Mercenaries: return cs::Cat(Pick({ "The ", "" }, h2), Pick({ "Grey", "Crimson", "Starborn", "Free", "Iron", "Broken", "Lucky", "Seventh" }, h), " ", Pick({ "Lances", "Company", "Wardogs", "Legion", "Blades", "Rifles" }, h2 >> 8));
			case OrgKind::School: return cs::Cat(place, Pick({ " Academy", " University", " Institute", " College of Spaceflight", " Polytechnic" }, h));
			case OrgKind::Agency: return cs::Cat(Pick({ "Bureau ", "Directorate ", "Office ", "Section " }, h), Pick({ "Nine", "Seven", "Zero", "Twelve", "Four", "Delta", "Echo" }, h2));
			case OrgKind::Insurgents: return cs::Cat(Pick({ "Free ", "Sons of ", "Daughters of ", "The ", "" }, h2), place, Pick({ " Front", " Liberation Army", " Resistance", " Movement", "" }, h));
		}
		return place;
	}
}
