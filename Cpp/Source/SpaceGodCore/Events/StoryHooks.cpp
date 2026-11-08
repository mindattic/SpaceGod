// US-T139: the pregenerated story pools for the dynamic events (Events/StoryHooks.h).
//
// Each distress pool is openers x predicaments x pleas. Placeholders: {caller} (who is calling) and {at} (" off the Vega
// lane", " off Ceres", " beside the lane", or nothing when the call names no place).

#include "Events/StoryHooks.h"

#include "Events/Opportunity.h"

#include <vector>

namespace SpaceGod::Core
{
	namespace
	{
		using Pool = std::vector<const char*>;

		struct CallPools
		{
			Pool Openers, Predicaments, Pleas;
			int Count() const { return static_cast<int>(Openers.size() * Predicaments.size() * Pleas.size()); }
		};

		const Pool& CalmOpeners()
		{
			static const Pool p{
				"{caller} here.", "This is {caller}.", "Mayday, mayday, this is {caller}.", "Any ship, any ship: {caller} calling.",
				"{caller} to anyone listening.", "Repeat, this is {caller}.", "Calling all traffic, {caller} here.",
				"Pan-pan, pan-pan, {caller}.", "{caller}, broadcasting on the open band.", "If anyone hears this, it's {caller}.",
				"{caller} on the distress band.", "General call from {caller}." };
			return p;
		}

		const CallPools& Adrift()
		{
			static const CallPools p{
				CalmOpeners(),
				{ "An interdiction pulse threw us out of the lane{at} and the drives won't light.",
				  "We were pulled out of the lane{at}; the drives are dead and we're tumbling.",
				  "The pulse took our drives{at}. We're drifting.",
				  "Something yanked us off the lane{at}. Main drive's cold and the thrusters are barely holding.",
				  "We came out of the lane hard{at}. No drives, no shields, lights on batteries.",
				  "We're dead in space{at}. The pulse fried the drive controllers.",
				  "We're adrift{at} with a cold drive and a crew getting nervous.",
				  "Pulled off the lane{at}. The engineer says hours, maybe more.",
				  "No drive, no lane{at}. Just us and the dark.",
				  "We dropped out{at} and the drive won't take a start.",
				  "A pulse caught us mid-lane{at}. Every breaker on the drive bus tripped at once.",
				  "We were thrown clear of the lane{at} and we've been drifting since." },
				{ "Requesting assistance.", "Anyone close, please come.", "We'd be grateful for a tow or a jump start.", "Please respond.",
				  "Any help at all, we'll pay what we can.", "We can't fix this ourselves.", "Come alongside if you can.",
				  "We're sitting ducks out here. Please hurry.", "Please, anyone.", "Standing by on this channel.",
				  "If you're near, we'd owe you.", "Over." } };
			return p;
		}

		const CallPools& Dry()
		{
			static const CallPools p{
				CalmOpeners(),
				{ "We're drifting{at} with our tanks run dry.", "Our reaction mass ran out{at} and we're coasting.",
				  "Tanks read empty{at}. We misjudged the burn.", "We're dry{at}, and the tanker is still a long way out.",
				  "Bone-dry tanks{at}. We can steer, just barely, and not stop.", "We burned the last of our mass{at} dodging traffic.",
				  "A valve stuck open and vented our reserve{at}. We're out.", "We're coasting{at} on an empty tank and a prayer.",
				  "We ran the margins too fine and now we're dry{at}.", "Fuel's gone{at}. We've a long fall ahead and no way to stop it.",
				  "The gauges hit zero{at} an hour ago.", "We're out of mass{at}, holding attitude on cold gas." },
				{ "Can anyone spare reaction mass?", "A few tonnes would get us moving.", "Anyone with mass to spare, please come.",
				  "We'll pay well for a transfer.", "Please, even a little would do.", "Can you pump some across?",
				  "We'd take whatever you can spare.", "Please respond if you can top us up.", "The tanker won't make it in time. Can you?",
				  "Standing by for anyone with full tanks.", "Help us get underway.", "Over." } };
			return p;
		}

		const CallPools& UnderFire()
		{
			static const CallPools p{
				{ "Mayday, mayday: {caller}!", "This is {caller}, we're under attack!", "Any ship, {caller} is taking fire!",
				  "{caller} to anyone, mayday!", "Mayday! {caller} here!", "Emergency, emergency, {caller}!",
				  "All traffic, all traffic, {caller} under fire!", "{caller} calling, we're in trouble!",
				  "Help! This is {caller}!", "Mayday from {caller}!", "{caller}, open band, we're hit!", "Somebody answer, it's {caller}!" },
				{ "We're under attack{at} and the shields are failing.", "They're on us{at}. Shields are down to nothing.",
				  "Hostiles closing{at}; we can't outrun them.", "We're taking hits{at} and losing pressure aft.",
				  "Raiders{at}, more than we can fight.", "They jumped us{at}. We've already lost a turret.",
				  "Shields are flickering{at} and they keep coming.", "We're pinned{at}. Hull's buckling forward.",
				  "Fire on three sides{at}. We can't hold.", "They're cutting us apart{at}.", "Our escort's gone{at}; it's just us now.",
				  "We've fire in the hold{at} and they won't stop shooting." },
				{ "Any ship, please respond!", "Please, anyone, hurry!", "We need help now!", "Come quickly or there'll be nothing left!",
				  "Anyone with guns, please!", "We can't last long!", "For pity's sake, come!", "We'll pay anything, just come!",
				  "Please respond!", "Help us!", "If you're close, now's the time!", "Hurry!" } };
			return p;
		}

		cs::String Fill(const char* line, const cs::String& caller, const cs::String& at)
		{
			cs::String out, text(line);
			for (size_t i = 0; i < text.size(); i++)
			{
				if (text[i] == '{')
				{
					size_t j = text.find('}', i);
					if (j != cs::String::npos)
					{
						cs::String key = text.substr(i + 1, j - i - 1);
						if (key == "caller") { out += caller; i = j; continue; }
						if (key == "at") { out += at; i = j; continue; }
					}
				}
				out += text[i];
			}
			return out;
		}

		uint32_t Mix(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
			return x;
		}

		const char* Pick(const Pool& pool, uint32_t seed, uint32_t salt) { return pool[Mix(seed ^ salt) % pool.size()]; }
	}

	uint32_t StoryHooks::SeedOf(const cs::Ref<Opportunity>& op)
	{
		uint32_t h = 2166136261u;
		if (op != nullptr)
		{
			for (unsigned char c : op->SourceKey) { h ^= c; h *= 16777619u; }
			h ^= static_cast<uint32_t>(op->Id) * 2654435761u;
		}
		return Mix(h);
	}

	cs::String StoryHooks::Tell(const cs::Ref<Opportunity>& op)
	{
		if (op == nullptr) return cs::String();
		// A lure keeps reading as the stranded ship it pretends to be until it is sprung; then the director's hook says
		// what it was. Calls from older saves, with no caller kept, keep their hook.
		if (op->Kind != OpportunityKind::Distress || op->Caller.empty()) return op->Hook;
		const CallPools& pools = op->UnderAttack ? UnderFire() : op->DryTanks ? Dry() : Adrift();
		uint32_t seed = SeedOf(op);
		cs::String at = op->Place.empty() ? cs::String() : cs::Cat(" ", op->Place);
		return cs::Cat(Fill(Pick(pools.Openers, seed, 0x51u), op->Caller, at), " ", Fill(Pick(pools.Predicaments, seed, 0xA3u), op->Caller, at), " ",
			Fill(Pick(pools.Pleas, seed, 0x1F7u), op->Caller, at));
	}

	int StoryHooks::Combinations() { return Adrift().Count() + Dry().Count() + UnderFire().Count(); }

	cs::String StoryHooks::LureName(int seed)
	{
		static const Pool first{ "Patient", "Quiet", "Honest", "Lucky", "Long", "Silver", "Morning", "Faithful", "Little", "Steady",
			"Bright", "Gentle", "Second", "Northern", "Kindly", "Humble" };
		static const Pool second{ "Mercy", "Haul", "Dawn", "Penny", "Margin", "Harvest", "Wren", "Promise", "Lantern", "Tern",
			"Ledger", "Orchard", "Chance", "Heron", "Thimble", "Errand" };
		uint32_t s = Mix(static_cast<uint32_t>(seed) * 2246822519u ^ 0x12Eu);
		return cs::Cat(first[s % first.size()], " ", second[(s >> 8) % second.size()]);
	}
}
