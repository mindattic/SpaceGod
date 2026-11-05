#pragma once
// US-T135: the EVA repair worker, an embodiable damage control team out on a hull, and the weld timing that makes an
// embodied one (the SpaceGod) repair faster than the team would on its own.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Subsystem;

	/// <summary>
	/// The welding torch's timing (US-T135), the skill part of an embodied worker's repair: a marker sweeps a bar back and
	/// forth and the player taps the torch (left mouse) as it crosses the bright zone. A hit raises the weld's quality
	/// (0..1) and moves the zone, a little narrower on a streak; a miss halves it and the torch sputters for
	/// <see cref="StallS"/>, the repair back to the team's plain rate meanwhile. Quality fades while nothing is hit. The
	/// repair runs at <see cref="Multiplier"/> times the team's own rate: 1 + <see cref="FlatBonus"/> for the SpaceGod's
	/// hands at all, up to <see cref="SkillBonus"/> more for good welding. Deterministic (nothing drawn at random), so a
	/// co-op peer or a replay steps it the same on the same input.
	/// </summary>
	struct SPACEGODCORE_API WeldTiming
	{
		/// <summary>Sweeps of the bar a second (there and back is two).</summary>
		static constexpr double SweepHz = 0.75;
		static constexpr double QualityPerHit = 0.3;
		static constexpr double QualityDecayS = 0.1;
		static constexpr double StallS = 0.6;
		static constexpr double FlatBonus = 0.25;
		static constexpr double SkillBonus = 1.5;

		/// <summary>Where the sweep is, 0..2 (0..1 out, 1..2 back).</summary>
		double Phase = 0;
		double Quality = 0;
		int Hits = 0, Misses = 0, Streak = 0;
		double Stall = 0;

		/// <summary>The last tap's verdict, shown for <see cref="VerdictS"/> seconds.</summary>
		double VerdictS = 0;
		bool VerdictGood = false;
		bool PrimaryWas = false;

		/// <summary>The marker on the bar, 0..1.</summary>
		double Marker() const { return Phase <= 1 ? Phase : 2 - Phase; }
		double ZoneCentre() const;
		double ZoneHalf() const;

		/// <summary>One step: the sweep moves, and a tap of the torch this step (the trigger's press) is judged.</summary>
		void Step(double dt, bool primary);

		/// <summary>The repair rate's multiple of the team's own while the SpaceGod holds the torch.</summary>
		double Multiplier() const;

		void Reset() { *this = WeldTiming(); }
	};

	/// <summary>
	/// An EVA repair worker (US-T135): one of a hull's damage control teams (<see cref="Broadsides.Teams"/>), suited up out
	/// on its plating, as an <see cref="IEmbodiable"/>. Unheld, the team works inside with the others under damage control
	/// (<see cref="Broadsides.StepDamageControl"/>), exactly as before. Taken, it is out of that pool
	/// (<see cref="BattleUnit.EvaHeldTeams"/>) and the player crawls the hull where they look (W/S A/D, Shift the suit's jets)
	/// to a damaged hardpoint (a shield generator, an engine, a turret: the hull's <see cref="SubsystemDef"/> hardpoints) and
	/// welds it at the team's own rate times the torch's <see cref="WeldTiming.Multiplier"/>, so the SpaceGod repairs faster
	/// than the worker would alone, and faster still for good timing. A hardpoint merely damaged comes up to the field
	/// repair cap (<see cref="Broadsides.FieldRepairCap"/>); a shield emitter or gun mount shot away is brought back as the
	/// emergency repair crews do (<see cref="ShieldEmitters.Restorable"/>). Everything on the hull is in hull lengths, so it
	/// works the same at the game's and the true scale (<see cref="ScaleModel"/>). Only ever stepped while held: crews,
	/// the AI and the Bench never see it. On a station (whose crews do no AI repair in a fight, as in the C# Core this
	/// matches) the worker welds at the same team rate.
	/// <para>The hook for US-T82: boarding marines and the station workers of that story embody the same way, as kinds of
	/// their own found through <see cref="Embodiables.CrewOn"/>.</para>
	/// </summary>
	class SPACEGODCORE_API RepairWorker final : public IEmbodiable
	{
		cs::Ref<IEmbodiable> host;

	public:
		cs::Ref<Core::Battle> Battle;
		cs::Ref<BattleUnit> Unit;

		/// <summary>Which of the hull's damage control teams it is, 0 up.</summary>
		int Team = 0;

		/// <summary>Where it is on the hull: hull frame, hull lengths (as a hardpoint's X, Y, Z), on the plating.</summary>
		DVec3 Local;

		/// <summary>Which way it faces along the plating (hull frame, unit).</summary>
		DVec3 Facing = DVec3(0, 0, 1);

		/// <summary>The hardpoint in reach it is welding, or null; and the nearest damaged one anywhere on the hull (to crawl to).</summary>
		cs::Ref<Subsystem> Job, Nearest;
		double NearestLengths = 0;

		WeldTiming Weld;

		/// <summary>Hit points (and restoring work) put in while held, for the HUD.</summary>
		double Welded = 0;
		int Restored = 0;

	private:
		bool placed = false;

	public:
		/// <summary>Crawl speed, hull lengths a second; the suit's jets multiply it.</summary>
		static constexpr double CrawlLengthsS = 0.05;
		static constexpr double JetFactor = 2.5;

		/// <summary>How close to a hardpoint (hull lengths, plating to plating) it must be to weld it.</summary>
		static constexpr double ReachLengths = 0.06;

		RepairWorker(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit, int team, const cs::Ref<IEmbodiable>& host = nullptr);

		/// <summary>The teams a hull can send out: its damage control teams, at least one on a capital ship or station; none
		/// on anything smaller (a fighter has no EVA crew).</summary>
		static int TeamsOn(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="s"/> is work for a worker: damaged below the field repair cap, or shot away and restorable.</summary>
		static bool Repairable(const cs::Ref<Subsystem>& s);

		/// <summary>The team's own rate (hit points a second), as damage control works it: the AI's rate for one team.</summary>
		static double TeamHpS(const cs::Ref<BattleUnit>& u);

		/// <summary>Where on the plating a hardpoint is reached (hull lengths).</summary>
		static DVec3 SurfaceOf(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s);

		/// <summary>The nearest repairable hardpoint to <paramref name="local"/> on the plating, and how far (hull lengths).</summary>
		static cs::Ref<Subsystem> NearestJob(const cs::Ref<BattleUnit>& u, DVec3 local, double& lengths);

		/// <summary>How whole the job is, toward what a worker can bring it to (0..1).</summary>
		static double JobProgress(const cs::Ref<Subsystem>& s);

		cs::String Name() const override;
		EmbodimentKind Kind() const override { return EmbodimentKind::RepairWorker; }
		cs::Ref<Core::Faction> Faction() const override;

		/// <summary>The hull's helm, or the station it works on.</summary>
		cs::Ref<IEmbodiable> Host() const override { return host; }

		bool CanEmbody() const override { return Blocker().empty(); }
		cs::String Blocker() const override;
		bool Embodied() const override;
		EmbodimentPose PoseAt(double jd) override;
		void TakeCharge() override;
		void Control(const EmbodimentInput& input, double dt) override;
		void HandBack() override;

		EmbodimentInputProfile InputProfile() const override { return EmbodimentInputProfile::HullWalker; }
		EmbodimentCameraRig CameraRig() const override;
		EmbodimentRolePanel RolePanel() override;
		EmbodimentSubject Subject() const override;

		bool Equals(const cs::Ref<IEmbodiable>& obj) const override;
		size_t GetHashCode() const override;

	private:
		/// <summary>Out of the airlock: the dorsal plating amidships, each team a little along the spine from the last.</summary>
		void Place();

		/// <summary>Puts <paramref name="work"/> hit points' worth into the job, as damage control would.</summary>
		void Put(double work);
	};
}
