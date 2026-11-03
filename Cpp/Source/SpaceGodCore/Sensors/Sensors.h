#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Sensors/Sensors.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Ship;
	class World;
	enum class ShipClass : int;

	/// <summary>How much an observer knows about a contact (U.SPC-T66).</summary>
	enum class ContactLevel : int
	{
		/// <summary>Not detected at all.</summary>
		None,

		/// <summary>Something is there: a heat or EM source, nothing more.</summary>
		Blip,

		/// <summary>Its hull class is resolved (a destroyer, a freighter), but not whose it is or its name.</summary>
		Class,

		/// <summary>Identified: name and faction (by transponder, or by a close enough look).</summary>
		Identified,
	};
	SPACEGODCORE_API const char* ToString(ContactLevel v);

	/// <summary>
	/// What a ship gives off, in signature units (1 = a 9,000 t freighter's hull coasting with its
	/// transponder off). Waste heat scales with hull surface (mass^(2/3)); active systems (fire-control
	/// radar, shields up) add EM; a drive burn adds a plume that scales with thrust; weapons fire is very
	/// bright. Silent running dims the hull's heat to a tenth and kills the EM, but cannot hide a burn.
	/// A hulk is cold; a ship on the ground is hidden from space altogether. Detection range goes as the
	/// square root of signature (inverse-square fall-off): see <see cref="Sensors.Resolve"/>.
	/// </summary>
	class SPACEGODCORE_API Signatures
	{
	public:
		/// <summary>The hull mass whose coasting heat is one signature unit, tonnes (a freighter).</summary>
		static constexpr double ReferenceMassTonnes = 9000;

		/// <summary>What a burn adds for a reference hull at 0.1 g, scaled by thrust (mass times acceleration).</summary>
		static constexpr double DriveFactor = 40;

		/// <summary>A warship's active systems (fire control, shields up) and a civilian's lesser ones.</summary>
		static constexpr double MilitaryEm = 1; static constexpr double CivilEm = 0.2;

		/// <summary>Guns, lasers and missile motors in a fight.</summary>
		static constexpr double WeaponsFire = 24;

		/// <summary>What silent running leaves of a hull's heat (EM goes to nothing).</summary>
		static constexpr double SilentFactor = 0.08;

		/// <summary>What a derelict hulk still gives off of its hull heat.</summary>
		static constexpr double HulkFactor = 0.03;

		/// <summary>A battle seen from outside it (weapons fire over a hull): the reference for witnesses.</summary>
		static constexpr double Battle = 25;

		/// <summary>A hidden haven: reactors and docks buried in a rock, but still warm.</summary>
		static constexpr double Haven = 25;

		/// <summary>
		/// Running dark: the ship's own choice (<see cref="Ship.SilentRunning"/>), or raiders lying in wait
		/// beside a lane, who always do.
		/// </summary>
		static bool Silent(const cs::Ref<Ship>& s);

		/// <summary>Squawking its identity: lawful ships do, unless running silent; pirates and hulks never.</summary>
		static bool Transponder(const cs::Ref<Ship>& s);

		/// <summary>The signature of <paramref name="s"/> now: <paramref name="burning"/> under its drive,
		/// <paramref name="firing"/> its weapons (by default, whenever it is in a battle).</summary>
		static double Of(const cs::Ref<Ship>& s, bool burning, cs::Nullable<bool> firing = std::nullopt);
	};

	/// <summary>A scanner fit: how far it sees a one-unit signature, km, and how finely it resolves what it sees.</summary>
	struct SPACEGODCORE_API SensorSuite
	{
		double RangeKm = 0;
		double Resolution = 0;

		constexpr SensorSuite() = default;
		constexpr SensorSuite(double rangeKm, double resolution) : RangeKm(rangeKm), Resolution(resolution) {}

		/// <summary>A civilian hull's navigation sensors, a warship's search and fire-control suite, and a
		/// settlement's traffic control.</summary>
		static const SensorSuite Civil; static const SensorSuite Military; static const SensorSuite Station;

		/// <summary>What each scanner suite fitted (<see cref="ModuleKind.Scanner"/>) adds: half the base range again, and resolution.</summary>
		static constexpr double ScannerRangeShare = 0.5; static constexpr double ScannerResolution = 0.5;

		/// <summary>A gate picket's long-range search suite (U.SPC-T480): six times a warship's reach and twice its
		/// resolution, so it makes out a raider lying dark off its gate and an arrival's class as it comes through.</summary>
		static const SensorSuite Picket;

		/// <summary>A corvette's patrol search suite (U.SPC-T476): twice a warship's reach and a finer look, to pick out the
		/// smugglers and raiders working its beat (and a hull running silent past it) before they are gone.</summary>
		static const SensorSuite Patrol;

	private:
		/// <summary>Hull classes built round a suite of their own rather than the civil or military base (the extension
		/// point: register a class's suite, see <see cref="RegisterBase"/>).</summary>
		static cs::Dictionary<ShipClass, SensorSuite>& baseByClass();

	public:
		/// <summary>Gives hulls of <paramref name="cls"/> <paramref name="suite"/> as their base suite.</summary>
		static void RegisterBase(ShipClass cls, SensorSuite suite);

		/// <summary>The suite a hull of <paramref name="cls"/> is built with, before any scanner is fitted.</summary>
		static SensorSuite BaseOf(ShipClass cls, bool military);

		/// <summary>What a command ship's sensor array (<see cref="FacilityKinds.SensorArray"/>, U.SPC-T477) is worth in
		/// scanner suites.</summary>
		static constexpr double ArrayScanners = 3;

		/// <summary>The suite <paramref name="s"/> carries: its hull's base, more with each scanner fitted, much more
		/// with a sensor array inside it, and more again with recon fighters aboard to scout ahead (U.SPC-T485).</summary>
		static SensorSuite Of(const cs::Ref<Ship>& s);

		/// <summary>The better of two suites, term by term (a group sees with its best scanners).</summary>
		static SensorSuite Best(SensorSuite a, SensorSuite b) { return SensorSuite(cs::Math::Max(a.RangeKm, b.RangeKm), cs::Math::Max(a.Resolution, b.Resolution)); }

		/// <summary>How far this suite sees a signature, km.</summary>
		double ReachKm(double signature) const { return signature > 0 ? RangeKm * cs::Math::Sqrt(signature) : 0; }
	};
	// C# static readonly SensorSuite fields: constant-initialised, so no cross-file init order.
	inline constexpr SensorSuite SensorSuite::Civil = SensorSuite(2e5, 1);
	inline constexpr SensorSuite SensorSuite::Military = SensorSuite(4e5, 1.5);
	inline constexpr SensorSuite SensorSuite::Station = SensorSuite(6e5, 2);
	inline constexpr SensorSuite SensorSuite::Picket = SensorSuite(2.4e6, 3);
	inline constexpr SensorSuite SensorSuite::Patrol = SensorSuite(8e5, 2.25);

	/// <summary>
	/// One look at a contact (U.SPC-T100): when the sweep was made, when the light it saw had left the contact (as long
	/// before as light takes to cross the gap), where the contact was then, where the observer was, and the world it was
	/// parked at (-1 in flight). Word of it goes on from the observer at light speed (<see cref="ArrivesAt"/>).
	/// </summary>
	struct SPACEGODCORE_API ContactSighting
	{
		double ObservedJd = 0, SeenJd = 0;
		DVec3 HelioKm, ObserverHelioKm;
		int Body = 0;
		ContactLevel Level = ContactLevel::None;

		ContactSighting() = default;
		ContactSighting(double observedJd, double seenJd, DVec3 helio, DVec3 observer, int body, ContactLevel level)
		{
			ObservedJd = observedJd;
			SeenJd = seenJd;
			HelioKm = helio;
			ObserverHelioKm = observer;
			Body = body;
			Level = level;
		}

		/// <summary>The light delay the observer saw it with, seconds.</summary>
		double LagSeconds() const;

		/// <summary>When word of this sighting, radioed on from the observer, reaches <paramref name="where"/>.</summary>
		double ArrivesAt(DVec3 where) const;
	};

	/// <summary>One track in a picture: what is (or was last) known of a ship, and where it was seen.</summary>
	class SPACEGODCORE_API Contact final
	{
	public:
		cs::Ref<SpaceGod::Core::Ship> Ship;

		/// <summary>The best the track has been resolved to (held while it is cold, until it is lost).</summary>
		ContactLevel Level = ContactLevel::None;

		/// <summary>What the last sweep saw of it: <see cref="ContactLevel.None"/> once it has gone cold.</summary>
		ContactLevel Now = ContactLevel::None;

		double FirstSeenJd = 0, LastSeenJd = 0;

		/// <summary>Where it was last seen, heliocentric km (where it was when the light left it, not where it is now),
		/// and how bright it was.</summary>
		DVec3 LastHelioKm;
		double Signature = 0;

		/// <summary>When the light of the last look left it: <see cref="LastSeenJd"/> less the light time across the gap.</summary>
		double SeenJd = 0;

		/// <summary>The light delay of the last look, seconds (U.SPC-T100).</summary>
		double LagSeconds() const;

		/// <summary>The latest looks at it, oldest first (at most <see cref="ContactBook.SightingsKept"/>): what a fleet
		/// far from the observers knows is an older one, until word of the newer has crossed to it.</summary>
		cs::List<ContactSighting> Sightings;

		/// <summary>Not seen on the last sweep: a track going stale, lost after the picture's hold time.</summary>
		bool Cold() const { return Now == ContactLevel::None; }

		/// <summary>What an observer would call it: "unknown contact", its class, or its name.</summary>
		cs::String Label() const { return Describe(Ship, Level); }

		static cs::String Describe(const cs::Ref<SpaceGod::Core::Ship>& s, ContactLevel level);
	};

	/// <summary>A picture: every contact one observer (a faction's ships and stations, or the player's own ship) holds.</summary>
	class SPACEGODCORE_API ContactBook final
	{
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Contact>> contacts;

	public:
		int Count() const { return contacts.Count(); }
		cs::List<cs::Ref<Contact>> Contacts() const { return contacts.Values(); }

		cs::Ref<Contact> Of(const cs::Ref<Ship>& s) const;

		/// <summary>How many looks each track keeps: at a sweep every 0.1 day, more than two days of them, longer
		/// than light takes across any system.</summary>
		static constexpr int SightingsKept = 24;

		/// <summary>
		/// What is known of <paramref name="s"/> at <paramref name="where"/> at <paramref name="jd"/> (U.SPC-T100): the
		/// newest look whose word, radioed on from the observer at light speed, has had time to get there; null if none has.
		/// </summary>
		cs::Nullable<ContactSighting> KnownAt(const cs::Ref<Ship>& s, DVec3 where, double jd) const;

		/// <summary>What is known of <paramref name="s"/>: its track's level, or nothing.</summary>
		ContactLevel LevelOf(const cs::Ref<Ship>& s) const;

		/// <summary>Counts the live (not cold) tracks at <paramref name="level"/>.</summary>
		int CountLive(ContactLevel level) const;

		/// <summary>A sweep saw <paramref name="s"/> at <paramref name="level"/> (None: did not see it).
		/// A track keeps what it had resolved while it is held, so a known ship that goes dark and is
		/// picked up again as a blip is still known.</summary>
		void Observe(const cs::Ref<Ship>& s, ContactLevel level, DVec3 helio, double signature, double jd); // internal

		/// <summary>A look from <paramref name="observer"/> that saw <paramref name="s"/> at <paramref name="helio"/> by light
		/// that left it at <paramref name="seenJd"/>, parked at <paramref name="body"/> (-1 in flight).</summary>
		void Observe(const cs::Ref<Ship>& s, ContactLevel level, DVec3 helio, double signature, double jd, DVec3 observer, double seenJd, int body); // internal

		/// <summary>Drops the tracks cold for longer than <paramref name="holdDays"/>, and the ships gone for good.
		/// Returns the ships lost.</summary>
		// (C# returns null when none is lost: an empty list here.)
		cs::List<cs::Ref<Ship>> Expire(double jd, double holdDays); // internal

		void Clear() { contacts.Clear(); }
		SG_SERIALIZABLE(ContactBook); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// Sensors, signatures and stealth (U.SPC-T66). Nobody, player or AI, simply knows where another ship is:
	/// every faction holds a picture built by its ships' and stations' scanners (<see cref="PictureOf"/>), swept
	/// every <see cref="SweepDays"/>, and the player's piloted ship holds its own (<see cref="Player"/>). A contact
	/// starts as an unknown blip, resolves to its hull class and then its identity as it closes (or at once, if it
	/// squawks a transponder), and is lost once it has been cold (out of reach) for the picture's hold time. What
	/// the pictures hold drives the rest: navies hunt only raiders they have seen and track them home to hidden
	/// havens, patrols hail only a ship they can make out (so silent running slips contraband past them), crimes
	/// are witnessed only by those who could see them, and a lane ambush catches only victims who did not spot
	/// the raiders lying dark beside it.
	/// </summary>
	class SPACEGODCORE_API Sensors final
	{
		cs::Ref<World> world;
		cs::Dictionary<cs::Ref<Faction>, cs::Ref<ContactBook>> pictures;

	public:
		/// <summary>What the player's piloted ship's own scanners hold.</summary>
		cs::Ref<ContactBook> Player = cs::New<ContactBook>();

		/// <summary>How often the factions' pictures are swept, days.</summary>
		double SweepDays = 0.1;

		/// <summary>How long a faction's picture holds a cold track before it is lost, days.</summary>
		double FactionHoldDays = 0.5;

		/// <summary>How often the player's picture is swept, and how long it holds a cold track, seconds.</summary>
		double PlayerSweepSeconds = 1, PlayerHoldSeconds = 300;

		/// <summary>Resolve thresholds: how many times inside its detection reach a contact must be (divided by the
		/// suite's resolution) for its class, and for its identity, to be made out.</summary>
		static constexpr double ClassQuality = 2.5; static constexpr double IdentifyQuality = 6;

	private:
		double nextSweepJd = cs::Double::NegativeInfinity, nextPlayerSweepJd = cs::Double::NegativeInfinity, lastSweepJd = cs::Double::NegativeInfinity;

		/// <summary>The piloted ship's own position and whether its drive is lit, as last reported by the helm.</summary>
		cs::Ref<Ship> pilotShip;
		DVec3 pilotHelio;
		bool pilotBurning = false;

	public:
		/// <summary>Raised when a sweep loses a track: (observer, ship). The observer is null for the player's picture.</summary>
		cs::Event<cs::Ref<Faction>, cs::Ref<Ship>> Lost;

		Sensors(cs::Ref<World> world) : world(std::move(world)) {}

		/// <summary>The picture <paramref name="f"/>'s ships and stations share.</summary>
		cs::Ref<ContactBook> PictureOf(const cs::Ref<Faction>& f);

		// ---------------------------------------------------------------- detection

		/// <summary>
		/// What a suite makes of a signature at a distance: nothing beyond its reach (<see cref="SensorSuite.ReachKm"/>),
		/// else a blip, the class once it is <see cref="ClassQuality"/>/resolution times inside that reach, the identity
		/// at <see cref="IdentifyQuality"/>/resolution -- or straight away from a transponder.
		/// </summary>
		static ContactLevel Resolve(SensorSuite suite, double signature, double distanceKm, bool transponder);

		/// <summary>Whether the drive of <paramref name="s"/> is lit at <paramref name="jd"/>: the helm's word for the piloted ship,
		/// else its schedule (a burn between worlds; riding a lane or holding station is not one).</summary>
		bool Burning(const cs::Ref<Ship>& s, double jd);

	private:
		bool Burning(const cs::Ref<Ship>& s, double jd, DVec3& helio);

	public:
		/// <summary>Where <paramref name="s"/> is now: the helm's own reckoning for the piloted ship, else its schedule.</summary>
		DVec3 HelioOf(const cs::Ref<Ship>& s, double jd);

	private:
		/// <summary>
		/// Records a look from <paramref name="observer"/> at <paramref name="s"/> in <paramref name="book"/> at
		/// <paramref name="jd"/>: the light it saw left the ship as long ago as light takes across the gap, so the track
		/// puts it where it was then (U.SPC-T100), at the world it was parked at, if any.
		/// </summary>
		void Record(const cs::Ref<ContactBook>& book, const cs::Ref<Ship>& s, ContactLevel level, DVec3 helioNow, DVec3 observer, double signature, double jd);

		/// <summary>The world <paramref name="s"/> is parked at (or down on), or -1 in flight.</summary>
		static int ParkedAt(const cs::Ref<Ship>& s);

	public:
		/// <summary>
		/// What <paramref name="f"/> knows of <paramref name="target"/> at <paramref name="where"/> at <paramref name="jd"/>
		/// (U.SPC-T100): the newest sighting in its picture whose word has crossed from the observer at light speed. A
		/// fleet across the system acts on a look minutes or hours old, and the ship may have moved on since. Its own
		/// ships it knows as they are.
		/// </summary>
		cs::Nullable<ContactSighting> KnownAt(const cs::Ref<Faction>& f, const cs::Ref<Ship>& target, DVec3 where, double jd);

		/// <summary>The signature of <paramref name="s"/> at <paramref name="jd"/>.</summary>
		double SignatureOf(const cs::Ref<Ship>& s, double jd);

		/// <summary>What the scanners of <paramref name="observer"/> (its group's best, for a flagship) make of
		/// <paramref name="target"/> right now.</summary>
		ContactLevel Detect(const cs::Ref<Ship>& observer, const cs::Ref<Ship>& target, double jd);

		/// <summary>The best scanners in the group <paramref name="s"/> belongs to.</summary>
		SensorSuite GroupSuite(const cs::Ref<Ship>& s);

		/// <summary>
		/// What <paramref name="f"/> knows of <paramref name="target"/>: its picture's track if the last sweep saw it,
		/// else a look now from every ship and station it has (recorded in its picture), so nothing is missed between sweeps.
		/// </summary>
		ContactLevel Knows(const cs::Ref<Faction>& f, const cs::Ref<Ship>& target, double jd);

		// ---------------------------------------------------------------- sweeps

	private:
		struct Observer
		{
			cs::Ref<SpaceGod::Core::Faction> Faction;
			DVec3 Helio;
			SensorSuite Suite;

			Observer() = default;
			Observer(cs::Ref<SpaceGod::Core::Faction> f, DVec3 helio, SensorSuite suite)
			{
				Faction = f;
				Helio = helio;
				Suite = suite;
			}
		};

		struct Target
		{
			cs::Ref<SpaceGod::Core::Ship> Ship;
			DVec3 Helio;
			double Signature = 0, Root = 0;
			bool Transponder = false;

			Target() = default;
			Target(cs::Ref<SpaceGod::Core::Ship> s, DVec3 helio, double sig, bool squawk)
			{
				Ship = s;
				Helio = helio;
				Signature = sig;
				Root = cs::Math::Sqrt(sig);
				Transponder = squawk;
			}
		};

		/// <summary>Every ship and station that can look: group flagships (with their escorts' best scanners),
		/// lone hulls, and every settlement with people and an owner.</summary>
		cs::List<Observer> Observers(double jd);

		static bool Looks(const cs::Ref<Ship>& s);

	public:
		/// <summary>Sweeps the factions' pictures when one is due.</summary>
		void Update(double jd);

		/// <summary>Every faction's ships and stations look at every ship not their own, and the pictures are updated:
		/// tracks seen are refreshed, unseen ones go cold and, past <see cref="FactionHoldDays"/>, are lost. Then the
		/// navies follow any raider they hold on its way into, or parked at, a hidden haven: that gives the haven away.</summary>
		void Sweep(double jd);

	private:
		/// <summary>A navy that holds a raider (by class at least: a warship running without a transponder) parked at,
		/// or burning in towards, a hidden haven's rock has followed pirate traffic home: the haven is found.</summary>
		void TrackHome(double jd);

	public:
		/// <summary>
		/// The helm reports the piloted ship's own position and whether its drive is lit (the schedule does not
		/// know), and its own picture is swept every <see cref="PlayerSweepSeconds"/>: every other ship in reach
		/// of its scanners, resolved by how bright and how close it is; tracks cold past <see cref="PlayerHoldSeconds"/>
		/// are lost. Null (not piloting) clears the picture.
		/// </summary>
		void UpdatePilot(const cs::Ref<Ship>& ship, DVec3 helioKm, bool burning, double jd);

		/// <summary>The piloted ship's own signature now, as the helm last reported it.</summary>
		double PilotSignature() const;

		/// <summary>Whether the helm last reported the piloted ship's drive lit.</summary>
		bool PilotBurning() const { return pilotShip != nullptr && pilotBurning; }

		// ---------------------------------------------------------------- witnesses and havens

		/// <summary>
		/// The first lawful station or ship (not pirates, hulks, those on the ground, nor whatever <paramref name="exclude"/>
		/// rules out) whose scanners reach a <paramref name="signature"/> at <paramref name="helioKm"/>; null if none does.
		/// </summary>
		// (A null C# string is the empty string here.)
		cs::String FindWitness(DVec3 helioKm, double jd, double signature, const cs::Func<bool, cs::Ref<Ship>>& exclude = nullptr);

		/// <summary>How far <paramref name="s"/>'s scanners pick out a hidden haven (<see cref="Signatures.Haven"/>), km.</summary>
		static double HavenReachKm(const cs::Ref<Ship>& s);
	private:
		Sensors() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Sensors); // ObjectGraph registration (Persistence/Registrations)
	};
}
