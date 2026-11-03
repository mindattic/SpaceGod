#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Radiators.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	class BattleUnit;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;
	class SubsystemDef;
	class ToggleSetting;

	/// <summary>
	/// Radiators and heat on big ships (U.SPC-T491). A capital ship's reactor, engines and guns build heat, and it sheds it
	/// through radiator panels mounted out on its hull (<see cref="SubsystemKind.Radiator"/>), with a little more through
	/// the hull's own skin. With its radiators whole a ship sheds everything it can make, fighting flat out under full
	/// burn, so heat is never a worry for it. The panels are thin and spread out to the sky, though: a weak point, and
	/// gunners go for them, the more so on a hull already running hot. A ship that has lost enough of them builds heat
	/// (<see cref="BattleUnit.HullHeat"/>, 1 at its sinks' limit); past <see cref="ThrottleAt"/> its crew throttles its guns'
	/// reloading and its drive (<see cref="Throttle"/>) to hold it, and a ship that runs hot instead (one running for its
	/// life, or a player who orders it, <see cref="BroadsideCommand.RunHot"/>) goes past the limit, where its crew and
	/// systems cook: hands lost and its guns, drives and shield generators burned out.
	///
	/// Which hulls carry panels, and where, is data (<see cref="Register"/>): a class with none (destroyers and
	/// everything smaller) sheds what it makes through its skin and has no heat to manage. Every figure is a setting in
	/// Settings > Broadsides.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API Radiators
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& RadiatorShed();

		static const cs::Ref<RangeSetting>& SkinShed();

		static const cs::Ref<RangeSetting>& WeaponHeat();

		static const cs::Ref<RangeSetting>& EngineHeat();

		static const cs::Ref<RangeSetting>& SinkSeconds();

		static const cs::Ref<RangeSetting>& ThrottleAt();

		static const cs::Ref<RangeSetting>& MinThrottle();

		static const cs::Ref<RangeSetting>& CrewLoss();

		static const cs::Ref<RangeSetting>& SystemBurn();

		static const cs::Ref<ToggleSetting>& AiFleesHot();

		// ------------------------------------------------------------ rules

		/// <summary>A panel's toughness: a thin sheet out in the open, a fraction of a gun mount's (a heavy laser's hit
		/// nearly takes one away).</summary>
		static constexpr double PanelHp = 140;

		/// <summary>What the reactor makes at idle, on the same scale as the settings' figures.</summary>
		static constexpr double ReactorHeat = 0.1;

		/// <summary>The furthest past the limit the heat goes: past this it is all burning out what makes it.</summary>
		static constexpr double MaxHeat = 1.5;

		/// <summary>How much more gunners favour a panel on a hull running at its limit than on a cold one.</summary>
		static constexpr double HotTargetBonus = 2;

	private:
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<SubsystemDef>>>& Panels();

		/// <summary>Port and starboard panels at each of <paramref name="zs"/>, <paramref name="x"/> out from the keel.</summary>
		static cs::List<cs::Ref<SubsystemDef>> Pairs(double x, double y, const cs::List<double>& zs);

	public:
		/// <summary>The radiator panels a class is designed with (none for a class that sheds through its skin).</summary>
		static const cs::List<cs::Ref<SubsystemDef>>& PanelsFor(ShipClass c);

		/// <summary>Sets (or, with null, removes) a class's radiator panels. A design built after this carries them;
		/// <see cref="CombatSpec"/> fits the stock designs' when it first builds them.</summary>
		static void Register(ShipClass c, cs::Nullable<cs::List<cs::Ref<SubsystemDef>>> panels);

		/// <summary><paramref name="layout"/> with <paramref name="c"/>'s panels added after its own hardpoints (unchanged if it
		/// has them already, or the class has none).</summary>
		static cs::List<cs::Ref<SubsystemDef>> Fit(ShipClass c, const cs::List<cs::Ref<SubsystemDef>>& layout);

		/// <summary>Whether the hull manages heat at all: one built with radiator panels.</summary>
		static bool Carries(const cs::Ref<ShipCombat>& c);

		/// <summary>The share of its panels' cooling the hull still has (by their grades), 1 for a hull with none.</summary>
		static double Intact(const cs::Ref<ShipCombat>& c);

		/// <summary>Heat the hull sheds a second, on the settings' scale: its skin's and its working panels'.</summary>
		static double Shed(const cs::Ref<ShipCombat>& c);

		/// <summary>
		/// Heat the hull makes a second, on the settings' scale: the reactor's, its guns' and launchers' (each one
		/// reloading makes its share by firepower, scaled by how fast it is reloading, <paramref name="reload"/>), and its
		/// drive's (by how near its full battle speed it is going).
		/// </summary>
		static double Made(const cs::Ref<BattleUnit>& u, double reload);

		/// <summary>Whether the hull keeps its guns and drive at full however hot it gets: ordered to
		/// (<see cref="BroadsideCommand.RunHot"/>), or an AI captain running for its life (<see cref="AiFleesHot"/>).</summary>
		static bool RunsHot(const cs::Ref<BattleUnit>& u);

		/// <summary>How hard the crew lets the guns reload and the drive push at the hull's heat: 1 below
		/// <see cref="ThrottleAt"/>, down to <see cref="MinThrottle"/> at the sinks' limit; 1 always for a hull running hot
		/// or one with no radiators to manage.</summary>
		static double Throttle(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// One battle step of the hull's heat: it fills by what is made less what is shed, over the sinks' size, and empties
		/// the same way. Past the limit the crew loses hands and every working gun, drive and shield generator burns
		/// (those burned out are added to <paramref name="burnedOut"/>, if given). <paramref name="reload"/> is how fast the
		/// guns are reloading this step before the heat's own throttle. Nothing for a hull without radiators.
		/// </summary>
		static void Step(const cs::Ref<BattleUnit>& u, double dt, double reload = 1, cs::List<cs::Ref<Subsystem>>* burnedOut = nullptr);

	private:
		/// <summary>What heat burns out: the guns, drives and shield generators it pours through. Never the reactor (its
		/// own safeties scram it short of that), the bridge or the panels themselves.</summary>
		static bool Cooks(const cs::Ref<Subsystem>& s);

	public:
		/// <summary>How much gunners want a panel on <paramref name="target"/>: some, and more as the hull runs hotter (it
		/// shows, and they know what it means).</summary>
		static double TargetWeight(const cs::Ref<BattleUnit>& target);
		SG_SERIALIZABLE(Radiators); // ObjectGraph registration (Persistence/Registrations)
	};
}
