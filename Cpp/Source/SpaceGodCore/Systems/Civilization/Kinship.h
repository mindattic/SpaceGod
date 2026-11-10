#pragma once
// US-T277: Kinship, the family side of the Civilization layer (Civilization.h).
//  - Feuds: a buyout, a scandal, a friendship gone bad can set two houses against each other. A feud runs hot or cold;
//    while hot, one house sabotages the other's business (and, across flags, sours the two states), until it cools, a
//    marriage between them ends it, or one house is gone. Underworld (US-T280) hires killers for the hottest.
//  - Inheritance: when a head dies leaving more than one grown heir to a fortune, they may quarrel over it; a quarrel can
//    split the house in two, the younger founding a house and a firm of their own with their share (and a feud, often).
//  - Affairs and divorce: the charming and the reckless stray; found out, a marriage may end, undoing the warmth a wedding
//    across flags gave, and the betrayed house may take it up as a feud. Years on, a hidden heir may come forward.
//  - Schools: the big learned worlds found academies; the young of rich houses study there four years and come out
//    abler, which their businesses feel; a teacher becomes their mentor.
//  - Celebrities: the charming, the gifted and those who want fame build a name; past a point they are famous, endorse
//    businesses in the commercials (which pays), and may fall from grace.
// Deterministic like the rest: fixed hashes of ids and days, a whole-day grid.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Citizen;
	class Civilization;
	class Galaxy;
	class Household;
	class Society;

	/// <summary>Two houses at daggers drawn (US-T277).</summary>
	class SPACEGODCORE_API Feud final
	{
	public:
		int Id = 0;
		cs::Ref<Household> A, B;
		/// <summary>0..1: how hot it runs; it cools a little every month.</summary>
		double Heat = 0.5;
		cs::String Cause;
		int Incidents = 0;
		double StartedJd = 0, EndedJd = cs::Double::NaN;
		bool Steered = false;
		bool Active() const { return cs::Double::IsNaN(EndedJd); }
		bool Between(const cs::Ref<Household>& x, const cs::Ref<Household>& y) const { return (A == x && B == y) || (A == y && B == x); }
	};

	/// <summary>An affair under way (US-T277).</summary>
	class SPACEGODCORE_API Affair final
	{
	public:
		cs::Ref<Citizen> Who, Whom;
		double StartedJd = 0;
		/// <summary>When it came out (NaN: secret still), and when a child of it may come forward (NaN: none).</summary>
		double FoundOutJd = cs::Double::NaN, HeirDueJd = cs::Double::NaN;
	};

	class SPACEGODCORE_API Kinship final
	{
	public:
		static constexpr int MaxSchools = 12;
		static constexpr double StudyYears = 4;
		static constexpr double FameThreshold = 0.6;

		cs::List<cs::Ref<Feud>> Feuds = cs::List<cs::Ref<Feud>>();
		cs::List<cs::Ref<Affair>> Affairs = cs::List<cs::Ref<Affair>>();

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		cs::Ref<Feud> FeudBetween(const cs::Ref<Household>& a, const cs::Ref<Household>& b) const;
		/// <summary>The hottest feud a house is in, or null.</summary>
		cs::Ref<Feud> FeudOf(const cs::Ref<Household>& h) const;

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Declare(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, double jd);
		cs::String Reconcile(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, double jd);
		cs::String Fame(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& c, double jd);
		cs::String Seduce(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b, double jd);

	private:
		int seenEvent = 0;

		cs::Ref<Feud> Start(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, const cs::String& cause, double heat, double jd, bool steered);
		void End(Civilization& civ, Galaxy& galaxy, const cs::Ref<Feud>& f, double jd);
		void React(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Inherit(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& h, double jd);
		void Feuding(Civilization& civ, Galaxy& galaxy, double jd);
		void Straying(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Schooling(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Renown(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		SG_SERIALIZABLE(Kinship);
	};
}
