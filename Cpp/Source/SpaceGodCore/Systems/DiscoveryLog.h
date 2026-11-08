#pragma once
// US-T259: the discovery log, what the player has found, kept to look back on and plan from (the Runtime's discoveries
// screen, Shift+O, shows it).
//
// The simulation already knows what has been found (Galaxy::Discoveries, Settlement::Discovered, a void station's sights,
// a place's graveyards, the named pilots met), but scattered, mostly undated, and nowhere a player can read it. This log
// gathers it into one dated list: the game sweeps it now and then (DiscoveryLog::Sweep) and every find not yet on it is
// written in with the day it was noticed (or the day the simulation itself dated it: a star's discovery, a pilot's meeting).
//
// Display only: the log reads the simulation and never changes it, and no simulation code reads the log, so the Bench
// digests never see it. It is saved with the Galaxy (Galaxy::DiscoveryLog, optional: absent from older saves). Every peer
// of a co-op game sweeps the same shared state and so keeps the same log, but for the bodies a player has scanned (opened
// on the scan card), which are that player's own.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <unordered_set>

namespace SpaceGod::Core
{
	class Galaxy;
	class StarSystem;

	/// <summary>What a find in the discovery log is (US-T259); the screen lists them by this, in this order.</summary>
	enum class DiscoveryKind : int
	{
		/// <summary>A star system charted (discovered by a lookout) or visited.</summary>
		Star,
		/// <summary>A planet, moon, dwarf planet, asteroid or station body scanned or come close to at the helm.</summary>
		Body,
		/// <summary>A colony, station port or mining outpost in a system the player has been in.</summary>
		Settlement,
		/// <summary>A pirate haven, once somebody has found it.</summary>
		Haven,
		/// <summary>A ship graveyard (fields of derelict hulks and old wrecks).</summary>
		Graveyard,
		/// <summary>A derelict station, on its own or in a graveyard.</summary>
		Derelict,
		/// <summary>A void station's sight (a tourist trap worth the detour) or its megastructure.</summary>
		VoidSight,
		/// <summary>A Bio Mass bloom, a Void Raider strike, a sign of the Watcher.</summary>
		Sighting,
		/// <summary>A named pilot met (hailed, embodied, fought or rescued).</summary>
		Pilot,
	};
	SPACEGODCORE_API const char* ToString(DiscoveryKind v);

	/// <summary>One find in the log (US-T259).</summary>
	class SPACEGODCORE_API DiscoveryEntry final
	{
	public:
		/// <summary>What it is.</summary>
		DiscoveryKind Kind = DiscoveryKind::Star;
		/// <summary>The log's own id for it (kind, system and name): one entry per thing found.</summary>
		cs::String Id;
		/// <summary>Its name as the game shows it.</summary>
		cs::String Name;
		/// <summary>The star system (or void station) it is in, or the star itself; "" for a sighting out between the stars.</summary>
		cs::String System;
		/// <summary>The body of <see cref="System"/>'s catalogue it is (a body) or is on (a settlement), else -1.</summary>
		int Body = -1;
		/// <summary>When it was found (or, for what the log notices late, when the log first noticed it).</summary>
		double Jd = 0;
		/// <summary>Who found it, when the simulation says ("the pilot", a group's name, a navy), else "".</summary>
		cs::String By;
		/// <summary>What the log says of it in a line, from the simulation's own words at the time (a sight's summary, a
		/// sighting's news line, how a pilot was met); "" when there is nothing more.</summary>
		cs::String Note;
		/// <summary>The Prose key of its description (Core::LoreKeys); "" for none.</summary>
		cs::String LoreKey;
		/// <summary>The named pilot's id (<see cref="NamedPilot.Id"/>) for a pilot, else 0.</summary>
		int PilotId = 0;

		SG_SERIALIZABLE(DiscoveryEntry);
	};

	/// <summary>
	/// The discovery log (US-T259): every find, dated, in the order it was written in. See the file's head.
	/// </summary>
	class SPACEGODCORE_API DiscoveryLog final
	{
	public:
		/// <summary>The finds, in the order they were written in (not necessarily by date: a pilot's meeting is dated when
		/// it happened, which may be before the log noticed).</summary>
		cs::List<cs::Ref<DiscoveryEntry>> Entries;

		/// <summary>Counts every change, so a view of the log knows to rebuild (not saved).</summary>
		int Version = 0;

		/// <summary>Whether a find with this id is on the log.</summary>
		bool Has(const cs::String& id) const;

		/// <summary>Writes a find in if its id is new; the new entry, or null when it was already there.</summary>
		cs::Ref<DiscoveryEntry> Record(DiscoveryKind kind, const cs::String& id, const cs::String& name, const cs::String& system, int body,
			double jd, const cs::String& by = cs::String(), const cs::String& note = cs::String(), const cs::String& loreKey = cs::String());

		/// <summary>The entry with this id, or null.</summary>
		cs::Ref<DiscoveryEntry> Find(const cs::String& id) const;

		/// <summary>How many finds of <paramref name="kind"/> the log holds.</summary>
		int CountOf(DiscoveryKind kind) const;

		/// <summary>The finds of <paramref name="kind"/>, newest first (ties in the order written).</summary>
		cs::List<cs::Ref<DiscoveryEntry>> Of(DiscoveryKind kind) const;

		/// <summary>What the game knows of where the player is, for a sweep.</summary>
		struct Place
		{
			/// <summary>The system the player is in (its settlements, graveyards and sights count as found), or null.</summary>
			cs::Ref<StarSystem> Here;
			/// <summary>The body of <see cref="Here"/> whose frame the helm is in (come close to), or -1.</summary>
			int NearBody = -1;
			/// <summary>The body of <see cref="Here"/> open on the scan card (selected), or -1.</summary>
			int ScannedBody = -1;
		};

		/// <summary>
		/// Writes in every find not yet on the log: the stars discovered (Galaxy::Discoveries, with their own day and finder),
		/// the system the player is in and, there, its settlements and found havens, graveyards, derelict stations, a void
		/// station's sights, a Bio Mass bloom, and the bodies scanned or come close to; the Void Raider strikes and Watcher
		/// signs on the deep-space log; and the named pilots met in every system (dated when met). Reads the simulation
		/// only. Returns how many finds it wrote in.
		/// </summary>
		int Sweep(const Galaxy& galaxy, const Place& place, double jd);

		/// <summary>
		/// The scanner's readings of a find, as the simulation stands now (a body's gravity and temperature, a settlement's
		/// people and flag, a pilot's rank and hull...), a line each; when the thing can no longer be found (a system let go,
		/// a pilot dead and gone) only what was written in at the time.
		/// </summary>
		static cs::List<cs::String> Readings(const Galaxy& galaxy, const DiscoveryEntry& e, double jd);

		/// <summary>The kind's heading on the screen ("Stars charted", "Pilots met").</summary>
		static const char* Heading(DiscoveryKind kind);

		/// <summary>How many kinds there are (the last kind's value plus one).</summary>
		static constexpr int KindCount = static_cast<int>(DiscoveryKind::Pilot) + 1;

	private:
		/// <summary>The ids of <see cref="Entries"/>, built again whenever it falls out of step (after a load). Not saved.</summary>
		mutable std::unordered_set<std::string> ids;
		mutable int indexed = -1;
		void Index() const;

		SG_SERIALIZABLE(DiscoveryLog);
	};
}
