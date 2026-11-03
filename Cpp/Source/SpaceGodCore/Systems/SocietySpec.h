#pragma once
// Port of Assets/UnitySpace/Core/Systems/SocietySpec.cs (US-T17). The World partial-class members this file adds
// (CreateSettled, BalanceSupply, ExpectedRunning, SupplyMargin) belong in World.h's declaration of World; they are
// defined in SocietySpec.cpp.

#include "Cs/Cs.h"
#include "BodyDefinition.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	class SystemSpec;
	class StationSpec;

	/// <summary>Which of a settled system's powers a settlement or ship belongs to.</summary>
	enum class SocietyRole : int
	{
		/// <summary>The government, with the system's navy.</summary>
		Authority,

		/// <summary>The mining and industrial concern, with no warships of its own.</summary>
		Company,

		/// <summary>The raiders, at their haven.</summary>
		Raiders,
	};
	const char* ToString(SocietyRole v);

	/// <summary>One of a settled system's factions (game layer).</summary>
	class FactionSpec final
	{
	public:
		cs::String Name, Prefix;
		Rgb Color;

		/// <summary>Treasury at the nearest frontier, credits; scaled with the system (<see cref="SocietySpec.Scale"/>).</summary>
		double Treasury = 4e8;

		double Marines = 1;

		FactionSpec(const cs::String& name, const cs::String& prefix, Rgb color, double treasury = 4e8, double marines = 1);
	};

	/// <summary>
	/// A settlement as the society plans it: where it is, whose, how many people (before the frontier
	/// scaling, see <see cref="SocietySpec.Scale"/>), what it digs and what it makes. Mines and the
	/// factories that feed the system are sized by <see cref="World.CreateSettled"/> to what the
	/// system uses; only the yards and munitions plants, which serve ships rather than people, are
	/// given a level here.
	/// </summary>
	class SettlementSpec final
	{
	public:
		cs::String Name, Body;
		SettlementKind Kind = static_cast<SettlementKind>(0);
		SocietyRole Owner = SocietyRole::Authority;
		double Population = 0;
		cs::List<Good> Mines;
		cs::List<IndustryKind> Makes;

		/// <summary>Shipyard and munitions plant levels (at the nearest frontier; scaled like the people).</summary>
		double Shipyard = 0, Munitions = 0;

		bool Hidden = false;

		/// <summary>A lawless station with a black market (U.SPC-T94, see <see cref="Settlement.Lawless"/>).</summary>
		bool Lawless = false;

		/// <summary>Why it is where it is (what makes the place plausible).</summary>
		cs::String Note;
	};

	/// <summary>
	/// A nearby system's society (U.SPC-T90): its powers, settlements, the invented stations they
	/// live on, its hyperlanes and the names of its ships. All of it is fiction, placed on the real
	/// bodies of the system's catalogue entry (or on stations in orbit where none will do).
	/// </summary>
	class SocietySpec final
	{
	public:
		/// <summary>The system it settles (<see cref="SystemSpec.Name"/>).</summary>
		cs::String System;

		/// <summary>The system's distance from Sol, light-years.</summary>
		double DistanceLy = 0;

		cs::Ref<FactionSpec> Authority, Company, Traders, Raiders;

		cs::List<cs::Ref<SettlementSpec>> Settlements;

		/// <summary>The invented stations the settlements need, added to the system's bodies.</summary>
		cs::List<cs::Ref<StationSpec>> Stations;

		/// <summary>Hides in the system's belts and discs where a pirate clan could one day found a haven (stations named in
		/// <see cref="Stations"/>, empty at the start; U.SPC-T94).</summary>
		cs::List<cs::String> HavenSites;

		/// <summary>The C# (string a, string b, double bend) tuple.</summary>
		struct LaneTuple { cs::String a, b; double bend = 0; };
		cs::List<LaneTuple> Lanes;

		cs::List<cs::String> Freighters, Haulers, Couriers;
		cs::List<cs::String> Yachts, Prospectors;

		/// <summary>Its deep-space explorers (U.SPC-T419): free survey hulls with an FTL drive of their own.</summary>
		cs::List<cs::String> Explorers;
		// C# null (no names of its own) is the empty list here.
		cs::List<cs::String> RaiderNames;

		/// <summary>The navy's ships: leader first, then escorts (the first is a cruiser in a big system, a destroyer in a small one).</summary>
		cs::List<cs::String> Navy;

		/// <summary>The companies: name, house mark, home port, flag, whether it is in the ore trade.</summary>
		struct CompanyTuple { cs::String name, tag, home; SocietyRole flag = SocietyRole::Authority; bool mining = false; };
		cs::List<CompanyTuple> Companies;

		cs::String Summary;

		/// <summary>How built up a system this far out is (<see cref="Frontier"/>).</summary>
		double Scale() const { return Frontier(DistanceLy); }

		/// <summary>
		/// How thickly settled a system is at <paramref name="ly"/> from Sol, against Alpha Centauri (1 at its
		/// 4.34 ly): falling with the square of the distance, as the cost of the crossing and of everything
		/// shipped out does, so Barnard's Star at 6 ly is half as built up and the 12 ly frontier an eighth.
		/// </summary>
		static double Frontier(double ly);

		/// <summary>Total people once scaled.</summary>
		double Population() const;

		/// <summary>Adds the society's stations to the system's catalogue entry (once).</summary>
		void AddStations(const cs::Ref<SystemSpec>& spec) const;

		cs::Ref<SettlementSpec> Add(const cs::String& name, const cs::String& body, SettlementKind kind, SocietyRole owner, double population, const cs::String& note,
			const cs::List<Good>& mines = {}, const cs::List<IndustryKind>& makes = {}, double shipyard = 0, double munitions = 0, bool hidden = false, bool lawless = false);

		void Station(const cs::String& name, const cs::String& host, double aAu, double meanAnomalyDeg, const cs::String& summary);

		void Lane(const cs::String& a, const cs::String& b, double bend) { Lanes.Add({ a, b, bend }); }

		/// <summary>A hide in a belt or disc, empty until a clan founds a haven there (U.SPC-T94): a station body of its own.</summary>
		void HavenSite(const cs::String& name, const cs::String& host, double aAu, double meanAnomalyDeg, const cs::String& summary);
	};

	// World (SocietySpec.cs part) -- to be declared inside World in World.h (integration):
	//   /// <summary>A nearby system settled by society (U.SPC-T90) ...</summary>
	//   static cs::Ref<World> CreateSettled(cs::Ref<Ephemeris> ephemeris, double jd, int seed, cs::Ref<SocietySpec> society, cs::Ref<Polities> polities = nullptr);
	//   static void BalanceSupply(const cs::List<cs::Ref<Settlement>>& settlements, const cs::String& system);
	//   private: static double ExpectedRunning(const cs::Ref<Settlement>& s, const cs::Ref<Facility>& f);
	//   public: static constexpr double SupplyMargin = 1.15;
}
