#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/People.cs (US-T19).

#include "Cs/Cs.h"

#include <tuple>

namespace SpaceGod::Core
{
	class Settlement;
	class Facility;
	class Diplomacy;
	enum class Good : int;
	enum class SettlementKind : int;

	/// <summary>
	/// The people side of the economy: what a person uses a day, how many of them work, who staffs
	/// which facility, what they earn and pay in tax, and where they move to. Every figure is listed,
	/// with its source or the reason it is assumed, in Economy/SOURCES.md.
	/// </summary>
	class People
	{
	public:
		// ---------------------------------------------------------------- needs

		/// <summary>Food eaten per person a day, kg (BVAD ISS planning value 2.39 kg packaged, less 0.43 kg packaging and 0.20 kg never opened; Ewert and Stromgren 2019).</summary>
		static constexpr double FoodKg = 1.76;

		/// <summary>Metabolic oxygen per person a day, kg (Ewert and Stromgren 2019, 82 kg astronaut exercising 90 minutes a day).</summary>
		static constexpr double OxygenKg = 0.89;

		/// <summary>Water a day per person the Sabatier reactor gets back from exhaled CO2, kg (Ewert and Stromgren 2019).</summary>
		static constexpr double SabatierWaterKg = 0.47;

		/// <summary>Cabin air lost to leaks per person a day, kg: the ISS design allowance of 0.23 kg/day spread over its crew of six.</summary>
		static constexpr double LeakAirKg = 0.23 / 6;

		/// <summary>Nitrogen's share of breathing air by mass (dry air: 75.5% N2, 23.1% O2).</summary>
		static constexpr double AirNitrogenShare = 0.755, AirOxygenShare = 0.231;

		/// <summary>Medical supplies per person a day, kg: between the clinical share (~7 g) and the whole (~47 g) of US hospital waste per head.</summary>
		static constexpr double MedicalKg = 0.020;

		/// <summary>Water each person draws from the loop a day, kg: the ISS-like habitat budget (5.03 kg, Ewert and Stromgren 2019), or the UN's 50 litres for basic domestic needs in a colony with showers and laundry.</summary>
		static double GrossWaterKg(SettlementKind kind);

		/// <summary>Share of that water the loop recovers: 98% with the ISS brine processor (2023), 93.5% before it; a haven's patched kit is assumed to manage 85%.</summary>
		static double WaterRecovery(SettlementKind kind);

		/// <summary>
		/// Water lost per person a day, kg: what the loop does not recover, plus the water electrolysed
		/// for oxygen (metabolic need and leaked air, 9 kg of water per 8 kg of O2) less what the Sabatier
		/// reactor returns.
		/// </summary>
		static double WaterLossKg(SettlementKind kind);

		/// <summary>Nitrogen bled in a day per person to replace leaked air, kg.</summary>
		static constexpr double NitrogenKg = LeakAirKg * AirNitrogenShare;

	private:
		static const cs::List<cs::List<std::tuple<Good, double>>>& needs();

		static cs::List<cs::List<std::tuple<Good, double>>> BuildNeeds();

	public:
		/// <summary>Daily per-person needs in tonnes for a kind of settlement (built once, read without locking).</summary>
		static const cs::List<std::tuple<Good, double>>& NeedsOf(SettlementKind kind);

		/// <summary>A person's daily need of <paramref name="g"/> at a kind of settlement, tonnes (0 if it is not a need).</summary>
		static double NeedOf(SettlementKind kind, Good g);

		// ---------------------------------------------------------------- growth

		/// <summary>Natural increase of a fed, watered and doctored settlement, per day (4%/yr, a frontier boom a little above today's fastest-growing countries).</summary>
		static constexpr double GrowthPerDay = 0.00011;

		/// <summary>Extra decline per day with no medicine at all.</summary>
		static constexpr double SicknessPerDay = 0.0002;

		/// <summary>Decline per day per unit of unmet food or water below the 98% line.</summary>
		static constexpr double FaminePerDay = 0.004;

		/// <summary>A settlement never shrinks below this share of its founding population (a hard core stays), nor grows past <see cref="MaxGrowth"/> times it.</summary>
		static constexpr double MinShare = 0.25, MaxGrowth = 4;

		/// <summary>Population growth rate per day from how well needs were met this tick.</summary>
		static double GrowthRate(double sustenance, double health);

		/// <summary>Days over which <see cref="Settlement.Wellbeing"/> follows what people actually got.</summary>
		static constexpr double WellbeingDays = 30;

		// ---------------------------------------------------------------- labour

		/// <summary>Share of everyone who is in the labour force (ILO: about 3.6 billion workers of 8.1 billion people, 2023).</summary>
		static constexpr double LabourForceShare = 0.45;

		/// <summary>Share of the labour force free for the facilities; the rest run services (ILO: services are about half of world employment).</summary>
		static constexpr double IndustrialShare = 0.5;

		/// <summary>A facility with less than this share of its crew cannot run at all.</summary>
		static constexpr double MinimumCrew = 0.25;

		/// <summary>Share of a facility's value added paid as wages (ILO: the global labour income share is about 52%).</summary>
		static constexpr double LabourShare = 0.52;

		/// <summary>Tax the owner takes on wages (OECD average tax wedge on labour, about 35%).</summary>
		static constexpr double WageTaxRate = 0.35;

		/// <summary>A service job pays this share of the system's average facility wage (assumed).</summary>
		static constexpr double ServiceWageShare = 0.5;

		/// <summary>Workers available to the facilities at a settlement.</summary>
		static double Labour(const cs::Ref<Settlement>& s);

		/// <summary>
		/// Shares the labour out among the facilities: life support (farms and water) is crewed first,
		/// then everything else in proportion. Sets each facility's <see cref="Facility.Staffing"/> and the
		/// settlement's overall <see cref="Settlement.Staffing"/>.
		/// </summary>
		static void AssignLabour(const cs::Ref<Settlement>& s);

		/// <summary>How much of its capacity a facility can run with its crew: in proportion, and not at all below <see cref="MinimumCrew"/>.</summary>
		static double CrewFactor(double staffing);

		/// <summary>Value added a day by a facility at local prices at full capacity: outputs less inputs (never negative).</summary>
		static double ValueAdded(const cs::Ref<Settlement>& s, const cs::Ref<Facility>& f);

		/// <summary>The facility wage a day per filled job at <paramref name="s"/> (0 with nobody at work).</summary>
		static double Wage(const cs::Ref<Settlement>& s);

		/// <summary>
		/// Expected daily earnings of a member of the labour force (Harris and Todaro 1970): the facility
		/// wage times the chance of holding a facility job, the rest at the service wage.
		/// </summary>
		static double ExpectedEarnings(const cs::Ref<Settlement>& s, double serviceWage);

		// ---------------------------------------------------------------- migration

		/// <summary>Migration rate: the share of a population a day that moves for a full unit gap in attraction, before weighting by destination size.</summary>
		static constexpr double MigrationPerDay = 0.002;

		/// <summary>The most people a settlement holds as <see cref="Settlement.Overflow"/>, as a share of its population: past it, those waiting give up.</summary>
		static constexpr double OverflowShare = 0.05;

		/// <summary>A settlement past this share of its cap is crowded (see <see cref="Crowding"/>).</summary>
		static constexpr double CrowdedShare = 0.6;

		/// <summary>
		/// How crowded <paramref name="s"/> is, 0..1: 0 up to <see cref="CrowdedShare"/> of its cap, rising to 1 at the
		/// cap. That share of its growth would rather leave than squeeze in, and is counted as <see cref="Settlement.Overflow"/>.
		/// </summary>
		static double Crowding(const cs::Ref<Settlement>& s);

		/// <summary>Counts <paramref name="people"/> more at <paramref name="s"/> with no room (U.SPC-T98), up to <see cref="OverflowShare"/> of its people.</summary>
		static void AddOverflow(const cs::Ref<Settlement>& s, double people);

		/// <summary>Gaps in attraction smaller than this move nobody: the cost and bother of moving.</summary>
		static constexpr double MigrationThreshold = 0.05;

		/// <summary>
		/// A place's pull on migrants: its living standard (wellbeing, with medicine) times how its
		/// expected earnings compare with the system's, damped so that hunger matters more than pay.
		/// </summary>
		static double Attraction(const cs::Ref<Settlement>& s, double meanEarnings);

		/// <summary>Whether people can move between two settlements: neither empty, not at war, pirates only among pirates.</summary>
		static bool CanMigrate(const cs::Ref<Diplomacy>& diplomacy, const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b);

		/// <summary>
		/// Moves people over <paramref name="days"/> from where life is worse to where it is better:
		/// from each settlement toward every reachable one with a clearly higher pull, weighted by the
		/// destination's share of the reachable population (bigger places draw more). Nobody leaves a
		/// place below its floor or arrives at one already at its cap. Sets each settlement's
		/// <see cref="Settlement.Earnings"/>, <see cref="Settlement.Attraction"/> and
		/// <see cref="Settlement.NetMigration"/>.
		/// </summary>
		static void Migrate(const cs::List<cs::Ref<Settlement>>& settlements, const cs::Ref<Diplomacy>& diplomacy, double days);
	};
}
