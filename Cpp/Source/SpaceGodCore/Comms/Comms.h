#pragma once
// US-T137: comms. Hail any ship or station and talk to it: ask to trade, ask for advice about the area, ask the news, ask
// about them; threaten it for its cargo; answer a hull that has struck its colours.
//
// Every answer is made from the simulation as it stands: the advice from what the speaker's faction actually holds on its
// sensors (so "pirates 10 klicks out at bearing 104 mark 12" is where the track is, from the asker's own nose), the news
// from the system's log, a trade from the hull's real hold and purse, a threat's answer from the target's own pilot (their
// courage, greed and grudges, US-T138), its cargo's worth and the two groups' real strength. Everything is decided by a
// pure function of that state, so lockstep co-op and a rewind say and do the same (see CommsCommand).
//
// The SpaceGod is no one ship or pilot: whatever is said is said by the unit it holds (CommsContext.Speaker), and every
// consequence lands on that unit's pilot, hull and faction (their law record, their story, their faction's grievance),
// never on a player identity. When the SpaceGod leaves, the pilot is left holding it (Characters.Released).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <functional>

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Faction;
	class NamedPilot;
	class Settlement;
	class Ship;
	class World;
	enum class Good : int;
	enum class ContactLevel : int;

	/// <summary>What the speaker says (US-T137). The order is the panel's.</summary>
	enum class CommsVerb : int
	{
		Hail,
		AskAdvice,
		AskNews,
		AskTrade,
		/// <summary>Buy one crate of what the hull carries, at the price it named.</summary>
		Buy,
		AskStory,
		Threaten,
		/// <summary>To a hull that struck its colours: take what it offers (its cargo, or the ship).</summary>
		AcceptSurrender,
		/// <summary>No quarter: it fights on.</summary>
		RefuseSurrender,
		/// <summary>Board it and strip it: ship, cargo and all.</summary>
		Plunder,
		LetGo,
		SignOff,
	};
	SPACEGODCORE_API const char* ToString(CommsVerb v);
	SPACEGODCORE_API cs::String VerbLabel(CommsVerb v);

	/// <summary>How the other side feels about the speaker, from faction, law, grievance and the pilot's grudges.</summary>
	enum class CommsStance : int
	{
		Friendly,
		Neutral,
		Wary,
		Hostile,
	};
	SPACEGODCORE_API const char* ToString(CommsStance v);

	/// <summary>What a threat or a surrender answer came to.</summary>
	enum class CommsOutcome : int
	{
		None,
		Talked,
		/// <summary>Threatened, it gave up its cargo.</summary>
		Complied,
		/// <summary>Threatened, it ran.</summary>
		Ran,
		/// <summary>Threatened, it turned to fight.</summary>
		Fought,
		Refused,
		Traded,
		SurrenderTaken,
		NoQuarter,
		Plundered,
		LetGo,
	};
	SPACEGODCORE_API const char* ToString(CommsOutcome v);

	/// <summary>
	/// How ranges are said (US-T137): the true distance ("10 klicks", "42 thousand klicks", "0.3 AU") at both scales,
	/// through <see cref="Ranges"/> (US-T156), the one source the HUD reads its ranges from too, so a hull the HUD has at
	/// 320 km is "320 klicks" on the radio, in the contact list and in anyone's advice alike.
	/// </summary>
	struct SPACEGODCORE_API CommsRuler
	{
		/// <summary>"10 klicks", "4.2 thousand klicks", "1.3 million klicks", "0.42 AU" (Ranges::Spoken).</summary>
		static cs::String Words(double km);

		cs::String Range(DVec3 fromHelio, DVec3 toHelio) const;
	};

	/// <summary>Someone comms can talk to: a hull, or a station (a settlement's traffic control).</summary>
	struct SPACEGODCORE_API CommsParty
	{
		cs::Ref<Core::Ship> Ship;
		cs::Ref<Settlement> Station;

		bool IsNone() const { return Ship == nullptr && Station == nullptr; }
		bool IsStation() const { return Ship == nullptr && Station != nullptr; }
		cs::Ref<Core::Faction> Faction() const;

		/// <summary>"the Kestrel", "Ceres Hub traffic control".</summary>
		cs::String Name() const;

		/// <summary>What <see cref="CommsCommand"/> carries: a hull's id (0 and up), or -(settlement id + 1).</summary>
		int Key() const;

		static CommsParty OfShip(cs::Ref<Core::Ship> s) { CommsParty p; p.Ship = std::move(s); return p; }
		static CommsParty OfStation(cs::Ref<Settlement> s) { CommsParty p; p.Station = std::move(s); return p; }

		bool operator==(const CommsParty& o) const { return Ship == o.Ship && Station == o.Station; }
	};

	/// <summary>
	/// The speaker and where they are (US-T137): the unit the SpaceGod holds now, its hull (null for a station seat), its
	/// faction, and its pose for bearings and for where a fight starts. Built by the app each time something is said.
	/// </summary>
	struct SPACEGODCORE_API CommsContext
	{
		cs::Ref<Core::World> World;
		/// <summary>The held hull, or null when the SpaceGod holds a station (then <see cref="SpeakerStation"/>).</summary>
		cs::Ref<Core::Ship> Speaker;
		cs::Ref<Settlement> SpeakerStation;
		cs::Ref<Core::Faction> SpeakerFaction;

		/// <summary>Held by the SpaceGod now (PilotInput.Piloted's sense): comms is the held unit's, so nothing is said without one.</summary>
		bool Piloted = false;

		int FrameBody = 0;
		DVec3 OffsetKm, Forward = DVec3(1, 0, 0), Up = DVec3(0, 0, 1);
		DVec3 HelioKm;
		double Jd = 0;
		CommsRuler Ruler;
	};

	/// <summary>One line on the comms log.</summary>
	struct SPACEGODCORE_API CommsLine
	{
		cs::String Who;
		cs::String Text;
		bool FromSpeaker = false;
	};

	struct SPACEGODCORE_API CommsReply
	{
		cs::List<CommsLine> Lines;
		CommsOutcome Outcome = CommsOutcome::None;
		/// <summary>One line for the HUD's toast, or empty.</summary>
		cs::String Toast;
		/// <summary>The fight it started (a threat answered with guns, a runner chased), or null.</summary>
		cs::Ref<Core::Battle> Battle;
	};

	/// <summary>Cargo a threatened hull dropped (US-T137), for US-T95's tractor to pick up as canisters.</summary>
	struct SPACEGODCORE_API CargoDrop
	{
		cs::Ref<Core::Ship> From, To;
		Good What{};
		double Tonnes = 0;
		bool Stolen = true;
		DVec3 HelioKm;
		double Jd = 0;
	};

	class SPACEGODCORE_API Comms final
	{
	public:
		/// <summary>How far a hail carries, km: anything the speaker's sensors hold inside it can be hailed.</summary>
		static constexpr double HailRangeKm = 2.0e6;

		/// <summary>
		/// US-T96's hook: true while the speaker transmits through a broadcast array that hides who it is. Then a robbery is
		/// booked on nobody's record unless the victim's own sensors made out the hull, and grudges name "a masked voice".
		/// Null (the default) means every hail carries its transponder ID.
		/// </summary>
		static std::function<bool(const cs::Ref<Ship>&)>& AnonymousBroadcast();

		/// <summary>
		/// US-T95's hook: cargo dropped to a threat goes here first (to float as canisters for a tractor); it returns the tonnes
		/// it took. Null (the default), or whatever it leaves, goes straight into the speaker's hold as stolen crates while
		/// there is room, and the rest drifts off.
		/// </summary>
		static std::function<double(const CargoDrop&)>& Tractor();

		/// <summary>Who can be hailed from here: the hulls the speaker's sensors hold within <see cref="HailRangeKm"/>, nearest
		/// first, then the stations at the world the speaker is at (at most <paramref name="max"/> all told).</summary>
		static cs::List<CommsParty> Contacts(const CommsContext& c, int max = 12);

		/// <summary>How well the speaker makes out <paramref name="s"/>: its own side fully, anyone else by its picture's track
		/// or its own scanners' look now, whichever is better. None: not seen, so not hailable.</summary>
		static ContactLevel Seen(const CommsContext& c, const cs::Ref<Ship>& s);

		/// <summary>Where a party is, heliocentric km (a station at its world's centre).</summary>
		static DVec3 HelioOf(const CommsContext& c, const CommsParty& p);

		/// <summary>The other side's stance toward the speaker (see <see cref="CommsStance"/>).</summary>
		static CommsStance StanceOf(const CommsContext& c, const CommsParty& to);

		/// <summary>What can be said to <paramref name="to"/> now, in the panel's order.</summary>
		static cs::List<CommsVerb> Verbs(const CommsContext& c, const CommsParty& to);

		/// <summary>The one entry point: the speaker says <paramref name="verb"/> to <paramref name="to"/>, and the world answers
		/// (and acts: a threat may move cargo, start a fight; a surrender answer settles terms).</summary>
		static CommsReply Say(const CommsContext& c, const CommsParty& to, CommsVerb verb);

		/// <summary>The hull in the speaker's fight that struck its colours to the speaker's side and is hailing for terms, or null.</summary>
		static cs::Ref<BattleUnit> SurrenderHail(const CommsContext& c);

		/// <summary>What the hailing hull offers for its life, in its pilot's voice.</summary>
		static cs::String SurrenderOffer(const CommsContext& c, const cs::Ref<BattleUnit>& u);

		/// <summary>"bearing 104 mark 12": the direction from <paramref name="fromHelio"/> to <paramref name="toHelio"/> off the
		/// nose (<paramref name="forward"/>, the roof <paramref name="up"/>): bearing round to starboard 0-359, mark up 0-359.</summary>
		static cs::String Bearing(DVec3 fromHelio, DVec3 forward, DVec3 up, DVec3 toHelio);

		/// <summary>The named pilot who speaks for <paramref name="p"/> (met now if not before), or null for a station.</summary>
		static cs::Ref<NamedPilot> PilotOf(const CommsContext& c, const CommsParty& p);

		/// <summary>The party a <see cref="CommsCommand"/> names (<see cref="CommsParty.Key"/>), or none.</summary>
		static CommsParty Resolve(const cs::Ref<World>& world, int key);

		/// <summary>A <see cref="CommsCommand"/> applied: its target resolved by key and its verb said (<see cref="Say"/>). What a
		/// co-op peer or a replay runs with the command's two numbers, at the tick it lands.</summary>
		static CommsReply Apply(const CommsContext& c, int targetKey, int verb);

	private:
		static cs::String Greeting(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p);
		static cs::String Advice(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p);
		static cs::String News(const CommsContext& c, const CommsParty& to);
		static cs::String TradeOffer(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p, double& pricePerTonne);
		static CommsReply Buy(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p);
		static CommsReply Threaten(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p);
		static CommsReply AnswerSurrender(const CommsContext& c, CommsVerb verb);
		static double GroupStrength(const cs::Ref<World>& world, const cs::Ref<Ship>& s);
		static double Robbery(const CommsContext& c, const cs::Ref<Ship>& victim, Good& good);
		static void BookPiracy(const CommsContext& c, const cs::Ref<Ship>& victim, const cs::String& detail);
	};
}
