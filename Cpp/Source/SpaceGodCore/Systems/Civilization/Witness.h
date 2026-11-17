#pragma once
// US-T292: Witness, the Civilization layer's eyes on the wider simulation (Civilization.h).
//
// The people of the galaxy live inside a simulation that runs whether anyone is watching: wars are declared and won,
// worlds change hands, pirate clans dig in and are burned out, markets break. Witness turns those happenings into
// SocialEvents so every module reacts to them the way it reacts to its own: a conquered world resents its new flag and
// grows a cell, its firms crash or are bought, its people flee; a war calls up the reserves and moves the shares; a peace
// brings the prisoners home and the chronicles out.
//
// It reads the Core simulation and never writes it. Once a day (first in the Civilization's order, so every module's
// React sees the day's news the same day) it compares a compact snapshot of what Core shows against yesterday's: who owns
// each settlement and whether it stands, its people, the wars, the pacts, the pirate havens, the market shocks in force,
// the states still standing and the battles each system has finished. Its first look only takes the snapshot, and a
// system it has never seen (one the galaxy has just built) is taken in quietly, so nothing floods.
//
// The SpaceGod's hand is the galaxy's own hand here: a world it takes, a war its commanded polity declares, a battle it
// flew in all reach the people through these same events, marked Steered where Core can tell (see Witness.cpp).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Civilization;
	class Conflict;
	class Faction;
	class Galaxy;
	class Pact;
	class Settlement;
	class SocialEvent;
	class StarSystem;
	enum class SocialKind : int;

	/// <summary>How one settlement stood when last seen (US-T292).</summary>
	class SPACEGODCORE_API WitnessedWorld final
	{
	public:
		cs::Ref<SpaceGod::Core::Settlement> Where;
		cs::Ref<StarSystem> System;
		cs::Ref<Faction> Owner, OccupiedFrom;
		/// <summary>Standing (owned, not a ruin); for a haven, not fallen.</summary>
		bool Stood = false;
		double Population = 0;
	};

	/// <summary>One star system as last seen (US-T292): the finished battles its war still lists.</summary>
	class SPACEGODCORE_API WitnessedSystem final
	{
	public:
		cs::Ref<StarSystem> System;
		cs::List<int> Battles = cs::List<int>();
	};

	class SPACEGODCORE_API Witness final
	{
	public:
		/// <summary>A settlement losing this share of its people in a day (and at least <see cref="DevastatedPeople"/>) is devastated.</summary>
		static constexpr double DevastatedShare = 0.25;
		static constexpr double DevastatedPeople = 5000;

		/// <summary>Whether the first snapshot has been taken.</summary>
		bool Primed = false;
		cs::List<cs::Ref<WitnessedWorld>> Worlds = cs::List<cs::Ref<WitnessedWorld>>();
		cs::List<cs::Ref<WitnessedSystem>> Systems = cs::List<cs::Ref<WitnessedSystem>>();
		cs::List<cs::Ref<Conflict>> Wars = cs::List<cs::Ref<Conflict>>();
		cs::List<cs::Ref<Pact>> Pacts = cs::List<cs::Ref<Pact>>();
		/// <summary>The states (no pirate clans) that held a standing settlement.</summary>
		cs::List<cs::Ref<Faction>> Standing = cs::List<cs::Ref<Faction>>();
		/// <summary>The market shocks in force, by key.</summary>
		cs::List<int> Shocks = cs::List<int>();
		/// <summary>Events witnessed, all time.</summary>
		int Witnessed = 0;

		void Day(Civilization& civ, Galaxy& galaxy, double jd);

		/// <summary>The polity the SpaceGod commands from the galaxy map (StrategicCommand.Player), or null.</summary>
		static cs::Ref<Faction> Commanded(const Galaxy& galaxy);
		/// <summary>Whether the kind is one Witness publishes (the people's view of a Core happening).</summary>
		static bool IsWitnessed(SocialKind k);

	private:
		void Settlements(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, const cs::List<cs::Ref<StarSystem>>& fresh, const cs::List<cs::Ref<StarSystem>>& flown);
		void WarsAndPacts(Civilization& civ, Galaxy& galaxy, double jd, bool quiet);
		void Battles(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, cs::List<cs::Ref<StarSystem>>& fresh, cs::List<cs::Ref<StarSystem>>& flown);
		void Markets(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, const cs::List<cs::Ref<StarSystem>>& fresh);
		void States(Civilization& civ, Galaxy& galaxy, double jd, bool quiet);
		void Tell(Civilization& civ, Galaxy& galaxy, const cs::Ref<SocialEvent>& e);

		/// <summary>Who took each state's last world this pass, and whether by the SpaceGod's hand (for FactionFallen).</summary>
		cs::List<cs::Ref<Faction>> lostTo = cs::List<cs::Ref<Faction>>();
		cs::List<cs::Ref<Faction>> lostBy = cs::List<cs::Ref<Faction>>();
		cs::List<cs::Ref<SpaceGod::Core::Settlement>> lostWhere = cs::List<cs::Ref<SpaceGod::Core::Settlement>>();
		cs::List<int> lostSteered = cs::List<int>();

		SG_SERIALIZABLE(Witness);
	};
}
