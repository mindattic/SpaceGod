#pragma once
// US-T260: exploration pays. Charting a star, being first to a body, visiting a void sight, scanning an anomaly or coming
// on a graveyard leaves survey data aboard the embodied hull (worth more the rarer the find and the further it lies past
// charted space); a cartographer (a larger lawful port) buys it on the market board, charting what it shows for that
// port's faction; and every find is a first discovery, credited in the news and the pilot's story, with a little
// Influence for the SpaceGod.
//
// Galaxy state, kept on the territory (Territory.Exploration) so it saves with the galaxy, and changed only by the
// simulation: the finds come from where the helm reported the hull (Fleet.HeldPoseOf), the stars from the discovery
// lookouts and the anomalies from each system's event director, so nothing here runs without a piloted hull (the Bench
// flies none, so its digests never see it) and nothing draws a random number.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Galaxy;
	class Settlement;
	class Ship;
	class StarSystem;
	class World;

	/// <summary>What a piece of survey data is of (US-T260).</summary>
	enum class SurveyKind : int
	{
		/// <summary>A star found past the known neighbourhood (<see cref="Galaxy.Discover"/>).</summary>
		Star,
		/// <summary>A planet, moon, dwarf planet, asteroid or shattered planetoid of a system past the known neighbourhood,
		/// first come within survey range of.</summary>
		Body,
		/// <summary>A void station's sight (<see cref="VoidSight"/>), visited.</summary>
		Sight,
		/// <summary>An anomaly the event director turned up, seen through (<see cref="OpportunityKind.Anomaly"/>).</summary>
		Anomaly,
		/// <summary>A ship graveyard (<see cref="Graveyards"/>), come on.</summary>
		Graveyard,
	};
	SPACEGODCORE_API const char* ToString(SurveyKind v);

	/// <summary>One find's survey data, held aboard the hull that made it until it is sold or the hull is lost.</summary>
	class SPACEGODCORE_API SurveyRecord final
	{
	public:
		/// <summary>What was found ("Wolf 1061", "Glass moonlet of Tarn Reach", "Lensing ripple near Ceres").</summary>
		cs::String Subject;

		/// <summary>The key it is known by (<see cref="Exploration.Surveyed"/>): one find, once, galaxy-wide.</summary>
		cs::String Key;

		/// <summary>The system (or plain star) it was found in or at.</summary>
		cs::String Place;

		SurveyKind Kind = SurveyKind::Body;

		/// <summary>What a cartographer pays for it, credits.</summary>
		double Value = 0;

		/// <summary>How far past the known neighbourhood it lies, light-years (0 inside it).</summary>
		double BeyondLy = 0;

		double Jd = 0;

		/// <summary>The hull that holds it.</summary>
		cs::Ref<SpaceGod::Core::Ship> Hull;

		/// <summary>Who was flying it when it was found, as the news credited them.</summary>
		cs::String Pilot;
	};

	/// <summary>
	/// The galaxy's exploration ledger (US-T260): what has been surveyed, the data still held aboard hulls, what each
	/// faction's cartographers have been sold, and the first discoveries credited.
	/// </summary>
	class SPACEGODCORE_API Exploration final
	{
	public:
		/// <summary>Every find so far, by key, and when (a find pays once, to whoever makes it first).</summary>
		cs::Dictionary<cs::String, double> Surveyed;

		/// <summary>Data aboard hulls, not sold yet, oldest first.</summary>
		cs::List<cs::Ref<SurveyRecord>> Held;

		/// <summary>What each faction's maps have gained from data sold to their cartographers: "faction|key" to when.</summary>
		cs::Dictionary<cs::String, double> Charted;

		/// <summary>First discoveries credited, data sold, and what the sales paid, credits.</summary>
		int Firsts = 0, Sold = 0;
		double Earned = 0;

		// ---- transient (not saved): what the app shows as toasts.
		cs::List<cs::String> Notices;

		/// <summary>Survey range: a hull comes this near a body (or this many of its radii, whichever is further), km.</summary>
		static constexpr double SurveyKm = 100000;
		static constexpr double SurveyRadii = 12;

		/// <summary>A cartographer's port: lawful, a colony or station, and at least this many people.</summary>
		static constexpr double CartographerPopulation = 20000;

		/// <summary>Influence XP for a first discovery: a star, and anything else.</summary>
		static constexpr double StarXp = 15;
		static constexpr double FindXp = 5;

		/// <summary>The galaxy's ledger, made the first time it is asked for (older saves have none).</summary>
		static cs::Ref<Exploration> Of(Galaxy& galaxy);

		/// <summary>The galaxy's ledger if it has one yet, never making it.</summary>
		static cs::Ref<Exploration> Find(const Galaxy& galaxy);

		/// <summary>Whether <paramref name="key"/> has been surveyed by anyone.</summary>
		bool IsSurveyed(const cs::String& key) const { return Surveyed.ContainsKey(key); }

		/// <summary>Whether data on <paramref name="key"/> has been sold to <paramref name="f"/>'s cartographers.</summary>
		bool ChartedFor(const cs::Ref<Faction>& f, const cs::String& key) const;

		/// <summary>How many finds <paramref name="f"/>'s maps have gained from survey data sold to it.</summary>
		int ChartedCount(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// The hull the SpaceGod is embodied in, wherever it is: one flown in a system (that system's world), or the player's
		/// own group crossing open space (the world it set out from). Null when nobody is at a helm.
		/// </summary>
		static cs::Ref<Ship> EmbodiedHull(const Galaxy& galaxy, cs::Ref<World>& world, cs::Ref<StarSystem>& system);

		/// <summary>The survey data aboard <paramref name="hull"/> (none once it is destroyed).</summary>
		cs::List<cs::Ref<SurveyRecord>> HeldBy(const cs::Ref<Ship>& hull) const;

		/// <summary>What <paramref name="hull"/>'s data is worth all together, credits.</summary>
		double ValueHeldBy(const cs::Ref<Ship>& hull) const;

		/// <summary>Whether <paramref name="s"/> keeps a cartographer (see <see cref="CartographerPopulation"/>).</summary>
		static bool IsCartographer(const cs::Ref<Settlement>& s);

		/// <summary>What <paramref name="port"/>'s cartographer would pay for <paramref name="hull"/>'s data: everything its
		/// faction's maps do not have yet; <paramref name="reports"/> is how many reports that is. Zero for no cartographer.</summary>
		double OfferAt(const cs::Ref<Settlement>& port, const cs::Ref<Ship>& hull, int& reports) const;

		/// <summary>
		/// Sells <paramref name="hull"/>'s data at <paramref name="port"/>: what its faction's maps lack is charted for it and
		/// paid into the hull's owners' treasury; what they have already is kept aboard to sell elsewhere. Returns the pay
		/// (0: nothing sold), with how many reports went.
		/// </summary>
		double SellAt(const cs::Ref<World>& world, const cs::Ref<Settlement>& port, const cs::Ref<Ship>& hull, double jd, int& reports);

		/// <summary>
		/// A find by the embodied <paramref name="hull"/> in <paramref name="world"/>: data aboard worth <paramref name="value"/>
		/// at the base, raised by how far past charted space it is; the first discovery credited in the news (this system's
		/// log, or the deep-space log with no system), in the pilot's story, and with a little Influence. False if it was
		/// surveyed already.
		/// </summary>
		bool Record(Galaxy& galaxy, const cs::Ref<World>& world, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& hull, SurveyKind kind,
			const cs::String& key, const cs::String& subject, const cs::String& place, double baseValue, double distanceLy, double jd, int body = -1);

		/// <summary>A star discovered by the pilot's lookout (<see cref="Galaxy.Discover"/>): data aboard the embodied hull.</summary>
		void StarFound(Galaxy& galaxy, const cs::String& star, double distanceLy, double jd);

		/// <summary>
		/// Runs with the galaxy: drops data aboard lost hulls, then (with a hull at a helm in a system) surveys the bodies,
		/// void sights and graveyards within range of where the helm reported it, and the anomalies it has seen through.
		/// </summary>
		void Update(Galaxy& galaxy, double jd);

		/// <summary>Runs <see cref="Update"/> on the galaxy's ledger, making it only once a hull is embodied somewhere (so a
		/// galaxy nobody flies in, the Bench's, never gains one).</summary>
		static void Run(Galaxy& galaxy, double jd);

		/// <summary>The base worth of a kind of find (before distance), credits.</summary>
		static double BaseValue(SurveyKind kind);

		/// <summary>How much more data is worth per light-year past the known neighbourhood: one drive jump (7 ly) doubles it.</summary>
		static double DistanceFactor(double distanceLy);

		/// <summary>The notices since the last call, oldest first; the list is emptied.</summary>
		cs::List<cs::String> TakeNotices();

		SG_SERIALIZABLE(Exploration);
	};
}
