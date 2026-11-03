#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include <utility>

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;

	/// <summary>One hostile group running the player's ship down (see <see cref="Pursuit"/>).</summary>
	class Pursuer final
	{
	public:
		cs::Ref<Ship> Leader;
		cs::List<cs::Ref<Ship>> Ships;

		/// <summary>The group's position and velocity, km and km/s from the pilot's frame body, ecliptic axes.</summary>
		DVec3 Pos, Vel;

		/// <summary>Seconds since it made contact.</summary>
		double Seconds = 0;

		/// <summary>Seconds its interdiction EMP has been charging, or below 0 while it is not.</summary>
		double EmpCharge = -1;

		/// <summary>Seconds until its EMP can charge again.</summary>
		double EmpReadyIn = 0;

		cs::Ref<SpaceGod::Core::Faction> Faction() const;
	};

	/// <summary>What happened in a chase this step, for the helm to show and act on.</summary>
	enum class PursuitEvent : int
	{
		/// <summary>A group has seen the player and turned to run it down: it shows on the scanners.</summary>
		Contact,

		/// <summary>Its EMP is charging: the player has <see cref="Pursuit.EmpChargeSeconds"/> to get out of reach.</summary>
		EmpCharging,

		/// <summary>The EMP went off with the player inside it: drives and cruise down for <see cref="Pursuit.EmpStunSeconds"/>.</summary>
		EmpHit,

		/// <summary>The EMP went off with the player clear of it.</summary>
		EmpMissed,

		/// <summary>The group is close enough to fight: the battle starts here.</summary>
		Engage,

		/// <summary>The group has given the chase up and gone back to its business.</summary>
		GaveUp,
	};
	const char* ToString(PursuitEvent v);

	/// <summary>
	/// Hostiles that see the player's ship and run it down (U.SPC-T273). Every armed hostile group in the system whose
	/// scanners can make out the piloted ship may turn to chase it: it drops off its schedule and shows on the player's
	/// scanners at <see cref="ContactKm"/> on its real bearing, as if it had cruised in, then closes at up to
	/// <see cref="TopSpeedKmS"/> (faster than any hull's cruise, so a chase can be stretched but not outrun for ever).
	/// Within <see cref="EmpRangeKm"/> it charges an interdiction EMP that knocks the player's drives out if it goes off
	/// with the player still in reach, and within <see cref="EngageKm"/> the fight begins where the player is, anywhere
	/// in open space. A chase that runs too long or too far is given up and the group goes back to its schedule.
	/// </summary>
	class Pursuit final
	{
	public:
		/// <summary>How far off a chasing group shows up, km, and how far off it gives up.</summary>
		static constexpr double ContactKm = 320; static constexpr double GiveUpKm = 2500;

		/// <summary>A chasing group's top speed, km/s, and its acceleration, km/s².</summary>
		static constexpr double TopSpeedKmS = 6; static constexpr double AccelKmS2 = 0.5;

		/// <summary>How near the EMP reaches when it charges, km (it must still be within a quarter more when it goes off),
		/// how long it charges, how long it knocks the drives out, and how long before it can charge again, seconds.</summary>
		static constexpr double EmpRangeKm = 30; static constexpr double EmpChargeSeconds = 3; static constexpr double EmpStunSeconds = 7; static constexpr double EmpCooldownSeconds = 25;

		/// <summary>How near a chasing group must close to start the fight, km.</summary>
		static constexpr double EngageKm = 15;

		/// <summary>How long a chase goes on before it is given up, seconds.</summary>
		static constexpr double MaxChaseSeconds = 360;

		/// <summary>How far off a hostile can be to notice the player at all, km (its scanners must also make it out).</summary>
		static constexpr double NoticeKm = 2e6;

		/// <summary>The most groups that chase the player at once.</summary>
		static constexpr int MaxGroups = 2;

		/// <summary>Days before a group that has chased the player will do so again.</summary>
		static constexpr double RechaseDays = 0.5;

	private:
		/// <summary>Spacing between a chasing group's ships, km.</summary>
		static constexpr double FormationKm = 0.6;

		cs::Ref<World> world;
		cs::Random rng;
		cs::Dictionary<cs::Ref<Ship>, double> lastChased;

	public:
		cs::List<cs::Ref<Pursuer>> Groups;

		/// <summary>The body whose frame the chase is reckoned in: the pilot's.</summary>
		int Body = -1;

		Pursuit(cs::Ref<World> world, int seed);

		bool Active() const { return Groups.Count() > 0; }

		/// <summary>The chasing group <paramref name="s"/> is in, or null.</summary>
		cs::Ref<Pursuer> Of(const cs::Ref<Ship>& s) const;

		/// <summary>Where <paramref name="s"/> of chasing group <paramref name="g"/> is, km from the frame body: the leader at
		/// the group's position, the rest in loose formation about it.</summary>
		static DVec3 OffsetOf(const cs::Ref<Pursuer>& g, const cs::Ref<Ship>& s);

		/// <summary>Where a chasing ship is, heliocentric km, or null if it is not chasing.</summary>
		cs::Nullable<DVec3> HelioOf(const cs::Ref<Ship>& s, double jd) const;

		/// <summary>
		/// Looks for hostile groups that see the piloted ship and turn to run it down; each that does starts at
		/// <see cref="ContactKm"/> on its own bearing. Returns the groups that made contact.
		/// </summary>
		cs::List<cs::Ref<Pursuer>> Consider(const cs::Ref<Ship>& pilot, int body, DVec3 pilotOffset, DVec3 pilotVel, double jd);

	private:
		/// <summary>Raiders are the keenest to run a ship down; a navy at war with its flag only somewhat less.</summary>
		static double ChaseChance(const cs::Ref<Ship>& s);

		/// <summary>Whether <paramref name="s"/> may turn to run the pilot down off <paramref name="body"/>: an armed, sound
		/// hostile group free of other business, and commanded to fight there (U.SPC-T371, see <see cref="War.Commanded"/>):
		/// a group passing through, or off its beat, lets a hostile hull go by.</summary>
		bool MayChase(const cs::Ref<Ship>& s, const cs::Ref<Ship>& pilot, int body, double jd) const;

		/// <summary>Drops a ship from the rechase cooldown once its cooldown is over (<see cref="MayChase"/> would let
		/// it chase again anyway) or it is gone for good (destroyed, or through a gate), so the book does not grow
		/// for ever as a long game runs on (U.SPC-T321).</summary>
		void PruneLastChased(double jd);

	public:
		/// <summary>Puts <paramref name="leader"/>'s group on the chase at <paramref name="at"/> (km from the frame body),
		/// matching the pilot's velocity: the schedule stops moving it (see <see cref="World"/>'s hold).</summary>
		cs::Ref<Pursuer> Begin(const cs::Ref<Ship>& leader, DVec3 at, DVec3 pilotVel, double jd);

		/// <summary>
		/// Every chasing group closes on the pilot (in <paramref name="body"/>'s frame, at <paramref name="pilotOffset"/>,
		/// moving at <paramref name="pilotVel"/>) for <paramref name="dt"/> real seconds: matching its velocity and closing
		/// as fast as it can, slowing to come up alongside rather than overshoot, charging its EMP in reach and setting it
		/// off, and engaging close in. The events, in order.
		/// <para>Run every frame (far more often than <see cref="Consider"/>'s periodic look), so it must notice for
		/// itself when the pilot has moved into another body's frame since the chase began: a stale <see cref="Pursuer.Pos"/>
		/// reckoned from the old frame is meaningless against an offset now reckoned from the new one, so rather than
		/// steer and judge distance on that nonsense, the chase is simply given up, the same as <see cref="Consider"/>
		/// already does when it notices <see cref="Body"/> has changed.</para>
		/// </summary>
		// C#'s List<(Pursuer group, PursuitEvent what)>: first is the group, second what happened.
		cs::List<std::pair<cs::Ref<Pursuer>, PursuitEvent>> Step(const cs::Ref<Ship>& pilot, int body, DVec3 pilotOffset, DVec3 pilotVel, double dt, double jd);

		/// <summary>Takes a group off the chase for the battle that has taken it in (it is not rescheduled).</summary>
		void Engaged(const cs::Ref<Pursuer>& g);

		/// <summary>A group gives the chase up: each ship goes back on its schedule from where it is now.</summary>
		void Release(const cs::Ref<Pursuer>& g, double jd);

		/// <summary>Every chase is given up (the pilot has left the frame, the helm, or the system).</summary>
		void ReleaseAll(double jd);
	};
}
