// US-T409 / US-T410 (docs/REWIND.md, "Hold to rewind"): the designer's short rewind. Members are declared in
// UnitySpaceApp.HoldRewind.inl.
//
// Two records are kept, both by a read-only observer at the end of each frame that ran the simulation (never in Core,
// never with a random draw, so AI-only runs and the Bench are what they were):
//   - tracks: each hull's, craft's, missile's, round's and decoy's pose in the fight in view (capitals at
//     HoldRewindTuning::CapitalHz, the rest at CraftHz, the hull held every frame) and the flown hull's helm state,
//     with whether each hull was dead, gone or docked, and the world's and the fight's clocks; they are what a hold
//     draws, played back smoothly (Hermite on position and velocity, the nose lerped and normalised:
//     docs/research/rewind-short-track.md);
//   - resume points: every HoldRewindTuning::AnchorS, the fight's whole state by value (the Battle, every unit, its
//     combat state, subsystems, reactor, fighter and broadside state, every missile, round, mine and decoy) and the
//     helm's flight state. Letting go puts the newest one at or before the moment reached back over the live objects
//     (what Sands of Time does for its actors), so play goes on from a real state, not from the drawn one.
// US-T410: the world's bookings from the fight in view (a hull lost, struck or taken: the war's hooks) wait until their
// moment is older than the window (or the fight ends, or a save is made), so a hold can bring a lost hull back; a resume
// drops the ones after it. US-T413: a craft's launch or landing is put back too (a carrier's own FlightDecks, a
// fighter's DeckVisit and the hangar/shuttle bay lists live outside the per-unit copies, so they are captured and
// restored the same way, by value). What still cannot be put back (a hull joining the fight, a seat changed, the
// fight's end, your own hull's fate) is a barrier: a resume never goes back past the last one. A resume never goes
// back past the previous resume either (the floor: a moment already rewound through is never revisited). While held,
// the world clock is set back for the drawing (planets, ships in orbit and on the lanes), and put back on letting go:
// the world outside the fight goes on from now.

#include "UnitySpaceApp.h"
#include "UI/Hud.h"
#include "Coop/CoopPeer.h"
#include "Coop/CoopSmoke.h" // US-T423: the co-op smoke's host key
#include "Coop/CoopWire.h"
#include "Session/LocalSession.h" // US-T423: every player's seat, for the co-op barrier
#include "Combat/War.h" // US-T423: the fight kept, by its index in the war's battles
#include "Input/KeyBindings.h"
#include "UnityCompat/UnityRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Ephemeris.h" // US-T412: Ephemeris()->Update(jd), so bodies rewind too (PlayHoldRewind)
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h" // US-T413: a carrier's decks and a craft's deck visit, outside the per-unit snapshot
#include "Combat/PilotFlight.h"
#include "Embodiment/Embodiables.h" // US-T414: HelmOf/TurretsOn, for the self-test's seat-change check
#include "Embodiment/MountedTurret.h" // US-T414: the full type, so Ref<MountedTurret> converts to Ref<IEmbodiable>
#include "Embodiment/MechWalker.h" // US-T416: a deployed mech's state, captured by value
#include "Fleet.h" // US-T418: the fleet's comings and goings through the gates
#include "BodyCatalog.h" // US-T424: the birth check's hull, off Uranus
#include "BodyDefinition.h"
#include "Misc/Paths.h" // US-T419: the self-test's screenshots
#include "UnrealClient.h"
#include "Session/ISession.h" // US-T414: the session's refusal events, for the self-test's diagnosis
#include "Session/SessionEvents.h"
#include "Embodiment/ShipHelm.h" // US-T414: likewise for Ref<ShipHelm>
#include "Target.h" // US-T414: Target::OfShip, for the self-test's EnterPiloting call
#include "Persistence/HoldRewindCurve.h"
#include "Systems/HelmFtl.h"
#include "Camera/StarTravel.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		using HrTuning = UnitySpaceApp::HoldRewindTuning;
		constexpr double HrNone = std::numeric_limits<double>::lowest();

		/// <summary>US-T423: in co-op the hold's steps are shared ones (UnitySpaceApp::coopShared above 0, undone however the
		/// step leaves): the helm is everyone's there, so every peer records, plays and resumes it alike.</summary>
		struct HrSharedScope
		{
			int& n;
			bool on;
			HrSharedScope(int& counter, bool when) : n(counter), on(when) { if (on) n++; }
			~HrSharedScope() { if (on) n--; }
		};

		/// <summary>One kept pose: when (lived real seconds, the ring's key), the fight's own clock then, where it was,
		/// how it moved, where its nose pointed, and whether it was dead, gone or docked.</summary>
		struct HrPose
		{
			double T = 0;
			double S = 0;
			DVec3 Pos, Vel, Fwd;
			bool Dead = false, Fled = false, Docked = false;
		};

		template <class T> struct HrTrack
		{
			cs::Ref<T> Obj;
			std::deque<HrPose> Poses;
			/// <summary>In the fight's list at the latest projectile sample (projectiles only).</summary>
			bool Alive = false;
		};

		/// <summary>The clocks at one lived moment: the fight's (S) and the world's (Jd).</summary>
		struct HrClockMark
		{
			double T = 0;
			double S = 0;
			double Jd = 0;
		};

		struct HrPilotPose
		{
			double T = 0;
			PilotState State;
			/// <summary>US-T422: the body whose frame the pose is in (it changes when the helm crosses into another body's
			/// space), so the hold plays it back in its own frame and a resume puts the frame back with it.</summary>
			int Frame = 0;
		};

		/// <summary>A resume point: the fight's whole state at one moment, by value, beside the live objects it goes back
		/// over, and the helm's flight state.</summary>
		struct HrAnchor
		{
			double T = 0;
			cs::Ref<Battle> Live;
			std::shared_ptr<Battle> Copy;
			std::vector<std::pair<cs::Ref<BattleUnit>, BattleUnit>> Units;
			std::vector<std::pair<cs::Ref<ShipCombat>, ShipCombat>> Combats;
			std::vector<std::pair<cs::Ref<Subsystem>, Subsystem>> Subsystems;
			std::vector<std::pair<cs::Ref<PowerDistribution>, PowerDistribution>> Powers;
			std::vector<std::pair<cs::Ref<FighterState>, FighterState>> Fighters;
			std::vector<std::pair<cs::Ref<BroadsideCommand>, BroadsideCommand>> Broadsides;
			std::vector<std::pair<cs::Ref<Missile>, Missile>> Missiles;
			std::vector<std::pair<cs::Ref<KineticRound>, KineticRound>> Rounds;
			std::vector<std::pair<cs::Ref<Mine>, Mine>> Mines;
			std::vector<std::pair<cs::Ref<Decoy>, Decoy>> Decoys;
			// US-T413: a carrier's own flight decks (Aboard, Final, NextLaunch) and a craft's own progress along one
			// (DeckVisit) are not reached by the Units/Fighters copies above: FlightDecks::Of keeps them in its own
			// weak table outside the fight, and a fighter's DeckVisit is mutated in place, not recreated, while it
			// taxis or launches. Captured and restored the same way, by value, keyed by their own Ref.
			std::vector<std::pair<cs::Ref<FlightDeck>, FlightDeck>> Decks;
			std::vector<std::pair<cs::Ref<DeckVisit>, DeckVisit>> Visits;
			// US-T413: ShipCombat::Hangar/ShuttleBay are a Ref to a shared List, so copying ShipCombat by value only
			// copies that pointer, not what is in the list. Captured as the list's own contents and restored into the
			// same live List object (Clear then Add), so anyone else holding the same Ref still holds the right one.
			std::vector<std::pair<cs::Ref<cs::List<cs::Ref<Ship>>>, std::vector<cs::Ref<Ship>>>> Bays;
			// US-T416: a deployed mech (Battle::Mechs) is its own standalone object with substantial state mutated in
			// place (Pos, Armor, Heat, ammo, gait) -- the same shape of gap as a FlightDeck or a DeckVisit, since
			// Mechs is a list of Refs, not embedded values. Captured and restored the same way, by value.
			std::vector<std::pair<cs::Ref<MechWalker>, MechWalker>> Mechs;
			// US-T417: the seat the player held at this moment besides the helm (a gun, a craft, a mech), or null: put back
			// with the fight (RestoreHoldRewindSeat), so a seat change is undone with everything else.
			cs::Ref<IEmbodiable> Seat;
			bool HasPilot = false;
			PilotState Pilot;
			cs::Ref<Ship> PilotShip;
			int PilotFrame = -1;
		};

		/// <summary>What a resume cannot put back: who is in the fight and in what state, the seats. Any change between
		/// two frames is a barrier. US-T410: with the bookings held back, a hull lost, struck, taken or escaped is put
		/// back by the resume point itself. US-T413: a craft launched or landed is put back too (FlightDecks::Of's
		/// decks and a fighter's DeckVisit, and the hangar/shuttle bay lists, are captured and restored by value).
		/// US-T414: a unit joining mid-fight (Battle::AddShip's ship->Battle = shared_from_this(), outside the
		/// per-unit copies the same way FlightDecks was) is put back too, so a unit other than the one the player is
		/// aboard (by Ship, below, not BattleUnit::PlayerControlled() -- that flips the instant the player leaves
		/// the helm even for a turret on the same hull, which is not the hull the player left) has its mere presence
		/// no longer counted here at all -- only the hull the player is aboard (whose fate truly cannot be booked
		/// late, whichever of its seats the player holds) and a hull struck off the world's books by anything other
		/// than the held-back hooks (Lost, below) count.</summary>
		struct HrCensus
		{
			const void* Fight = nullptr;
			bool Over = false;
			int Units = 0, Dead = 0, Fled = 0, Struck = 0, Taken = 0, Lost = 0, Seats = 0;
			unsigned long long Flags = 0;
			/// <summary>US-T423: in co-op, every player's seat and the helm (CoopHoldRewindSeats); 0 in single-player.</summary>
			unsigned long long Coop = 0;
			const void* Ship = nullptr;
			int Frame = -1;
			bool Piloting = false;

			bool operator==(const HrCensus& o) const
			{
				return Fight == o.Fight && Over == o.Over && Units == o.Units && Dead == o.Dead && Fled == o.Fled &&
					Struck == o.Struck && Taken == o.Taken && Lost == o.Lost && Seats == o.Seats && Flags == o.Flags && Coop == o.Coop;
				// US-T422: neither the body held (Ship) nor the helm's frame (Frame) is a barrier any more: a resume puts the
				// seat back on whichever body it was (RestoreHoldRewindSeat takes that body's helm first) and the helm in
				// the frame it was in (the pose and its frame together, as a load does). Kept for the reasons' wording.
			}
		};

		/// <summary>US-T414: ship is now whichever hull the player is aboard, by ANY of its seats (the helm or one of
		/// its turrets, from HeldSeat()->Subject().Ship), not only the hull piloted; piloting stays a separate flag,
		/// read only for the pilot state capture/restore (HrCaptureAnchor, putBack), not for this census, so moving
		/// from the helm to a turret on the identical hull changes neither Ship nor which unit is "counted" below.</summary>
		HrCensus HrTakeCensus(const cs::Ref<Battle>& fight, const cs::Ref<Ship>& ship, int frame, bool piloting)
		{
			HrCensus c;
			c.Ship = ship.get();
			c.Frame = frame;
			c.Piloting = piloting;
			if (fight == nullptr) return c;
			c.Fight = fight.get();
			c.Over = fight->Over;
			// US-T416: a mech's state is captured by value and a stuck pod (Embedded) is write-once data the Battle copy
			// restores. US-T417: the player's own taken guns and flown craft (Manned, Flown) are put back too, with the
			// seat itself (RestoreHoldRewindSeat), so with the bookings held back none of them is a barrier any more.
			c.Seats = HrTuning::DeferBookings ? 0 : fight->Mechs.Count() + fight->Embedded.Count() + fight->Manned.Count() + fight->Flown.Count();
			unsigned long long h = 1469598103934665603ull;
			for (const auto& u : fight->Units)
			{
				if (u == nullptr) continue;
				// US-T417: with every hull's fate held back (the player's own too, DeferOwnFate), no unit's dead, fled,
				// struck or taken flag is a barrier: the resume point restores them all. A loss actually booked shows up
				// through Lost, below, whoever's it was.
				bool counted = !HrTuning::DeferBookings || (!HrTuning::DeferOwnFate && ship != nullptr && u->Ship == ship);
				if (counted)
				{
					c.Units++;
					c.Dead += u->Dead ? 1 : 0;
					c.Fled += u->Fled ? 1 : 0;
					c.Struck += u->Surrendering ? 1 : 0;
					c.Taken += u->Captured ? 1 : 0;
				}
				// A hull struck off the world's books (its Ship destroyed) by anything but the held-back hooks: a
				// real barrier regardless of whose unit it is, since that booking already happened for good.
				c.Lost += u->Ship != nullptr && u->Ship->Destroyed ? 1 : 0;
				// US-T413: Docked and the hangar/shuttle bay counts are no longer a barrier (they are captured and
				// restored by value now), so they are not part of the census. US-T414: neither is any unit other
				// than the one the player is aboard (it joining or leaving by dying, fleeing, being taken,
				// launching or landing, or simply not being the player's own hull): everything about it the resume
				// needs is already captured elsewhere, so it is skipped here entirely rather than folded into the
				// hash with its state bits merely masked off.
				if (!counted) continue;
				unsigned long long bits = (u->Dead ? 1u : 0u) | (u->Fled ? 2u : 0u) | (u->Surrendering ? 8u : 0u) | (u->Captured ? 16u : 0u);
				h = (h ^ (static_cast<unsigned long long>(u->Id) * 64 + bits)) * 1099511628211ull;
			}
			c.Flags = h;
			return c;
		}

		/// <summary>US-T417: the body a seat belongs to, as the embody gate counts it (Embodiables' BodyUnit): a craft launched
		/// from a carrier is the carrier's, so flying your own carrier's fighter is not leaving the hull.</summary>
		cs::Ref<Ship> HrBodyShip(const cs::Ref<IEmbodiable>& seat)
		{
			if (seat == nullptr) return nullptr;
			auto sub = seat->Subject();
			auto u = sub.Unit;
			if (u == nullptr && sub.Ship != nullptr && sub.Ship->Battle != nullptr) u = sub.Ship->Battle->UnitOf(sub.Ship);
			if (u != nullptr && u->Fighter != nullptr && u->Fighter->Carrier != nullptr && u->Fighter->Carrier->Ship != nullptr)
				return u->Fighter->Carrier->Ship;
			if (sub.Ship != nullptr) return sub.Ship;
			return u != nullptr ? u->Ship : nullptr;
		}

		/// <summary>In words, what changed between two censuses (the barrier's reason).</summary>
		cs::String HrWhyChanged(const HrCensus& a, const HrCensus& b)
		{
			if (a.Fight != b.Fight) return b.Fight == nullptr ? "the fight was left behind" : "another fight came into view";
			if (a.Over != b.Over) return "the fight ended";
			if (a.Coop != b.Coop) return "a player took or let go of a seat or the helm"; // US-T423
			// US-T414: Ship is now whichever hull the player is aboard by any seat (HeldSeat, not just the helm),
			// so moving between its own seats (helm to a turret and back) no longer fires this: only actually
			// leaving the hull, or it having none of you aboard any more, does.
			if (a.Ship != b.Ship) return "you left the hull";
			if (a.Frame != b.Frame) return "you crossed into another body's space";
			if (b.Dead > a.Dead || b.Lost > a.Lost) return "a hull was lost";
			if (b.Fled > a.Fled) return "a hull escaped the fight";
			if (b.Taken != a.Taken || b.Struck != a.Struck) return "a hull struck or was taken";
			// US-T414: Units now counts only the unit the player is aboard (see HrTakeCensus), so this fires only
			// if that changes in some other way -- not when an AI reinforcement joins or leaves, which is fully
			// restorable and no longer reaches this far.
			if (b.Units != a.Units) return "the units you control changed";
			if (b.Seats != a.Seats) return "you took or let go of a gun or a craft";
			return "the fight changed in a way not undone yet";
		}

		DVec3 HrHermite(const DVec3& p0, const DVec3& v0, const DVec3& p1, const DVec3& v1, double h, double u)
		{
			double u2 = u * u, u3 = u2 * u;
			double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
			return p0 * h00 + v0 * (h10 * h) + p1 * h01 + v1 * (h11 * h);
		}

		/// <summary>The pose at <paramref name="tau"/>: held at either end, between two kept ones smoothed (the flags those
		/// of the earlier, so a hull lost between them is whole until the later).</summary>
		bool HrPoseAt(const std::deque<HrPose>& poses, double tau, HrPose& out)
		{
			if (poses.empty()) return false;
			if (tau <= poses.front().T)
			{
				out = poses.front();
				return true;
			}
			if (tau >= poses.back().T)
			{
				out = poses.back();
				return true;
			}
			auto it = std::lower_bound(poses.begin(), poses.end(), tau, [](const HrPose& p, double t) { return p.T < t; });
			const HrPose& b = *it;
			const HrPose& a = *(it - 1);
			double f = (tau - a.T) / std::max(1e-9, b.T - a.T);
			double h = b.S - a.S;
			out = a;
			out.T = tau;
			out.S = a.S + h * f;
			out.Pos = h > 1e-9 ? HrHermite(a.Pos, a.Vel, b.Pos, b.Vel, h, f) : DVec3::Lerp(a.Pos, b.Pos, f);
			out.Vel = DVec3::Lerp(a.Vel, b.Vel, f);
			auto fwd = DVec3::Lerp(a.Fwd, b.Fwd, f);
			out.Fwd = fwd.LengthSquared() > 1e-12 ? fwd.Normalized() : a.Fwd;
			return true;
		}

		template <class T> void HrPushProjectile(HrTrack<T>& tr, const cs::Ref<T>& obj, double t, double s, size_t& samples)
		{
			HrPose p;
			p.T = t;
			p.S = s;
			p.Pos = obj->Pos;
			p.Vel = obj->Vel;
			p.Fwd = obj->Vel;
			tr.Poses.push_back(p);
			samples++;
		}

		template <class T> void HrRecordProjectiles(std::unordered_map<const T*, HrTrack<T>>& tracks, const cs::List<cs::Ref<T>>& list, double t, double s, size_t& samples)
		{
			for (auto& kv : tracks) kv.second.Alive = false;
			for (const auto& obj : list)
			{
				if (obj == nullptr) continue;
				auto& tr = tracks[obj.get()];
				if (tr.Obj == nullptr) tr.Obj = obj;
				tr.Alive = true;
				HrPushProjectile(tr, obj, t, s, samples);
			}
		}

		/// <summary>Draws back into the fight's list the projectiles in flight at <paramref name="tau"/>, each where it was.</summary>
		template <class T> void HrPlayProjectiles(cs::List<cs::Ref<T>>& list, std::unordered_map<const T*, HrTrack<T>>& tracks, double tau, double interval)
		{
			list.Clear();
			for (auto& kv : tracks)
			{
				auto& tr = kv.second;
				if (tr.Poses.empty() || tau < tr.Poses.front().T) continue;
				if (!tr.Alive && tau > tr.Poses.back().T + interval) continue;
				HrPose p;
				if (!HrPoseAt(tr.Poses, tau, p)) continue;
				tr.Obj->Pos = p.Pos;
				tr.Obj->Vel = p.Vel;
				list.Add(tr.Obj);
			}
		}

		/// <summary>Drops a track's poses older than <paramref name="cutoff"/> but the last one before it (the next pose's
		/// interpolation needs it).</summary>
		template <class P> void HrPruneFront(std::deque<P>& poses, double cutoff, size_t* samples)
		{
			while (poses.size() >= 2 && poses[1].T < cutoff)
			{
				poses.pop_front();
				if (samples != nullptr && *samples > 0) (*samples)--;
			}
		}

		template <class T> void HrPruneTracks(std::unordered_map<const T*, HrTrack<T>>& tracks, double cutoff, size_t& samples)
		{
			for (auto it = tracks.begin(); it != tracks.end();)
			{
				HrPruneFront(it->second.Poses, cutoff, &samples);
				bool stale = it->second.Poses.empty() || (it->second.Poses.back().T < cutoff && !it->second.Alive);
				if (stale)
				{
					samples -= std::min(samples, it->second.Poses.size());
					it = tracks.erase(it);
				}
				else ++it;
			}
		}

		template <class P> void HrTrimBack(std::deque<P>& poses, double after, size_t* samples)
		{
			while (!poses.empty() && poses.back().T > after)
			{
				poses.pop_back();
				if (samples != nullptr && *samples > 0) (*samples)--;
			}
		}

		template <class P> void HrShiftPoses(std::deque<P>& poses, double by)
		{
			for (auto& p : poses) p.T += by;
		}

		template <class T> void HrTrimAndShift(std::unordered_map<const T*, HrTrack<T>>& tracks, double after, double by, size_t& samples)
		{
			for (auto it = tracks.begin(); it != tracks.end();)
			{
				HrTrimBack(it->second.Poses, after, &samples);
				if (it->second.Poses.empty()) it = tracks.erase(it);
				else
				{
					HrShiftPoses(it->second.Poses, by);
					++it;
				}
			}
		}

		std::shared_ptr<HrAnchor> HrCaptureAnchor(double t, const cs::Ref<Battle>& fight, bool piloting, const PilotState& pilot, const cs::Ref<Ship>& ship, int frame,
			const cs::Ref<IEmbodiable>& seat)
		{
			auto a = std::make_shared<HrAnchor>();
			a->T = t;
			if (fight != nullptr)
			{
				a->Live = fight;
				a->Copy = std::make_shared<Battle>(*fight);
				a->Units.reserve(static_cast<size_t>(fight->Units.Count()));
				for (const auto& u : fight->Units)
				{
					if (u == nullptr) continue;
					a->Units.emplace_back(u, *u);
					if (u->Combat != nullptr)
					{
						a->Combats.emplace_back(u->Combat, *u->Combat);
						for (const auto& s : u->Combat->Subsystems)
							if (s != nullptr) a->Subsystems.emplace_back(s, *s);
						if (u->Combat->Reactor != nullptr) a->Powers.emplace_back(u->Combat->Reactor, *u->Combat->Reactor);
						// US-T413: the hangar and shuttle bay's own contents (a Ref to a shared List; ShipCombat's own
						// copy above only carries the pointer).
						if (u->Combat->Hangar != nullptr)
						{
							std::vector<cs::Ref<Ship>> items;
							for (const auto& s : *u->Combat->Hangar) items.push_back(s);
							a->Bays.emplace_back(u->Combat->Hangar, std::move(items));
						}
						if (u->Combat->ShuttleBay != nullptr)
						{
							std::vector<cs::Ref<Ship>> items;
							for (const auto& s : *u->Combat->ShuttleBay) items.push_back(s);
							a->Bays.emplace_back(u->Combat->ShuttleBay, std::move(items));
						}
					}
					if (u->Power != nullptr) a->Powers.emplace_back(u->Power, *u->Power);
					if (u->Fighter != nullptr)
					{
						a->Fighters.emplace_back(u->Fighter, *u->Fighter);
						// US-T413: the craft's own progress along the deck it is visiting, mutated in place while it
						// taxis, lands or launches; not reached by the FighterState copy above, which only carries
						// the Ref to it.
						if (u->Fighter->Deck != nullptr) a->Visits.emplace_back(u->Fighter->Deck, *u->Fighter->Deck);
					}
					if (u->Broadside != nullptr) a->Broadsides.emplace_back(u->Broadside, *u->Broadside);
					// US-T413: a carrier's own flight decks (who is aboard, who is cleared to land, the next launch
					// slot) live in FlightDecks::Of's weak table outside the fight, keyed by the carrier's own
					// BattleUnit, which stays alive (even if Dead) for as long as the fight is recorded.
					if (u->Kind == UnitKind::Ship)
						for (const auto& d : FlightDecks::Of(u))
							if (d != nullptr) a->Decks.emplace_back(d, *d);
				}
				for (const auto& m : fight->Missiles)
					if (m != nullptr) a->Missiles.emplace_back(m, *m);
				for (const auto& r : fight->Rounds)
					if (r != nullptr) a->Rounds.emplace_back(r, *r);
				for (const auto& m : fight->Mines)
					if (m != nullptr) a->Mines.emplace_back(m, *m);
				// US-T416: Battle::Mechs is a list of Refs; the mech objects they point to carry their own evolving
				// state (position, armour, heat, ammo), mutated in place, not recreated.
				for (const auto& w : fight->Mechs)
					if (w != nullptr) a->Mechs.emplace_back(w, *w);
				for (const auto& d : fight->Decoys)
					if (d != nullptr) a->Decoys.emplace_back(d, *d);
			}
			a->Seat = seat;
			if (piloting)
			{
				a->HasPilot = true;
				a->Pilot = pilot;
				a->PilotShip = ship;
				a->PilotFrame = frame;
			}
			return a;
		}

		/// <summary>Puts the fight's state at the resume point back over the live objects. The hooks the world hung on the
		/// fight (its events, and US-T410's held-back bookings round them) stay as they are now, and so do its effects
		/// (US-T410: the Fx list and its sequence go on from now, so the views neither show an effect twice nor miss the
		/// next ones); everything else of the Battle, each unit and what it owns is the resume point's.</summary>
		void HrRestoreFight(const HrAnchor& a)
		{
			if (a.Live != nullptr && a.Copy != nullptr)
			{
				Battle& b = *a.Live;
				auto unitCaptured = b.UnitCaptured;
				auto unitDestroyed = b.UnitDestroyed;
				auto unitSurrendered = b.UnitSurrendered;
				auto surrenderDecided = b.SurrenderDecided;
				auto cargoDemanded = b.CargoDemanded;
				auto ending = b.Ending;
				auto holdEnd = b.HoldEnd;
				auto helpDueS = b.HelpDueS;
				auto fighterStep = b.FighterStep;
				auto launch = b.Launch;
				auto wantsAlive = b.WantsAlive;
				auto boards = b.Boards;
				auto fx = b.Fx;
				int fxSeq = b.FxSeq;
				auto fxBy = b.FxBy;
				auto fxByMount = b.FxByMount;
				// US-T414: a unit that joined after this anchor set its ship's Battle outside the per-unit copies
				// below (Battle::AddShip: ship->Battle = shared_from_this(), the same shape of problem US-T413 found
				// for FlightDecks and the hangar lists). a.Units is exactly who was in the fight at this anchor,
				// read while b.Units still holds the live (pre-restore) list, so anyone live now but not kept here
				// joined since and has its ship's Battle cleared, the same null War::Join found before it joined.
				std::unordered_set<const void*> keptUnits;
				keptUnits.reserve(a.Units.size());
				for (const auto& p : a.Units) keptUnits.insert(p.first.get());
				for (const auto& u : b.Units)
					if (u != nullptr && u->Ship != nullptr && keptUnits.find(u.get()) == keptUnits.end())
						u->Ship->Battle = nullptr;
				b = *a.Copy;
				b.UnitCaptured = std::move(unitCaptured);
				b.UnitDestroyed = std::move(unitDestroyed);
				b.UnitSurrendered = std::move(unitSurrendered);
				b.SurrenderDecided = std::move(surrenderDecided);
				b.CargoDemanded = std::move(cargoDemanded);
				b.Ending = std::move(ending);
				b.HoldEnd = std::move(holdEnd);
				b.HelpDueS = std::move(helpDueS);
				b.FighterStep = std::move(fighterStep);
				b.Launch = std::move(launch);
				b.WantsAlive = std::move(wantsAlive);
				b.Boards = std::move(boards);
				b.Fx = std::move(fx);
				b.FxSeq = fxSeq;
				b.FxBy = std::move(fxBy);
				b.FxByMount = std::move(fxByMount);
			}
			for (const auto& p : a.Units) *p.first = p.second;
			// US-T424: a hull that has left the fight since (flown clear: War::Update clears its Battle once it is Fled) is in it
			// again at a resume point from before it left.
			if (a.Live != nullptr)
				for (const auto& p : a.Units)
				{
					auto ship = p.first->Ship;
					if (ship != nullptr && !p.first->Fled && !ship->Destroyed && ship->Battle == nullptr) ship->Battle = a.Live;
				}
			for (const auto& p : a.Combats) *p.first = p.second;
			for (const auto& p : a.Subsystems) *p.first = p.second;
			for (const auto& p : a.Powers) *p.first = p.second;
			for (const auto& p : a.Fighters) *p.first = p.second;
			for (const auto& p : a.Broadsides) *p.first = p.second;
			for (const auto& p : a.Missiles) *p.first = p.second;
			for (const auto& p : a.Rounds) *p.first = p.second;
			for (const auto& p : a.Mines) *p.first = p.second;
			for (const auto& p : a.Decoys) *p.first = p.second;
			// US-T413: a carrier's decks and a craft's own deck visit, outside the per-unit copies above.
			for (const auto& p : a.Decks) *p.first = p.second;
			for (const auto& p : a.Visits) *p.first = p.second;
			// US-T416: a deployed mech's own evolving state, outside the Battle::Mechs list-of-Refs copy.
			for (const auto& p : a.Mechs) *p.first = p.second;
			// US-T413: the hangar/shuttle bay list is restored into the same live List object (its Ref is shared),
			// not replaced, so anyone else holding that Ref still holds the one that now has the old contents back.
			for (const auto& p : a.Bays)
			{
				p.first->Clear();
				for (const auto& s : p.second) p.first->Add(s);
			}
		}

		/// <summary>A booking of the world's held back (US-T410): when (lived seconds) and what it does.</summary>
		struct HrBooking
		{
			double T = 0;
			std::function<void()> Fire;
		};

		/// <summary>The self-test's bookkeeping (sg.HoldRewind selftest).</summary>
		struct HrSelfTest
		{
			bool On = false, Done = false, Passed = false;
			int Phase = 0, Checks = 0, Failed = 0, Tries = 0, SettleFrames = 0;
			cs::Ref<Battle> Fight;
			double PhaseWall = 0, M = -1, Floor = HrNone, LastTau = std::numeric_limits<double>::max();
			unsigned long long DM = 0, DR = 0;
			bool HaveResume = false, TauMonotonic = true;
			double ResumedAnchorT = 0, FloorAtResume = 0;
			int FxSeqAtPress = 0, Resumes = 0;
			std::vector<cs::Ref<BattleUnit>> DiedSinceM;
			bool ForceDown = false;
			/// <summary>US-T420: the hold key is pressed for real (by sg.HoldRewind, through Unreal's input) to match
			/// ForceDown, which then no longer reaches the hold itself.</summary>
			bool RealKeys = false;
			bool KeyChecked = false;
			double Target = HrNone;
			// US-T417: the hull flown's unit (from the seat check) and the moment marked before it is killed.
			cs::Ref<BattleUnit> SeatUnit;
			double DeathMarkT = HrNone;
			double KilledAgainWall = -1;
			/// <summary>US-T419: the moment marked before every enemy is struck down, and the wall clock the fight was first
			/// seen over in the last phase.</summary>
			double EndMarkT = HrNone;
			double OverWall = -1;
			/// <summary>US-T419: the wall clock a screenshot was asked for, so the hold waits for it to be taken.</summary>
			double ShotWall = -1;
			/// <summary>US-T422: the moment marked before the helm is let go.</summary>
			double LeaveMarkT = HrNone;
			/// <summary>US-T424: the moment marked before the hull flies clear of the fight.</summary>
			double FleeMarkT = HrNone;
			// US-T414: the seat-change check (phases 8-13): the hull taken for it, the moment marked while piloting
			// it (before the switch to a turret), and whether the resume landed there or was blocked by a barrier.
			cs::Ref<Ship> SeatShip;
			double SeatMarkT = -1;
			/// <summary>US-T424: the birth check (sg.HoldRewind selftest birth): a hull flown off Uranus with no fight, a
			/// fight then begun on it, and a hold back to before it began. The moment marked before, the fight's number and
			/// name, and whether the playback was seen drawing the moment before it.</summary>
			bool Birth = false, SawUnborn = false;
			double BirthMarkT = HrNone;
			int BirthId = -1;
			/// <summary>US-T428: the crossing check: the moment marked, and the frame and offset the helm had then.</summary>
			double CrossMarkT = HrNone;
			int CrossFrom = -1;
			DVec3 CrossOffset;
			cs::String BirthName;
		};
	}

	struct UnitySpaceApp::HoldRewindState
	{
		/// <summary>Real seconds the simulation has run while recorded (a pause, the screens between the stars and the
		/// hold itself do not count): the key every pose and resume point is kept by.</summary>
		double LivedS = 0;

		cs::Ref<Battle> Fight;
		double FightSince = 0;
		/// <summary>US-T424: the fight watched since EnteredT, entered with none watched before (UndoFightStart): the resume
		/// points kept before it (the helm's) are kept, and whether it was born then (BornS) or was already under way.</summary>
		cs::Ref<Battle> Entered;
		double EnteredT = HrNone;
		bool EnteredBorn = false;
		/// <summary>US-T424: while a hold plays back a moment before the fight entered was born, its ships are let out of it
		/// (their Battle cleared, so everything that reads it, the HUD's fight panels and status lines too, sees no fight);
		/// these are put back in before anything else when the moment reached is after its start again, or the hold ends.</summary>
		std::vector<cs::Ref<Ship>> Detached;
		/// <summary>US-T426: the resume point whose hulls' damage the playback shows now (null: the present's).</summary>
		const HrAnchor* DamageShown = nullptr;
		std::unordered_map<const BattleUnit*, HrTrack<BattleUnit>> Units;
		std::unordered_map<const Missile*, HrTrack<Missile>> Missiles;
		std::unordered_map<const KineticRound*, HrTrack<KineticRound>> Rounds;
		std::unordered_map<const Decoy*, HrTrack<Decoy>> Decoys;
		std::deque<HrClockMark> Clock;
		std::deque<HrPilotPose> Pilot;
		cs::Ref<Ship> PilotShip;
		std::deque<std::shared_ptr<HrAnchor>> Anchors;
		double NextCapitalAt = 0, NextCraftAt = 0, NextAnchorAt = 0;
		size_t Samples = 0;

		HrCensus Last;
		bool HasLast = false;
		/// <summary>The newest barrier (lived seconds) and its reason: no resume goes back past it.</summary>
		double BarrierT = HrNone;
		cs::String BarrierWhy;
		/// <summary>US-T410: the last resume point (lived seconds): no hold goes back past it (a time already rewound
		/// through is never revisited).</summary>
		double FloorT = HrNone;

		/// <summary>US-T410: the world's bookings held back, oldest first, and the fight whose hooks hold them (with the
		/// hooks the world had hung on it).</summary>
		std::deque<HrBooking> Bookings;
		cs::Ref<Battle> Hooked;
		decltype(Battle::UnitDestroyed) OrigDestroyed;
		decltype(Battle::UnitCaptured) OrigCaptured;
		decltype(Battle::UnitSurrendered) OrigSurrendered;
		decltype(Battle::SurrenderDecided) OrigDecided;
		double KeyframeWaitSince = -1;

		bool Active = false, KeyWasDown = false, Releasing = false;
		double PressT = 0, Tau = 0, Oldest = 0, HeldS = 0, Rate = 0, ReleaseLeft = 0, ReleaseRate = 0, JdAtPress = 0;
		std::shared_ptr<HrAnchor> AtPress;
		/// <summary>US-T423: real seconds of frames the hold has stepped on (each frame's dt, the frame's own in co-op): the
		/// clock the cooldown runs on, so every peer's is the same (the wall clock differs per machine).</summary>
		double FrameClock = 0;
		/// <summary>FrameClock seconds before the next hold may start (US-T423: was the wall clock).</summary>
		double CooldownUntil = 0;
		/// <summary>US-T410: frames left in which a resume is "just now" (the HUD takes stock instead of reporting).</summary>
		int JustResumed = 0;
		/// <summary>US-T417: this frame, the hull flown is lost (1) or taken (2) in its fight, its fate held back for the
		/// window (HoldRewindOwnFateHeld): the readout says there is still time to undo it. Cleared each frame.</summary>
		int OwnFateHeld = 0;
		/// <summary>US-T417: lived seconds when the hull flown was first seen lost or taken, for the readout's countdown.</summary>
		double OwnFateSince = HrNone;

		/// <summary>US-T418: per fleet index, the lived seconds the ship went out through a gate (HrNone: not, or before the
		/// recording began) and the lived seconds it first appeared (HrNone: there when the recording began), so a hold
		/// draws the ships that left in the window again and hides the ones that came in it. Drawing only.</summary>
		std::vector<double> ShipGoneAt, ShipCameAt;
		std::vector<char> ShipWasGone;
		bool ShipsSeen = false;

		/// <summary>US-T419: lived seconds the fight watched first would have ended (HrNone: it has not, or it was fought on
		/// since): its end waits until this is older than the window.</summary>
		double EndHeldSince = HrNone;

		/// <summary>US-T421: the effects' clock (Time::unscaledTime) at the press, the effects' clock of the moment a hold
		/// played on from, and a count of holds ended, for the views that draw effects by their age (BattleFx).</summary>
		float PressFx = 0, ResumeFx = 0;
		int FxEnds = 0;

		HrSelfTest Test;

		cs::Ref<GUIStyle> Text;
	};

	bool UnitySpaceApp::HoldRewindActive() const { return holdRewind != nullptr && holdRewind->Active; }

	void UnitySpaceApp::TrackHoldRewindShips(double t)
	{
		auto& h = *holdRewind;
		auto fleet = Fleet();
		if (fleet == nullptr) return;
		{
			const auto& ships = fleet->Ships();
			size_t n = static_cast<size_t>(ships.Count());
			size_t had = h.ShipGoneAt.size();
			if (n < had) had = n;
			h.ShipGoneAt.resize(n, HrNone);
			h.ShipCameAt.resize(n, HrNone);
			h.ShipWasGone.resize(n, 0);
			for (size_t i = 0; i < n; i++)
			{
				const auto& ship = ships[static_cast<int>(i)];
				bool gone = ship != nullptr && ship->GateDeparted;
				if (i >= had) h.ShipCameAt[i] = h.ShipsSeen ? t : HrNone;
				if (gone && !h.ShipWasGone[i] && h.ShipsSeen && i < had) h.ShipGoneAt[i] = t;
				h.ShipWasGone[i] = gone ? 1 : 0;
			}
			h.ShipsSeen = true;
		}
	}

	bool UnitySpaceApp::HoldRewindShipShown(int index, bool shownNow) const
	{
		if (holdRewind == nullptr || !holdRewind->Active || index < 0) return shownNow;
		const auto& h = *holdRewind;
		size_t i = static_cast<size_t>(index);
		if (i >= h.ShipGoneAt.size()) return shownNow;
		// Came in after the moment reached: not there yet. Went out after it: still there.
		if (h.ShipCameAt[i] != HrNone && h.Tau < h.ShipCameAt[i]) return false;
		if (h.ShipGoneAt[i] != HrNone && h.Tau < h.ShipGoneAt[i]) return true;
		return shownNow;
	}

	bool UnitySpaceApp::HoldRewindBeforeFight(const cs::Ref<Battle>& battle) const
	{
		if (holdRewind == nullptr || !holdRewind->Active || battle == nullptr) return false;
		const auto& h = *holdRewind;
		return h.EnteredBorn && h.Entered == battle && h.Tau < h.EnteredT;
	}

	void UnitySpaceApp::HoldRewindReattach()
	{
		if (holdRewind == nullptr || holdRewind->Detached.empty()) return;
		auto& h = *holdRewind;
		for (const auto& s : h.Detached)
			if (s != nullptr && s->Battle == nullptr && h.Entered != nullptr) s->Battle = h.Entered;
		h.Detached.clear();
	}

	bool UnitySpaceApp::HoldRewindJustResumed() const { return holdRewind != nullptr && holdRewind->JustResumed > 0; }

	float UnitySpaceApp::HoldRewindFxClock(float now) const
	{
		if (holdRewind == nullptr || !holdRewind->Active) return now;
		const auto& h = *holdRewind;
		// Lived seconds are real seconds the fight ran: the moment reached, on the effects' clock.
		return h.PressFx - static_cast<float>(h.PressT - h.Tau);
	}

	bool UnitySpaceApp::HoldRewindFxEnded(int& seen, float& resumeFx) const
	{
		if (holdRewind == nullptr || holdRewind->FxEnds == seen || holdRewind->Active) return false;
		// The recording cleared (a load): the count starts again, nothing to undo.
		bool ended = holdRewind->FxEnds > seen;
		seen = holdRewind->FxEnds;
		if (!ended) return false;
		resumeFx = holdRewind->ResumeFx;
		return true;
	}

	cs::Ref<Battle> UnitySpaceApp::HoldRewindBattle()
	{
		if (IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr && !Piloted.Ship->Battle->Over) return Piloted.Ship->Battle;
		if (battleTimeFor != nullptr && !battleTimeFor->Over) return battleTimeFor;
		return nullptr;
	}

	unsigned long long UnitySpaceApp::HoldRewindFightDigest(const cs::Ref<Battle>& fight)
	{
		unsigned long long h = 1469598103934665603ull;
		auto mixU = [&h](unsigned long long v)
		{
			for (int i = 0; i < 8; i++)
			{
				h ^= (v >> (i * 8)) & 0xffu;
				h *= 1099511628211ull;
			}
		};
		auto mixD = [&mixU](double d)
		{
			unsigned long long v = 0;
			std::memcpy(&v, &d, sizeof v);
			mixU(v);
		};
		auto mixV = [&mixD](const DVec3& v)
		{
			mixD(v.X);
			mixD(v.Y);
			mixD(v.Z);
		};
		if (fight == nullptr) return 0;
		mixD(fight->Seconds);
		mixU(fight->Over ? 1u : 0u);
		mixU(static_cast<unsigned long long>(fight->Winner + 1));
		// The random draws to come (a copy drawn from, the fight's own untouched).
		cs::Random next = fight->Rng();
		mixD(next.NextDouble());
		mixD(next.NextDouble());
		mixU(static_cast<unsigned long long>(fight->Units.Count()));
		for (const auto& u : fight->Units)
		{
			if (u == nullptr) continue;
			mixU(static_cast<unsigned long long>(u->Id));
			mixU((u->Dead ? 1u : 0u) | (u->Fled ? 2u : 0u) | (u->Docked ? 4u : 0u) | (u->Surrendering ? 8u : 0u) | (u->Captured ? 16u : 0u));
			mixV(u->Pos);
			mixV(u->Vel);
			mixV(u->Forward);
			mixU(u->Target != nullptr ? static_cast<unsigned long long>(u->Target->Id) + 1 : 0);
			if (u->Combat != nullptr)
			{
				mixD(u->Combat->Hull);
				for (const auto& s : u->Combat->Subsystems)
				{
					if (s == nullptr) continue;
					mixD(s->Hp);
					mixD(s->Cooldown);
					mixU(static_cast<unsigned long long>(s->Ammo));
					mixD(s->Shield);
				}
			}
		}
		mixU(static_cast<unsigned long long>(fight->Missiles.Count()));
		for (const auto& m : fight->Missiles)
			if (m != nullptr)
			{
				mixV(m->Pos);
				mixV(m->Vel);
			}
		mixU(static_cast<unsigned long long>(fight->Rounds.Count()));
		for (const auto& r : fight->Rounds)
			if (r != nullptr)
			{
				mixV(r->Pos);
				mixV(r->Vel);
			}
		mixU(static_cast<unsigned long long>(fight->Decoys.Count()));
		for (const auto& d : fight->Decoys)
			if (d != nullptr)
			{
				mixV(d->Pos);
				mixV(d->Vel);
			}
		mixU(static_cast<unsigned long long>(fight->Mines.Count()));
		return h;
	}

	// ------------------------------------------------------------------------------------------- the held-back bookings

	void UnitySpaceApp::BookHoldRewindUpTo(double uptoS)
	{
		if (holdRewind == nullptr) return;
		auto& h = *holdRewind;
		while (!h.Bookings.empty() && h.Bookings.front().T <= uptoS)
		{
			auto b = std::move(h.Bookings.front());
			h.Bookings.pop_front();
			// Booked now: the world holds it, so no resume may go back before it.
			if (b.T > h.BarrierT)
			{
				h.BarrierT = b.T;
				h.BarrierWhy = "the world has booked a hull's fate";
			}
			if (b.Fire) b.Fire();
		}
	}

	void UnitySpaceApp::BookHoldRewindBeforeSave()
	{
		// US-T423: in co-op a save of this machine's own (an autosave, a quicksave, a keyframe) comes when this machine
		// says, so it must not book the shared world's held-back fates early: they wait out the window on every peer alike.
		// A sync's save books them first everywhere (ResetHoldRewindForSync).
		if (Coop::CoopPeer::Current() != nullptr) return;
		BookHoldRewindUpTo(std::numeric_limits<double>::max());
	}

	bool UnitySpaceApp::HoldRewindDelaysKeyframe()
	{
		if (holdRewind == nullptr || holdRewind->Bookings.empty())
		{
			if (holdRewind != nullptr) holdRewind->KeyframeWaitSince = -1;
			return false;
		}
		auto& h = *holdRewind;
		if (h.KeyframeWaitSince < 0) h.KeyframeWaitSince = h.LivedS;
		if (h.LivedS - h.KeyframeWaitSince < HrTuning::WindowS + 1.0) return true;
		h.KeyframeWaitSince = -1;
		return false;
	}

	void UnitySpaceApp::HookHoldRewindBookings(const cs::Ref<Battle>& fight)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto& h = *holdRewind;
		if (h.Hooked == fight) return;
		if (h.Hooked != nullptr)
		{
			// The fight leaves the rewind's eye: what it held back is booked, and the world's own hooks go back on.
			BookHoldRewindUpTo(std::numeric_limits<double>::max());
			h.Hooked->UnitDestroyed = h.OrigDestroyed;
			h.Hooked->UnitCaptured = h.OrigCaptured;
			h.Hooked->UnitSurrendered = h.OrigSurrendered;
			h.Hooked->SurrenderDecided = h.OrigDecided;
			h.Hooked->Ending = nullptr;
			h.Hooked->HoldEnd = nullptr; // US-T419: a fight held open ends on its next step
			h.Hooked = nullptr;
		}
		if (fight == nullptr || !HrTuning::DeferBookings) return;
		h.Hooked = fight;
		h.OrigDestroyed = fight->UnitDestroyed;
		h.OrigCaptured = fight->UnitCaptured;
		h.OrigSurrendered = fight->UnitSurrendered;
		h.OrigDecided = fight->SurrenderDecided;
		// Held back unless it is the player's own hull (the game must answer that at once: the seat, the session) or terms
		// being settled (cargo and prizes change hands): those are booked now, after everything held back, as a barrier.
		// US-T417: with DeferOwnFate the player's own hull waits out the window too (the helm is held dead meanwhile,
		// HoldRewindOwnFateHeld), so nothing here is booked at once any more.
		auto mine = [this](const cs::Ref<BattleUnit>& u)
		{
			if (HrTuning::DeferOwnFate) return false;
			return u != nullptr && (u->PlayerControlled() || (u->Ship != nullptr && World() != nullptr && u->Ship == World()->PilotedShip));
		};
		auto book = [this](bool now, std::function<void()> fire)
		{
			auto& st = *holdRewind;
			if (now)
			{
				BookHoldRewindUpTo(std::numeric_limits<double>::max());
				fire();
				double at = st.LivedS + 1e-6;
				if (at > st.BarrierT)
				{
					st.BarrierT = at;
					st.BarrierWhy = "your own hull's fate, or a surrender's terms, was settled";
				}
				return;
			}
			// The moment it happened: within the frame lived after LivedS (a resume point taken at the end of that frame
			// already shows it, one before does not).
			st.Bookings.push_back(HrBooking { st.LivedS + 1e-6, std::move(fire) });
		};
		auto destroyed = h.OrigDestroyed;
		fight->UnitDestroyed = [mine, book, destroyed](cs::Ref<Battle> b, cs::Ref<BattleUnit> u)
		{
			book(mine(u), [destroyed, b, u] { if (destroyed) destroyed(b, u); });
		};
		auto captured = h.OrigCaptured;
		fight->UnitCaptured = [mine, book, captured](cs::Ref<Battle> b, cs::Ref<BattleUnit> u, cs::Ref<Faction> captor)
		{
			book(mine(u), [captured, b, u, captor] { if (captured) captured(b, u, captor); });
		};
		auto surrendered = h.OrigSurrendered;
		fight->UnitSurrendered = [mine, book, surrendered](cs::Ref<Battle> b, cs::Ref<BattleUnit> u)
		{
			book(mine(u), [surrendered, b, u] { if (surrendered) surrendered(b, u); });
		};
		auto decided = h.OrigDecided;
		fight->SurrenderDecided = [book, decided](cs::Ref<Battle> b, cs::Ref<BattleUnit> u, cs::Ref<Faction> captor, SurrenderTerms terms)
		{
			book(!HrTuning::DeferOwnFate, [decided, b, u, captor, terms] { if (decided) decided(b, u, captor, terms); });
		};
		// The fight over: everything held back is booked before the war settles it (War::Resolve reads what was booked).
		fight->Ending = [this](cs::Ref<Battle>) { BookHoldRewindUpTo(std::numeric_limits<double>::max()); };
		// US-T419: the fight's end waits out the window like the losses in it, a mop-up meanwhile, so a hold can undo a win
		// (or a defeat). Timed from the first step it would have ended on; a resume to before that starts it afresh.
		fight->HoldEnd = [this](cs::Ref<Battle> b)
		{
			if (holdRewind == nullptr || !HrTuning::DeferFightEnd) return false;
			auto& st = *holdRewind;
			if (st.Hooked != b) return false;
			if (st.EndHeldSince == HrNone) st.EndHeldSince = st.LivedS;
			return st.LivedS - st.EndHeldSince < HrTuning::WindowS;
		};
	}

	void UnitySpaceApp::ClearHoldRewind(bool book)
	{
		if (holdRewind == nullptr) return;
		HoldRewindReattach(); // US-T424
		auto& h = *holdRewind;
		if (book)
		{
			// The world clock, if a hold had it set back for the drawing, comes back to now first.
			if (h.Active && Clock != nullptr && HrTuning::ViewSystemBack) Clock->SetJd(h.JdAtPress);
			HookHoldRewindBookings(nullptr);
		}
		// (A load: the bookings and the hooked fight are the old world's, thrown away with it.)
		double cooldown = h.CooldownUntil, frameClock = h.FrameClock;
		bool keyDown = h.KeyWasDown;
		auto text = h.Text;
		auto keptTest = h.Test;
		*holdRewind = HoldRewindState();
		holdRewind->FrameClock = frameClock;
		holdRewind->CooldownUntil = cooldown;
		holdRewind->KeyWasDown = keyDown;
		holdRewind->Text = text;
		holdRewind->Test = keptTest;
	}

	void UnitySpaceApp::ResetHoldRewindForSync(bool book)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		{
			HrSharedScope shared(coopShared, Coop::CoopPeer::Current() != nullptr);
			// A hold under way comes back to its press (as a tap does), so the save is of the world as it is.
			if (holdRewind->Active)
			{
				holdRewind->HeldS = 0;
				EndHoldRewind();
			}
			ClearHoldRewind(book);
		}
		// What a load keeps for single-player (the cooldown, the key's edge) starts from zero on every peer alike.
		holdRewind->FrameClock = 0;
		holdRewind->CooldownUntil = 0;
		holdRewind->KeyWasDown = false;
		coopHrStepFrame = -1;
		coopHrFight = nullptr;
		coopHrEntry.clear();
		coopHrAll = false;
		coopHrMask = 0;
	}

	unsigned long long UnitySpaceApp::CoopHoldRewindSeats()
	{
		unsigned long long h = 1469598103934665603ull;
		auto mix = [&h](long long v) { h = (h ^ static_cast<unsigned long long>(v)) * 1099511628211ull; };
		mix(coopHelmSeat + 1);
		mix(Piloted.IsNone() || Piloted.Ship == nullptr ? -1 : Piloted.Ship->Id);
		if (session != nullptr)
			for (const auto& p : session->Players())
			{
				mix(p.Seat);
				mix(static_cast<long long>(session->State()->SeatIdOf(p)) + 1);
			}
		return h;
	}

	long long UnitySpaceApp::CoopHoldRewindFrameNo(Coop::CoopPeer& peer) const
	{
		// The host's frame number moves on when its record opens (at the clock step, after the hold's step).
		return peer.IsHost() && peer.Open() == nullptr ? peer.Frame() + 1 : peer.Frame();
	}

	double UnitySpaceApp::HoldRewindRecordedS() const
	{
		if (holdRewind == nullptr || holdRewind->Fight == nullptr) return -1;
		return holdRewind->LivedS - holdRewind->FightSince;
	}

	double UnitySpaceApp::HoldRewindBackS() const
	{
		return holdRewind != nullptr && holdRewind->Active ? holdRewind->PressT - holdRewind->Tau : 0;
	}

	cs::String UnitySpaceApp::CoopHoldRewindWaiting() const
	{
		Coop::CoopPeer* peer = Coop::CoopPeer::Current();
		if (peer == nullptr || coopHrMask == 0 || coopHrAll) return cs::String();
		cs::String holding, waiting;
		for (const auto& p : peer->Players)
		{
			if (p == nullptr || p->Left) continue;
			cs::String& to = (coopHrMask & (1 << p->Seat)) != 0 ? holding : waiting;
			if (!to.empty()) to += ", ";
			to += cs::Cat("P", p->Seat + 1, " ", p->Name);
		}
		cs::String keys = Input::KeyBindings::Keys(Input::KeyAction::HoldRewind);
		return cs::Cat("REWIND: ", holding, " hold", holding.find(',') == cs::String::npos ? "s" : "", " ", keys, "  -  waiting for ", waiting, " to hold ", keys,
			" (it plays only while every player holds it)");
	}

	// ------------------------------------------------------------------------------------------- recording

	void UnitySpaceApp::RecordHoldRewind(float dt)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto& h = *holdRewind;
		if (h.Active) return;
		// US-T423: co-op records too, on every peer alike (the frame's dt, the host's fight, the shared helm); the rewind is
		// the party's, so one machine's -norewind does not reach it.
		const bool coop = Coop::CoopPeer::Current() != nullptr;
		// Single player, in a run that keeps a rewind at all (not the tour, a capture or -norewind).
		if ((!coop && !rewindEnabled) || Clock == nullptr || World() == nullptr)
		{
			if (!h.Clock.empty() || h.Fight != nullptr || h.Hooked != nullptr) ClearHoldRewind();
			return;
		}
		// Nothing lived: the clock stopped, or between the stars (a barrier, through the seat, once flying again).
		if (Clock->Paused() || Travel->Active || InFtlCruise()) return;
		HrSharedScope shared(coopShared, coop);

		h.LivedS += dt;
		double t = h.LivedS;
		TrackHoldRewindShips(t);
		bool piloting = IsPiloting();
		// US-T414: the hull the player is aboard by ANY seat (the helm, or a turret, through HeldSeat's own
		// EmbodimentSubject), not only while piloting -- piloting stays its own flag, for the pilot state capture
		// below, which a turret does not have and so does not try to restore.
		// US-T423: in co-op only the shared helm (each player's other seats are theirs: a change of any is a barrier).
		cs::Ref<IEmbodiable> heldSeat = !coop ? HeldSeat()
			: piloting && Piloted.Ship != nullptr ? cs::Ref<IEmbodiable>(Embodiables::HelmOf(World(), Piloted.Ship)) : nullptr;
		auto ship = heldSeat != nullptr ? heldSeat->Subject().Ship : nullptr;
		// US-T417: the census keys on the body (a craft off your own carrier is the carrier), the seat journal on the seat.
		auto bodyShip = HrBodyShip(heldSeat);
		auto heldNonHelm = coop ? nullptr : HeldNonHelm();
		// US-T423: in co-op the fight the host keeps (its frame names it), so every peer keeps the same one.
		auto fight = !coop ? HoldRewindBattle() : coopHrFight != nullptr && !coopHrFight->Over ? coopHrFight : nullptr;
		// US-T424: a fight left (the hull flew clear of it, or it went out of view) is still kept while it goes on, so a hold
		// can go back to before the hull left it (the resume point puts the hull back in it); only another fight replaces it.
		// (In co-op too: the fight kept and the war are the same on every peer, so every peer keeps the same one.)
		if (fight == nullptr && HrTuning::KeepFightLeft && h.Fight != nullptr && !h.Fight->Over && World()->War != nullptr &&
			World()->War->Battles.Contains(h.Fight))
			fight = h.Fight;
		// US-T419: fought on since it would have ended (or put back to before): its end, if it comes again, starts afresh.
		if (h.Hooked == nullptr || !h.Hooked->EndHeld) h.EndHeldSince = HrNone;
		bool entering = false;
		if (fight != h.Fight)
		{
			// Another fight (or none): its tracks start now, and nothing before now can be played on from...
			h.Units.clear();
			h.Missiles.clear();
			h.Rounds.clear();
			h.Decoys.clear();
			// ...US-T424: unless it is the first fight watched since none was: then the helm's resume points before it stay,
			// and a resume to one of them takes the fight back if it was born now (or lets the hull go of it, or leaves a
			// fight that only came into view going on from now).
			// (In co-op too: every peer sees the same fight entered at the same frame, and takes it back at the same frame.)
			entering = HrTuning::UndoFightStart && h.Fight == nullptr && fight != nullptr;
			h.Entered = entering ? fight : nullptr;
			h.EnteredT = entering ? t : HrNone;
			h.EnteredBorn = entering && fight->Seconds <= HrTuning::BornS && World()->War != nullptr && World()->War->Battles.Contains(fight);
			if (!entering) h.Anchors.clear();
			h.Fight = fight;
			h.FightSince = t;
			h.NextAnchorAt = 0;
		}
		// US-T410: the bookings of the fight in view wait out the window.
		HookHoldRewindBookings(fight);
		HrCensus census = HrTakeCensus(fight, bodyShip, pilotFrameBody, piloting);
		if (coop) census.Coop = CoopHoldRewindSeats();
		// US-T424: the fight entered is no barrier (only what else changed this frame): the census before it counted none.
		HrCensus was = h.Last;
		if (entering)
		{
			was.Fight = census.Fight;
			was.Over = census.Over;
			was.Units = census.Units;
			was.Dead = census.Dead;
			was.Fled = census.Fled;
			was.Struck = census.Struck;
			was.Taken = census.Taken;
			was.Lost = census.Lost;
			was.Seats = census.Seats;
			was.Flags = census.Flags;
		}
		if (h.HasLast && !(census == was))
		{
			h.BarrierWhy = HrWhyChanged(was, census);
			h.BarrierT = t;
		}
		h.Last = census;
		h.HasLast = true;
		if (ship != h.PilotShip)
		{
			h.Pilot.clear();
			h.PilotShip = ship;
		}

		double s = fight != nullptr ? fight->Seconds : 0;
		h.Clock.push_back(HrClockMark { t, s, Clock->Jd });
		bool capitalTick = t >= h.NextCapitalAt;
		if (capitalTick) h.NextCapitalAt = t + 1.0 / HrTuning::CapitalHz;
		bool craftTick = t >= h.NextCraftAt;
		if (craftTick) h.NextCraftAt = t + 1.0 / HrTuning::CraftHz;

		if (fight != nullptr)
		{
			for (const auto& u : fight->Units)
			{
				if (u == nullptr) continue;
				auto& tr = h.Units[u.get()];
				if (tr.Obj == nullptr) tr.Obj = u;
				bool changed = !tr.Poses.empty() && (tr.Poses.back().Dead != u->Dead || tr.Poses.back().Fled != u->Fled || tr.Poses.back().Docked != u->Docked);
				bool due = (u->PlayerControlled() && HrTuning::PlayerEveryFrame) || (u->Kind == UnitKind::Fighter ? craftTick : capitalTick);
				// A hull out of the fight holds its last pose: kept again only when it comes back.
				if (!tr.Poses.empty() && !changed && (!due || !u->Present())) continue;
				HrPose p;
				p.T = t;
				p.S = s;
				p.Pos = u->Pos;
				p.Vel = u->Vel;
				p.Fwd = u->Forward;
				p.Dead = u->Dead;
				p.Fled = u->Fled;
				p.Docked = u->Docked;
				tr.Poses.push_back(p);
				h.Samples++;
			}
			if (craftTick && h.Samples < HrTuning::MaxSamples)
			{
				HrRecordProjectiles(h.Missiles, fight->Missiles, t, s, h.Samples);
				HrRecordProjectiles(h.Rounds, fight->Rounds, t, s, h.Samples);
				HrRecordProjectiles(h.Decoys, fight->Decoys, t, s, h.Samples);
			}
		}
		if (piloting) h.Pilot.push_back(HrPilotPose { t, pilot(), pilotFrameBody });

		// US-T424: a resume point every AnchorS even with no fight or helm (an empty one: nothing of a fight to put back), so
		// a hold can play on from before a fight began when only watching.
		if (t >= h.NextAnchorAt)
		{
			h.Anchors.push_back(HrCaptureAnchor(t, fight, piloting, pilot(), ship, pilotFrameBody, heldNonHelm));
			h.NextAnchorAt = t + HrTuning::AnchorS;
			// The self-test's mark: the state this resume point holds, hashed now (it is the live state this instant).
			if (h.Test.On && h.Test.Phase == 1 && h.Test.M < 0 && fight != nullptr && fight == h.Test.Fight)
			{
				h.Test.M = t;
				h.Test.DM = HoldRewindFightDigest(fight);
			}
		}

		// Only the window (and one pose before it, for the curve into it) is kept, and nothing before the floor.
		double cutoff = std::max(t - HrTuning::WindowS - 1.0, h.FloorT);
		HrPruneTracks(h.Units, cutoff, h.Samples);
		HrPruneTracks(h.Missiles, cutoff, h.Samples);
		HrPruneTracks(h.Rounds, cutoff, h.Samples);
		HrPruneTracks(h.Decoys, cutoff, h.Samples);
		HrPruneFront(h.Clock, cutoff, nullptr);
		HrPruneFront(h.Pilot, cutoff, nullptr);
		while (h.Anchors.size() >= 2 && h.Anchors[1]->T < cutoff) h.Anchors.pop_front();
		// What is older than the window can no longer be rewound to: the world books it now.
		BookHoldRewindUpTo(t - HrTuning::WindowS);
	}

	cs::String UnitySpaceApp::HoldRewindRefusal()
	{
		auto& h = *holdRewind;
		// US-T423: everything here is the same on every peer in co-op (the frame clock, the shared world and helm).
		const bool coop = Coop::CoopPeer::Current() != nullptr;
		if (!coop && !rewindEnabled) return "Rewind is off in this run (-norewind, -demo, -capture)";
		if (h.FrameClock < h.CooldownUntil) return cs::Cat("REWIND ready in ", static_cast<int>(std::ceil(h.CooldownUntil - h.FrameClock)), " s");
		if (Travel->Active || InFtlCruise() || Ftl->Stage != FtlStage::Idle) return "No rewind between the stars or under FTL";
		if (!coop && rewindReplaying) return "A rewind is already replaying";
		if (h.Fight == nullptr && h.Pilot.empty()) return "Nothing here to rewind: fly a hull or watch a fight";
		double oldest = HoldRewindCurve::Oldest(h.LivedS, HrTuning::WindowS, h.Clock.empty() ? h.LivedS : h.Clock.front().T, h.FloorT);
		if (h.Clock.size() < 2 || h.LivedS - oldest < HrTuning::MinHoldS) return "Nothing to rewind yet";
		return cs::String();
	}

	// ------------------------------------------------------------------------------------------- the hold

	bool UnitySpaceApp::UpdateHoldRewind(float dt)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		Coop::CoopPeer* peer = Coop::CoopPeer::Current();
		if (peer == nullptr)
		{
			// US-T420: with real keys the self-test presses Shift+R itself: only the key counts, as for a player.
			auto& h = *holdRewind;
			return StepHoldRewind(dt, Input.HoldRewind || (h.Test.ForceDown && !h.Test.RealKeys));
		}
		// US-T423: co-op. Every peer steps the hold on the frame's key (every player's held, the host's call) and dt, inside
		// the shared steps; whether the frame is a hold frame is the host's (its "ho"), which a client follows.
		bool hostHold = false;
		if (peer->IsClient())
		{
			const Coop::FrameRecord* rec = peer->ClientRecord;
			if (rec == nullptr) return false;
			for (const auto& e : rec->Entries)
				if (e == "ho") hostHold = true;
			// A frame replayed again (its Update stopped at the pause menu before the clock step) has had its hold step.
			if (rec->Frame == coopHrStepFrame) return hostHold;
			coopHrStepFrame = rec->Frame;
		}
		bool down = CoopHoldRewindKey(*peer);
		bool held;
		{
			HrSharedScope shared(coopShared, true);
			held = StepHoldRewind(dt, down);
		}
		if (peer->IsHost()) return held;
		if (held != hostHold && !coopHrMismatchWarned)
		{
			coopHrMismatchWarned = true;
			Coop::CoopPeer::Warn(cs::Cat("hold rewind: this peer's hold (", held ? "held" : "not held", ") differs from the host's frame ", peer->Frame(),
				" (", hostHold ? "held" : "not held", "): following the host"));
		}
		return hostHold;
	}

	bool UnitySpaceApp::CoopHoldRewindKey(Coop::CoopPeer& peer)
	{
		namespace Wire = Coop::Wire;
		bool local = Input.HoldRewind;
		bool all = false;
		int mask = 0, index = -1;
		static const cs::List<cs::Ref<Battle>> none;
		const auto& battles = World() != nullptr ? World()->War->Battles : none;
		if (peer.IsHost())
		{
			if (Coop::CoopSmoke::HostHoldsRewind()) local = true;
			all = peer.HostHoldAll(local, mask);
			// The fight kept is the host's: the helm's, else the one its camera watches.
			cs::Ref<Battle> fight;
			{
				HrSharedScope shared(coopShared, true);
				fight = HoldRewindBattle();
			}
			for (int i = 0; fight != nullptr && i < battles.Count(); i++)
				if (battles[i] == fight) index = i;
			coopHrEntry = all || index >= 0 || mask != 0 ? cs::Cat("hr ", all ? 1 : 0, " ", index, " ", mask) : cs::String();
		}
		else
		{
			if (local != coopHrSent)
			{
				coopHrSent = local;
				peer.RequestHoldRewind(local);
			}
			for (const auto& e : peer.ClientRecord->Entries)
			{
				if (!Wire::StartsWith(e, "hr ")) continue;
				auto f = Wire::Split(e, ' ');
				if (f.size() < 4) continue;
				try
				{
					all = f[1] == "1";
					index = Wire::ParseI(f[2]);
					mask = Wire::ParseI(f[3]);
				}
				catch (const std::exception&) { all = false; index = -1; mask = 0; }
			}
		}
		coopHrFight = index >= 0 && index < battles.Count() ? battles[index] : nullptr;
		coopHrAll = all;
		coopHrMask = mask;
		int state = (all ? 1 << 8 : 0) | mask;
		if (state != coopHrLogged)
		{
			coopHrLogged = state;
			long long frame = CoopHoldRewindFrameNo(peer);
			if (all) Coop::CoopPeer::Log(cs::Cat("hold rewind: every player holds Shift+R at frame ", frame));
			else if (mask == 0) Coop::CoopPeer::Log(cs::Cat("hold rewind: nobody holds Shift+R at frame ", frame));
			else Coop::CoopPeer::Log(cs::Cat("hold rewind: ", CoopHoldRewindWaiting(), " (frame ", frame, ")"));
		}
		return all;
	}

	void UnitySpaceApp::CoopHoldRewindFrame(bool held)
	{
		Coop::CoopPeer* peer = Coop::CoopPeer::Current();
		if (peer == nullptr) return;
		if (peer->IsHost())
		{
			if (peer->Open() != nullptr) return;
			Coop::FrameRecord* rec = peer->HostOpenHoldFrame();
			rec->Before = Coop::ClockState::Of(*Clock);
			rec->JdAfter = Clock->Jd;
			rec->Entries.push_back("ho");
			if (!coopHrEntry.empty()) rec->Entries.push_back(coopHrEntry);
			return;
		}
		if (held && peer->ClientRecord != nullptr) peer->Consumed = true;
	}

	bool UnitySpaceApp::StepHoldRewind(float dt, bool down)
	{
		auto& h = *holdRewind;
		h.FrameClock += dt; // US-T423: the cooldown's clock
		if (h.JustResumed > 0) h.JustResumed--;
		// US-T417: set again by UpdatePiloting this frame while the hull flown's fate is still held back.
		if (h.OwnFateHeld == 0) h.OwnFateSince = HrNone;
		h.OwnFateHeld = 0;
		bool pressed = down && !h.KeyWasDown;
		h.KeyWasDown = down;
		if (!h.Active)
		{
			if (!pressed) return false;
			cs::String why = HoldRewindRefusal();
			if (!why.empty())
			{
				ShowToast(why);
				return false;
			}
			// The designer's rule: real time first, then real seconds back.
			if (Clock->WarpIndex > 0) Clock->SetWarp(0);
			bool piloting = IsPiloting() && World()->PilotedShip == h.PilotShip && !h.Pilot.empty();
			Coop::CoopPeer* peer = Coop::CoopPeer::Current();
			// US-T423: in co-op no seat but the shared helm is put back (any player's seat changed is a barrier instead).
			h.AtPress = HrCaptureAnchor(h.LivedS, h.Fight, piloting, pilot(), piloting ? h.PilotShip : nullptr, pilotFrameBody, peer != nullptr ? nullptr : HeldNonHelm());
			h.Active = true;
			h.Releasing = false;
			h.PressT = h.LivedS;
			h.PressFx = Time::unscaledTime;
			h.Tau = h.LivedS;
			h.HeldS = 0;
			h.Rate = HrTuning::StartRate;
			h.JdAtPress = Clock->Jd;
			h.Oldest = HoldRewindCurve::Oldest(h.LivedS, HrTuning::WindowS, h.Clock.front().T, h.FloorT);
			if (peer != nullptr)
				Coop::CoopPeer::Log(cs::Cat("hold rewind: plays the past back from frame ", CoopHoldRewindFrameNo(*peer), " (", h.Fight != nullptr ? h.Fight->Name : cs::String("no fight"),
					", ", cs::Fmt(h.LivedS - h.Oldest, "F2"), " s reachable)"));
			return true;
		}
		if (!down)
		{
			// Let go: a tap ends at once; a hold eases to a stop over ReleaseEaseS, then plays on.
			if (h.HeldS < HrTuning::MinHoldS || HrTuning::ReleaseEaseS <= 0)
			{
				EndHoldRewind();
				return false;
			}
			if (!h.Releasing)
			{
				h.Releasing = true;
				h.ReleaseLeft = HrTuning::ReleaseEaseS;
				h.ReleaseRate = h.Rate;
			}
			h.ReleaseLeft -= dt;
			if (h.ReleaseLeft <= 0)
			{
				EndHoldRewind();
				return false;
			}
			h.Rate = h.ReleaseRate * (1.0 - HoldRewindCurve::Smooth(1.0 - h.ReleaseLeft / HrTuning::ReleaseEaseS));
		}
		else
		{
			// Held (again): the ease goes on from where it was.
			h.Releasing = false;
			h.HeldS += dt;
			double rate = HoldRewindCurve::Rate(h.HeldS, HrTuning::StartRate, HrTuning::TopRate, HrTuning::RampS);
			h.Rate = HoldRewindCurve::Braked(rate, h.Tau, h.Oldest, HrTuning::BrakeS, HrTuning::BrakeMinRate);
		}
		h.Tau = HoldRewindCurve::Step(h.Tau, dt, h.Rate, h.Oldest, h.Test.On ? h.Test.Target : HrNone);
		PlayHoldRewind();
		return true;
	}

	void UnitySpaceApp::PlayHoldRewind()
	{
		auto& h = *holdRewind;
		double tau = h.Tau;
		// The clocks at the moment reached: the fight's, and (for the drawing only) the world's.
		if (!h.Clock.empty())
		{
			auto it = std::lower_bound(h.Clock.begin(), h.Clock.end(), tau, [](const HrClockMark& m, double t) { return m.T < t; });
			double s = 0, jd = h.JdAtPress;
			if (it == h.Clock.end())
			{
				s = h.Clock.back().S;
				jd = h.Clock.back().Jd;
			}
			else if (it == h.Clock.begin() || it->T <= tau)
			{
				s = it->S;
				jd = it->Jd;
			}
			else
			{
				const HrClockMark& a = *(it - 1);
				double f = (tau - a.T) / std::max(1e-9, it->T - a.T);
				s = a.S + (it->S - a.S) * f;
				jd = a.Jd + (it->Jd - a.Jd) * f;
			}
			// US-T424: before the fight was entered the marks hold no clock of it (it stays at its first, put back on letting go).
			if (h.Fight != nullptr && (h.EnteredT == HrNone || tau >= h.EnteredT)) h.Fight->Seconds = s;
			if (HrTuning::ViewSystemBack && Clock != nullptr && jd > 0)
			{
				Clock->SetJd(jd);
				// US-T412: Clock->Jd alone is not enough. Bodies are laid out from Ephemeris()->Helio(i), a cache
				// filled only by Ephemeris::Update(jd), which normally runs inside Galaxy->Update(Clock->Jd) -
				// skipped for the whole frame while a hold plays (UnitySpaceApp.cpp's held branch returns before
				// reaching it). Without this, planets, stations, lane endpoints and anything parked in orbit (which
				// reads the same cache, e.g. Fleet::BaseStateOf's ephemeris->Helio(s->Orbit->Body)) stay frozen at
				// the moment the hold began while ships elsewhere correctly rewind through Fleet::StateOf(ship, jd).
				// Ephemeris::Update is pure (no RNG, no writes outside its own cache), so this is safe every frame of
				// a hold; the ordinary Galaxy->Update(Clock->Jd) call recomputes it again, correctly, the instant
				// play resumes (ClearHoldRewind/EndHoldRewind need not restore it themselves).
				if (Ephemeris() != nullptr) Ephemeris()->Update(jd);
			}
		}
		if (h.Fight != nullptr)
		{
			// US-T424: before a fight born in the window began, none of it is there (its ships are drawn on their schedules,
			// HoldRewindBeforeFight; the hull flown on the helm's track), and its ships are out of it for every reader.
			bool unborn = HoldRewindBeforeFight(h.Fight);
			if (unborn && h.Detached.empty())
			{
				for (const auto& u : h.Fight->Units)
					if (u != nullptr && u->Ship != nullptr && u->Ship->Battle == h.Fight)
					{
						u->Ship->Battle = nullptr;
						h.Detached.push_back(u->Ship);
					}
			}
			else if (!unborn) HoldRewindReattach();
			auto flown = IsPiloting() ? World()->PilotedShip : nullptr;
			for (const auto& u : h.Fight->Units)
			{
				if (u == nullptr) continue;
				auto found = h.Units.find(u.get());
				if (found == h.Units.end()) continue;
				const auto& poses = found->second.Poses;
				HrPose p;
				if (!HrPoseAt(poses, tau, p)) continue;
				// A hull that came into the fight after its tracks began is not there yet before its first pose.
				bool notYet = (!poses.empty() && poses.front().T > h.FightSince + 0.5 && tau < poses.front().T) ||
					(unborn && (flown == nullptr || u->Ship != flown));
				u->Pos = p.Pos;
				u->Vel = p.Vel;
				u->Forward = p.Fwd;
				u->Dead = p.Dead;
				u->Fled = p.Fled;
				u->Docked = p.Docked || notYet;
			}
			// US-T426: the hulls' damage as it was (parts shot away, breaches, the hull's share), from the resume point at or
			// after the moment reached, so a hull mends as the hold goes back past its wounds. Every way a hold ends puts a
			// whole resume point (or the press's) back over it, so nothing of this lasts.
			const HrAnchor* damage = nullptr;
			for (const auto& a : h.Anchors)
				if (a->Live == h.Fight && a->T >= tau - 1e-9)
				{
					damage = a.get();
					break;
				}
			if (damage != nullptr && damage != h.DamageShown)
			{
				for (const auto& p : damage->Combats) *p.first = p.second;
				for (const auto& p : damage->Subsystems) *p.first = p.second;
				h.DamageShown = damage;
			}
			double interval = 1.0 / HrTuning::CraftHz;
			HrPlayProjectiles(h.Fight->Missiles, h.Missiles, tau, interval);
			HrPlayProjectiles(h.Fight->Rounds, h.Rounds, tau, interval);
			HrPlayProjectiles(h.Fight->Decoys, h.Decoys, tau, interval);
		}
		if (!h.Pilot.empty() && IsPiloting() && World()->PilotedShip == h.PilotShip)
		{
			auto it = std::lower_bound(h.Pilot.begin(), h.Pilot.end(), tau, [](const HrPilotPose& p, double t) { return p.T < t; });
			if (it == h.Pilot.end()) --it;
			// US-T422: in the frame it was kept in (the helm may have crossed into another body's space since).
			if (pilotFrameBody != it->Frame)
			{
				pilotFrameBody = it->Frame;
				if (helmFlight != nullptr) helmFlight->FrameBody = it->Frame;
			}
			pilot() = it->State;
			pilotAim = pilot().Forward;
		}
	}

	void UnitySpaceApp::RestoreHoldRewindSeat(const cs::Ref<IEmbodiable>& seat, bool piloting, const cs::Ref<Ship>& pilotShip)
	{
		auto held = HeldNonHelm();
		bool sameSeat = seat == nullptr ? held == nullptr : held != nullptr && EmbodiableEquals(seat, held);
		bool helmNow = IsPiloting() && Piloted.Ship == pilotShip;
		bool samePilot = piloting ? helmNow : !IsPiloting();
		if (sameSeat && samePilot) return;
		if (!sameSeat)
		{
			if (seat == nullptr) ReleaseSeat();
			else
			{
				// US-T422: the seat the resume point had may be on another body than the one held now (a gun or craft taken on
				// another hull since). The embody gate lets a seat be taken only on a body the player is aboard, so the helm
				// of that seat's body is taken first (the helm is the player's own to take, ungated, as a load does), which
				// puts the player aboard; the seat is then a seat on the body held, and the helm is let go below if the resume
				// point had none (the order US-T414 settled: the seat first, then the helm let go).
				auto body = HrBodyShip(seat);
				bool aboard = body != nullptr && IsPiloting() && Piloted.Ship == body;
				if (!aboard && body != nullptr && !body->Destroyed && HrBodyShip(HeldSeat()) != body)
				{
					if (IsPiloting()) ExitPiloting();
					EnterPiloting(Target::OfShip(body));
				}
				if (!SwitchSeat(seat)) UnityCompat::Debug::LogWarning(cs::Cat("Hold rewind: could not put the player back at ", seat->Name()));
			}
		}
		// Then the helm, as it was.
		helmNow = IsPiloting() && Piloted.Ship == pilotShip;
		if (piloting && !helmNow && IsPiloting()) ExitPiloting();
		if (piloting && !helmNow && pilotShip != nullptr && !pilotShip->Destroyed) EnterPiloting(Target::OfShip(pilotShip));
		else if (!piloting && IsPiloting()) ExitPiloting();
	}

	bool UnitySpaceApp::HoldRewindOwnFateHeld(const cs::Ref<Ship>& ship)
	{
		if (!HrTuning::DeferBookings || !HrTuning::DeferOwnFate || ship == nullptr || ship->Battle == nullptr) return false;
		auto battle = ship->Battle;
		auto unit = battle->UnitOf(ship);
		if (unit == nullptr || !(unit->Dead || unit->Captured)) return false;
		// Lost or taken in the fight, its booking held back: the helm follows the wreck (or the prize) and reads nothing.
		pilotFrameBody = battle->Body;
		SeedPilot(unit->Pos + battle->Anchor, unit->Vel, unit->Forward);
		unit->PlayerFireHeld = false;
		unit->PlayerSecondaryHeld = false;
		SpaceCamera->PilotView = PilotViewRotation(PilotControlMode);
		if (holdRewind != nullptr)
		{
			holdRewind->OwnFateHeld = unit->Captured ? 2 : 1;
			if (holdRewind->OwnFateSince == HrNone) holdRewind->OwnFateSince = holdRewind->LivedS;
		}
		return true;
	}

	void UnitySpaceApp::EndHoldRewind()
	{
		auto& h = *holdRewind;
		HoldRewindReattach(); // US-T424: the fight's ships back in it before anything reads them
		h.DamageShown = nullptr; // US-T426: what is put back below (or the press) replaces the damage the playback showed
		h.Active = false;
		h.Releasing = false;
		auto atPress = h.AtPress;
		h.AtPress = nullptr;
		Coop::CoopPeer* peer = Coop::CoopPeer::Current();
		// The world clock comes back to now: only the fight (and the helm) go back.
		if (HrTuning::ViewSystemBack && Clock != nullptr) Clock->SetJd(h.JdAtPress);
		auto putBack = [this, peer](const HrAnchor& a)
		{
			// US-T417: the seat first (its hand-backs and take-charges act on the live fight), then the fight over it, which
			// then matches the seats it was taken with. US-T423: in co-op no seat is put back (a seat or the helm changed
			// since by any player is a barrier, so the seats are already as they were).
			if (peer == nullptr) RestoreHoldRewindSeat(a.Seat, a.HasPilot, a.PilotShip);
			HrRestoreFight(a);
			if (a.HasPilot && IsPiloting() && World()->PilotedShip == a.PilotShip && a.PilotFrame >= 0)
			{
				// US-T422: in the frame it was taken in (a crossing into another body's space since is undone with it), as a
				// save loaded puts the frame and the pose back together (UnitySpaceApp.Saves.cpp).
				pilotFrameBody = a.PilotFrame;
				if (helmFlight != nullptr) helmFlight->FrameBody = a.PilotFrame;
				pilot() = a.Pilot;
				pilotAim = a.Pilot.Forward;
			}
		};
		h.JustResumed = 2;
		// US-T421: the effects play on from the press (a tap, a hold that could not play on from back there) unless a
		// resume point is put back below.
		h.ResumeFx = h.PressFx;
		h.FxEnds++;
		// A tap, or a hold that never got back: the present as it was at the press.
		// US-T423: in co-op every peer says where it played on from, and the fight it has then (the same on every peer).
		auto coopLog = [this, peer](const cs::String& what)
		{
			if (peer == nullptr) return;
			auto& st = *holdRewind;
			Coop::CoopPeer::Log(cs::Cat("hold rewind: played on ", what, " at frame ", CoopHoldRewindFrameNo(*peer), "; fight digest ",
				Coop::Wire::Hex(HoldRewindFightDigest(st.Fight))));
		};
		if (h.HeldS < HrTuning::MinHoldS || h.PressT - h.Tau < 1e-3)
		{
			if (atPress != nullptr) putBack(*atPress);
			coopLog("from the press (a tap)");
			return;
		}
		// The resume point: the newest kept at or before the moment reached, none from before the last barrier nor the
		// floor (then the oldest after them, nearer now than the moment reached).
		double from = std::max(h.BarrierT, h.FloorT) - 1e-9;
		std::shared_ptr<HrAnchor> best;
		for (const auto& a : h.Anchors)
			if (a->T >= from && a->T <= h.Tau + 1e-9) best = a;
		bool clamped = false;
		if (best == nullptr)
			for (const auto& a : h.Anchors)
				if (a->T >= from && a->T > h.Tau && a->T < h.PressT)
				{
					best = a;
					clamped = true;
					break;
				}
		h.CooldownUntil = h.FrameClock + HrTuning::CooldownS; // US-T423: on the frame clock (was the wall clock)
		if (best == nullptr)
		{
			if (atPress != nullptr) putBack(*atPress);
			ShowToast(cs::Cat("Could not play on from back there: ", h.BarrierWhy.empty() ? cs::String("nothing kept to play on from") : h.BarrierWhy,
				", and that is not undone yet. Back to now"));
			coopLog(cs::Cat("from the press (", h.BarrierWhy.empty() ? cs::String("nothing kept to play on from") : h.BarrierWhy, ")"));
			return;
		}
		// US-T424: a resume point from before the fight watched was entered: the fight is taken back if it was born in the
		// window, let go of by the hull if the hull joined one already under way, or (one that only came into view) left to
		// go on from now; either way the helm plays on from back there, outside it.
		cs::String beforeFight;
		if (h.Entered != nullptr && h.Entered == h.Fight && best->T < h.EnteredT)
		{
			auto fight = h.Fight;
			// What it held back never happened now; its hooks go back to the world's (nothing left to book).
			while (!h.Bookings.empty() && h.Bookings.back().T > best->T) h.Bookings.pop_back();
			HookHoldRewindBookings(nullptr);
			if (h.EnteredBorn && World()->War != nullptr && World()->War->Unstart(fight, Clock->Jd))
			{
				if (battleTimeFor == fight) battleTimeFor = nullptr;
				beforeFight = cs::Cat(": before ", fight->Name, " began");
			}
			else
			{
				// Under way before the hull came: it goes on as it was at the press (the playback moved its units).
				if (atPress != nullptr) HrRestoreFight(*atPress);
				auto ship = World()->PilotedShip;
				auto mine = ship != nullptr && ship->Battle == fight ? fight->UnitOf(ship) : nullptr;
				if (mine != nullptr)
				{
					// The hull joined it in the window: out of it again, as a hull that flew clear of a fight is.
					mine->Fled = true;
					ship->Battle = nullptr;
					beforeFight = cs::Cat(": before you joined ", fight->Name);
				}
				else beforeFight = cs::Cat(": before ", fight->Name, " came into view (it goes on from now)");
			}
			h.Units.clear();
			h.Missiles.clear();
			h.Rounds.clear();
			h.Decoys.clear();
			h.Fight = nullptr;
			h.Entered = nullptr;
			h.EnteredT = HrNone;
			h.EnteredBorn = false;
			h.EndHeldSince = HrNone;
		}
		putBack(*best);
		coopLog(cs::Cat("from ", cs::Fmt(h.PressT - best->T, "F2"), " s back", clamped ? cs::Cat(" (no further: ", h.BarrierWhy, ")") : cs::String(), beforeFight));
		h.ResumeFx = h.PressFx - static_cast<float>(h.PressT - best->T);
		double went = h.PressT - best->T;
		if (h.Test.On)
		{
			h.Test.HaveResume = true;
			h.Test.Resumes++;
			h.Test.ResumedAnchorT = best->T;
			h.Test.DR = h.Fight != nullptr ? HoldRewindFightDigest(h.Fight) : 0;
			// US-T410: the floor this resume sets is h.LivedS at this instant (h.FloorT = h.LivedS below); snapshot it,
			// since by the time the self-test's own tick checks it, h.LivedS has moved on with real play.
			h.Test.FloorAtResume = h.LivedS;
		}
		// The world's bookings from after the resume point never happened now: dropped. The rest move up with the tracks.
		while (!h.Bookings.empty() && h.Bookings.back().T > best->T) h.Bookings.pop_back();
		// The old future is gone: what was kept after the resume point is dropped, and the rest moves up so that the
		// resume point is now (the next hold goes back no further than it: the floor).
		double by = h.LivedS - best->T;
		HrTrimAndShift(h.Units, best->T, by, h.Samples);
		HrTrimAndShift(h.Missiles, best->T, by, h.Samples);
		HrTrimAndShift(h.Rounds, best->T, by, h.Samples);
		HrTrimAndShift(h.Decoys, best->T, by, h.Samples);
		HrTrimBack(h.Clock, best->T, nullptr);
		HrShiftPoses(h.Clock, by);
		HrTrimBack(h.Pilot, best->T, nullptr);
		HrShiftPoses(h.Pilot, by);
		for (auto& b : h.Bookings) b.T += by;
		while (!h.Anchors.empty() && h.Anchors.back()->T > best->T) h.Anchors.pop_back();
		for (auto& a : h.Anchors) a->T += by;
		if (h.BarrierT > HrNone) h.BarrierT += by;
		h.FloorT = h.LivedS;
		h.HasLast = false;
		// The world clock in the marks kept: what is drawn of the system on the next hold is the world as it was when kept.
		// US-T99's frame log can no longer replay across this: the fight was set back by hand, not by the logged frames.
		// A seek lands on its keyframes from here (they hold the whole world, so they are still exact).
		rewindFrames.clear();
		rewindKeySerial.clear();
		ShowToast(cs::Cat("Rewound ", cs::Fmt(went, "F1"), " s", clamped ? cs::Cat(" (no further: ", h.BarrierWhy, ")") : cs::String(),
			!beforeFight.empty() ? beforeFight
			: h.Fight == nullptr ? cs::String(": your hull's flight; the rest of the system goes on from now") : cs::String(": play on")));
	}

	void UnitySpaceApp::DrawHoldRewind()
	{
		// US-T423: in co-op too (the rewind is the party's: one machine's -norewind does not hide it).
		const bool coop = Coop::CoopPeer::Current() != nullptr;
		if (holdRewind == nullptr || ScreenCleared || (!coop && !rewindEnabled)) return;
		auto& h = *holdRewind;
		cs::String waiting = coop ? CoopHoldRewindWaiting() : cs::String();
		if (!h.Active && h.Fight == nullptr && !IsPiloting() && waiting.empty()) return;
		if (h.Text == nullptr)
		{
			h.Text = cs::New<GUIStyle>();
			h.Text->fontSize = 12;
			h.Text->alignment = TextAnchor::MiddleRight;
			h.Text->clipping = TextClipping::Clip;
		}
		double wall = h.FrameClock; // US-T423: the cooldown's clock (was the wall clock)
		cs::String line;
		Color tint = UI::Hud::Dim;
		Rect r(Screen::width - 520.0f, 44, 500, 20);
		if (h.Active)
		{
			double back = h.PressT - h.Tau;
			line = cs::Cat("<< REWIND  -", cs::Fmt(back, "F1"), " s  (x", cs::Fmt(h.Rate, "F1"), ")");
			if (h.Tau <= h.Oldest + 1e-6)
				line = cs::Cat(line, h.Oldest <= h.FloorT + 1e-6 ? "  (back to the last rewind: no further)" : "  (as far back as it goes)");
			if (h.BarrierT > h.Tau) line = cs::Cat(line, "  -  plays on no earlier than -", cs::Fmt(h.PressT - h.BarrierT, "F1"), " s: ", h.BarrierWhy);
			line = cs::Cat(line, coop ? "  -  every player holds: anyone lets go to play on" : "  -  let go to play on");
			tint = Color(1.0f, 0.78f, 0.3f, 1.0f);
			r = Rect((Screen::width - 900.0f) * 0.5f, 132, 900, 24);
			h.Text->alignment = TextAnchor::MiddleCenter;
			h.Text->fontSize = 15;
		}
		else
		{
			h.Text->alignment = TextAnchor::MiddleRight;
			h.Text->fontSize = 12;
			if (wall < h.CooldownUntil) line = cs::Cat("REWIND in ", static_cast<int>(std::ceil(h.CooldownUntil - wall)), " s");
			else line = cs::Cat("REWIND ready  (hold ", Input::KeyBindings::Keys(Input::KeyAction::HoldRewind), ")");
			// US-T423: in co-op some players hold the key and others not yet: who it is waiting for.
			if (!waiting.empty())
			{
				line = wall < h.CooldownUntil ? cs::Cat(waiting, "  -  ready in ", static_cast<int>(std::ceil(h.CooldownUntil - wall)), " s") : waiting;
				tint = Color(1.0f, 0.78f, 0.3f, 1.0f);
				r = Rect((Screen::width - 1000.0f) * 0.5f, 132, 1000, 24);
				h.Text->alignment = TextAnchor::MiddleCenter;
				h.Text->fontSize = 15;
			}
			// US-T417: the hull flown lost or taken, its fate held back: how long is left to undo it, centred and loud.
			else if (h.OwnFateHeld != 0)
			{
				double left = std::max(0.0, HrTuning::WindowS - (h.LivedS - h.OwnFateSince));
				cs::String what = h.OwnFateHeld == 2 ? "YOUR HULL IS TAKEN" : "YOUR HULL IS LOST";
				cs::String how = wall < h.CooldownUntil
					? cs::Cat("rewind ready in ", static_cast<int>(std::ceil(h.CooldownUntil - wall)), " s")
					: cs::Cat("hold ", Input::KeyBindings::Keys(Input::KeyAction::HoldRewind), " to undo it");
				line = cs::Cat(what, "  -  ", how, "  -  final in ", static_cast<int>(std::ceil(left)), " s");
				tint = Color(1.0f, 0.35f, 0.3f, 1.0f);
				r = Rect((Screen::width - 900.0f) * 0.5f, 132, 900, 24);
				h.Text->alignment = TextAnchor::MiddleCenter;
				h.Text->fontSize = 15;
			}
			// US-T419: the fight would be over, its end held back: the outcome, and how long is left to undo it.
			else if (h.Hooked != nullptr && h.Hooked->EndHeld && h.EndHeldSince != HrNone)
			{
				double left = std::max(0.0, HrTuning::WindowS - (h.LivedS - h.EndHeldSince));
				auto bodyShip = HrBodyShip(HeldSeat());
				auto me = bodyShip != nullptr ? h.Hooked->UnitOf(bodyShip) : nullptr;
				int winner = h.Hooked->Winner;
				cs::String what = winner < 0 ? "BOTH SIDES BREAK OFF" : me == nullptr ? "THE FIGHT IS DECIDED" : winner == me->Side ? "VICTORY" : "DEFEAT";
				cs::String how = wall < h.CooldownUntil
					? cs::Cat("rewind ready in ", static_cast<int>(std::ceil(h.CooldownUntil - wall)), " s")
					: cs::Cat("hold ", Input::KeyBindings::Keys(Input::KeyAction::HoldRewind), " to undo it");
				line = cs::Cat(what, "  -  ", how, "  -  final in ", static_cast<int>(std::ceil(left)), " s");
				tint = me != nullptr && winner == me->Side ? Color(0.55f, 1.0f, 0.62f, 1.0f) : Color(0.95f, 0.85f, 0.5f, 1.0f);
				r = Rect((Screen::width - 900.0f) * 0.5f, 132, 900, 24);
				h.Text->alignment = TextAnchor::MiddleCenter;
				h.Text->fontSize = 15;
			}
		}
		h.Text->normal.textColor = tint;
		GUI::Label(r, line, h.Text);
	}

	// ------------------------------------------------------------------------------------------- the self-test

	void UnitySpaceApp::BeginHoldRewindSelfTest(const cs::Ref<Battle>& fight)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto& t = holdRewind->Test;
		t = HrSelfTest();
		t.On = true;
		t.Fight = fight;
		t.PhaseWall = Time::realtimeSinceStartupAsDouble;
		holdRewind->CooldownUntil = 0;
		UnityCompat::Debug::Log("HoldRewind selftest: started");
	}

	bool UnitySpaceApp::BeginHoldRewindBirthSelfTest()
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto uranus = (*Catalog())["Uranus"];
		if (uranus == nullptr || World()->Sol == nullptr) return false;
		// Held out where a fight there is laid out, as the main self-test's skirmish is.
		auto dir = DVec3(1, 0.15, 0.4).Normalized();
		auto ship = Fleet()->SpawnAt("Rewind Trial", ShipClass::Cruiser, World()->Sol, uranus->Index, dir * Battle::AnchorDistanceKm(uranus), -dir, Clock->Jd);
		if (ship == nullptr) return false;
		if (IsPiloting()) ExitPiloting();
		EnterPiloting(Target::OfShip(ship));
		auto& t = holdRewind->Test;
		t = HrSelfTest();
		t.On = true;
		t.Birth = true;
		t.SeatShip = ship;
		t.Phase = 90; // US-T428: the crossing check first, then the birth
		t.PhaseWall = Time::realtimeSinceStartupAsDouble;
		holdRewind->CooldownUntil = 0;
		UnityCompat::Debug::Log("HoldRewind selftest: started (a fight born in the window)");
		return IsPiloting();
	}

	bool UnitySpaceApp::HoldRewindSelfTestPassed() const { return holdRewind != nullptr && holdRewind->Test.Done && holdRewind->Test.Passed; }

	bool UnitySpaceApp::HoldRewindSelfTestWantsKey() const { return holdRewind != nullptr && holdRewind->Test.On && holdRewind->Test.ForceDown; }

	void UnitySpaceApp::SetHoldRewindSelfTestRealKeys(bool real)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		holdRewind->Test.RealKeys = real;
	}

	bool UnitySpaceApp::TickHoldRewindSelfTest()
	{
		if (holdRewind == nullptr) return false;
		auto& h = *holdRewind;
		auto& t = h.Test;
		if (!t.On) return false;
		double wall = Time::realtimeSinceStartupAsDouble;
		auto selfCheck = [&t](bool ok, const cs::String& what)
		{
			t.Checks++;
			if (!ok) t.Failed++;
			UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: ", ok ? "PASS  " : "FAIL  ", what));
		};
		auto finish = [&t](const cs::String& why)
		{
			if (!why.empty())
			{
				t.Checks++;
				t.Failed++;
				UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: FAIL  ", why));
			}
			t.ForceDown = false;
			t.Target = HrNone;
			t.On = false;
			t.Done = true;
			t.Passed = t.Failed == 0;
			UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: ", t.Passed ? "PASS" : "FAIL", " (", t.Checks, " checks, ", t.Failed, " failed)"));
			return false;
		};
		if (!rewindEnabled) return finish("rewind is off in this run (-capture, -demo or -norewind): run it without them");
		if (Coop::CoopPeer::Current() != nullptr) return finish("rewind is single-player: run it outside co-op");
		if (wall - t.PhaseWall > 60) return finish(cs::Cat("phase ", t.Phase, " timed out"));
		// US-T419: the screen as drawn (the game's views and the readout), saved for a look: Saved/Screenshots/holdrewind-*.png.
		auto shot = [](const char* name)
		{
			FString path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), FString::Printf(TEXT("holdrewind-%hs.png"), name));
			FScreenshotRequest::RequestScreenshot(path, true, false);
			UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: screenshot ", name));
		};
		// US-T424: the birth check, phases 100 to 104.
		if (t.Birth)
		{
			auto ship = t.SeatShip;
			if (ship == nullptr || ship->Destroyed) return finish("the hull flown for the birth check was lost");
			auto start = [this, ship]() { return World()->War->TryStartPlayerBattle(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd); };
			switch (t.Phase)
			{
				// US-T428: crossing into another body's space undone: the helm put into a moon's space (the cruise frame's own
				// rebase), then a hold back to before puts it back in the planet's frame where it was.
				case 90:
				{
					if (!IsPiloting() || World()->PilotedShip != ship) return finish("not at the helm for the crossing check");
					if (h.Pilot.empty() || h.LivedS - h.Pilot.front().T < 2.0) return true;
					if (t.CrossMarkT == HrNone)
					{
						t.CrossMarkT = h.LivedS;
						t.CrossFrom = pilotFrameBody;
						t.CrossOffset = pilotOffsetKm();
						t.PhaseWall = wall;
						return true;
					}
					if (wall - t.PhaseWall < 0.7) return true;
					int moon = -1;
					for (const auto& b : Catalog()->Bodies())
						if (b->ParentIndex == pilotFrameBody && b->RadiusKm > 100)
						{
							moon = b->Index;
							break;
						}
					if (moon < 0) return finish("no moon to cross into for the crossing check");
					auto m = (*Catalog())[moon];
					SetpilotOffsetKm(Ephemeris()->Helio(moon) - Ephemeris()->Helio(pilotFrameBody) + DVec3(1, 0.2, 0.1).Normalized() * (m->RadiusKm * 4));
					RebaseCruiseFrame();
					selfCheck(pilotFrameBody == moon, cs::Cat("the helm crossed into ", m->Name, "'s space"));
					t.PhaseWall = wall;
					t.Phase = 91;
					return true;
				}
				case 91: // In the moon's space a second: hold back to the mark.
				{
					if (wall - t.PhaseWall < 1.0) return true;
					t.Target = t.CrossMarkT;
					t.ForceDown = true;
					t.SettleFrames = 0;
					t.PhaseWall = wall;
					t.Phase = 92;
					return true;
				}
				case 92:
				{
					if (!h.Active) return true;
					if (h.Tau > std::max(t.CrossMarkT, h.Oldest) + 1e-6) return true;
					if (++t.SettleFrames < 3) return true;
					t.ForceDown = false;
					t.HaveResume = false;
					t.PhaseWall = wall;
					t.Phase = 93;
					return true;
				}
				case 93: // Played on from before: in the planet's space again, where it was.
				{
					if (!t.HaveResume) return true;
					selfCheck(t.ResumedAnchorT <= t.CrossMarkT + HrTuning::AnchorS + 1e-6 && h.BarrierT < t.CrossMarkT,
						"crossing into another body's space does not block a resume to before it");
					selfCheck(pilotFrameBody == t.CrossFrom && helmFlight != nullptr && helmFlight->FrameBody == t.CrossFrom,
						cs::Cat("and the helm is back in ", (*Catalog())[t.CrossFrom]->Name, "'s frame"));
					double off = (pilotOffsetKm() - t.CrossOffset).Length();
					selfCheck(off < 5.0, cs::Cat("where it was before the crossing (", cs::Fmt(off, "F3"), " km off)"));
					t.PhaseWall = wall;
					t.Phase = 100;
					return true;
				}
				case 100: // At the helm, no fight: fly 3 s, mark, then bring the enemy in and the fight begins.
				{
					if (!IsPiloting() || World()->PilotedShip != ship) return finish("not at the helm for the birth check");
					if (h.Fight != nullptr) return finish(cs::Cat("a fight was already in view before the birth check: ", h.Fight->Name));
					if (!HoldRewindRefusal().empty()) return true; // US-T428: the crossing check's cooldown
					if (h.Pilot.empty() || h.LivedS - h.Pilot.front().T < 3.0) return true;
					if (t.BirthMarkT == HrNone)
					{
						t.BirthMarkT = h.LivedS;
						t.PhaseWall = wall;
						return true;
					}
					if (wall - t.PhaseWall < 0.7) return true;
					// A Reach squadron parked a few km off: hostile and armed, so the fight starts where the hull is.
					auto uranus = (*Catalog())["Uranus"];
					auto at = pilotOffsetKm();
					for (int i = 0; i < 3; i++)
						Fleet()->SpawnAt(cs::Cat("Rewind Trial Foe ", i + 1), i == 0 ? ShipClass::Cruiser : ShipClass::Destroyer, World()->Reach, uranus->Index,
							at + DVec3(20 + i * 3, 4, 0), DVec3(-1, 0, 0), Clock->Jd);
					auto fight = start();
					if (fight == nullptr) return finish("the fight for the birth check did not start");
					t.Fight = fight;
					t.BirthId = fight->Id;
					t.BirthName = fight->Name;
					t.PhaseWall = wall;
					t.Phase = 101;
					return true;
				}
				case 101: // Born and watched: fight 2 s, then hold back to the mark.
				{
					if (h.Fight != t.Fight) return true;
					if (h.LivedS - h.EnteredT < 2.0) return true;
					selfCheck(h.Entered == t.Fight && h.EnteredBorn, "the fight begun on the hull is seen being born in the window");
					selfCheck(h.EnteredT > t.BirthMarkT, "after the moment marked");
					selfCheck(ship->Battle == t.Fight && World()->War->Battles.Contains(t.Fight), "and the hull is in it");
					t.Target = t.BirthMarkT;
					t.ForceDown = true;
					t.SettleFrames = 0;
					t.PhaseWall = wall;
					t.Phase = 102;
					return true;
				}
				case 102:
				{
					if (!h.Active) return true;
					if (HoldRewindBeforeFight(t.Fight) && !t.SawUnborn)
					{
						t.SawUnborn = true;
						selfCheck(ship->Battle == nullptr && PilotUnit() == nullptr,
							"held before it began, the hull is in no fight for anything reading it (the HUD's fight panels too)");
						shot("5-before-the-fight-held");
						t.ShotWall = wall;
					}
					if (t.ShotWall >= 0 && wall - t.ShotWall < 0.4) return true;
					if (h.Tau > std::max(t.BirthMarkT, h.Oldest) + 1e-6) return true;
					if (++t.SettleFrames < 3) return true;
					t.ShotWall = -1;
					t.ForceDown = false;
					t.HaveResume = false;
					t.PhaseWall = wall;
					t.Phase = 103;
					return true;
				}
				case 103: // Played on from before it began: no fight, the hull free at its helm.
				{
					if (!t.HaveResume) return true;
					selfCheck(t.SawUnborn, "the playback drew the moment before the fight began (its ships out of it)");
					selfCheck(t.ResumedAnchorT <= t.BirthMarkT + HrTuning::AnchorS + 1e-6, "a fight born in the window does not block a resume to before it");
					selfCheck(!World()->War->Battles.Contains(t.Fight), "the fight is taken back: the war no longer holds it");
					selfCheck(ship->Battle == nullptr && IsPiloting() && World()->PilotedShip == ship, "the hull is out of it, still at its helm");
					bool shipsFree = true;
					for (const auto& u : t.Fight->Units)
						if (u != nullptr && u->Ship != nullptr && u->Ship->Battle == t.Fight) shipsFree = false;
					selfCheck(shipsFree, "and every ship that was in it is back on its own schedule");
					int lines = 0;
					for (const auto& e : World()->Events)
						if (e.Jd >= t.Fight->StartJd - 1e-9 && e.Text.find(t.BirthName) != cs::String::npos) lines++;
					selfCheck(lines == 0, "and the world's log no longer tells of it");
					selfCheck(h.Fight == nullptr && HoldRewindBattle() == nullptr, "and the rewind watches no fight");
					shot("6-before-the-fight");
					t.PhaseWall = wall;
					t.Phase = 104;
					return true;
				}
				case 104: // The same hull and squadron fight again: the fight it starts is the same one, by number and name.
				{
					if (wall - t.PhaseWall < 1.0) return true;
					auto again = ship->Battle != nullptr ? ship->Battle : start();
					if (again == nullptr) return finish("the fight did not begin again after it was taken back");
					selfCheck(again->Id == t.BirthId && again->Name == t.BirthName, cs::Cat("begun again, it is the same fight by number and name (",
						again->Name, " #", again->Id, ", was ", t.BirthName, " #", t.BirthId, ")"));
					return finish(cs::String());
				}
				default:
					return finish("lost its place");
			}
		}
		// (The last phase lets the fight end, once its end has waited out the window.)
		if (t.Fight == nullptr || (t.Fight->Over && !(t.Phase == 17 && t.KilledAgainWall >= 0))) return finish("the fight ended before the test did");
		// US-T419: every unit on the other side struck down by a real killing impact (the fight's own kill path).
		auto killEnemies = [&t]()
		{
			std::vector<cs::Ref<BattleUnit>> foes;
			for (const auto& u : t.Fight->Units)
				if (u != nullptr && u->Present() && !u->Dead && u->Side != t.SeatUnit->Side) foes.push_back(u);
			for (const auto& u : foes) t.Fight->ApplyImpact(u, u->Pos, 1e15);
			return foes.size();
		};
		auto enemiesStanding = [&t]()
		{
			int n = 0;
			for (const auto& u : t.Fight->Units)
				if (u != nullptr && u->Present() && !u->Dead && u->Side != t.SeatUnit->Side) n++;
			return n;
		};
		switch (t.Phase)
		{
			case 0: // Settle: the fight recorded a few seconds, nothing held.
			{
				if (h.Fight != t.Fight || h.Active || h.LivedS - h.FightSince < 3.0) return true;
				t.M = -1;
				t.Phase = 1;
				t.PhaseWall = wall;
				return true;
			}
			case 1: // Mark: RecordHoldRewind sets M and its digest at the next resume point.
			{
				if (t.M < 0) return true;
				t.Phase = 2;
				t.PhaseWall = wall;
				return true;
			}
			case 2: // Play on 3 s, then press with the stop at M.
			{
				if (h.LivedS - t.M < 3.0) return true;
				if (h.BarrierT > t.M || h.FloorT > t.M)
				{
					// The fight changed in a way not undone (a craft launched or landed): mark again.
					if (++t.Tries > 20) return finish("the fight never held still for 3 s without a barrier");
					t.M = -1;
					t.Phase = 1;
					t.PhaseWall = wall;
					return true;
				}
				t.DiedSinceM.clear();
				for (const auto& u : t.Fight->Units)
					if (u != nullptr && u->Dead) t.DiedSinceM.push_back(u);
				t.FxSeqAtPress = t.Fight->FxSeq;
				t.Target = t.M;
				t.ForceDown = true;
				t.LastTau = std::numeric_limits<double>::max();
				t.TauMonotonic = true;
				t.SettleFrames = 0;
				t.Phase = 3;
				t.PhaseWall = wall;
				return true;
			}
			case 3: // Rewinding: the moment reached only goes back, and stops at M.
			{
				if (!h.Active) return true;
				if (h.Tau > t.LastTau + 1e-9) t.TauMonotonic = false;
				t.LastTau = h.Tau;
				if (h.Tau > t.M + 1e-9) return true;
				if (++t.SettleFrames < 3) return true;
				selfCheck(t.TauMonotonic, "the moment reached only went back while held");
				selfCheck(h.Tau >= h.Oldest - 1e-9, "the hold never went past the oldest moment");
				if (t.RealKeys && !t.KeyChecked)
				{
					t.KeyChecked = true;
					selfCheck(Input.HoldRewind, "the hold is Shift+R held down through Unreal's input (the player's own key path)");
				}
				// US-T421: the effects' clock is back at the moment reached (explosions there played backward to it).
				selfCheck(std::fabs(HoldRewindFxClock(Time::unscaledTime) - (h.PressFx - static_cast<float>(h.PressT - h.Tau))) < 1e-3f
					&& HoldRewindFxClock(Time::unscaledTime) < Time::unscaledTime - 0.5f, "the effects' clock runs back with the hold");
				shot("1-held");
				// US-T418: a ship gone through a gate after the moment reached is drawn again, one that came after it
				// is hidden, and one that came or went before it is drawn as now (fleet index 0's notes, put back after).
				selfCheck(!h.ShipGoneAt.empty(), "the fleet's comings and goings are noted");
				if (!h.ShipGoneAt.empty())
				{
					double gone = h.ShipGoneAt[0], came = h.ShipCameAt[0];
					h.ShipGoneAt[0] = h.Tau + 1; h.ShipCameAt[0] = HrNone;
					bool backAgain = HoldRewindShipShown(0, false);
					h.ShipGoneAt[0] = HrNone; h.ShipCameAt[0] = h.Tau + 1;
					bool notYet = !HoldRewindShipShown(0, true);
					h.ShipGoneAt[0] = h.Tau - 1; h.ShipCameAt[0] = h.Tau - 2;
					bool asNow = !HoldRewindShipShown(0, false) && HoldRewindShipShown(0, true);
					h.ShipGoneAt[0] = gone; h.ShipCameAt[0] = came;
					selfCheck(backAgain && notYet && asNow, "a hold draws ships gone in the window again and hides ones that came in it");
				}
				t.HaveResume = false;
				t.ForceDown = false;
				t.Phase = 4;
				t.PhaseWall = wall;
				return true;
			}
			case 4: // Let go: the resume point is M, and the fight is exactly as it was then.
			{
				if (!t.HaveResume) return true;
				selfCheck(std::fabs(t.ResumedAnchorT - t.M) < 1e-9, cs::Cat("resumed at the marked moment (", cs::Fmt(t.ResumedAnchorT, "F3"), " vs ", cs::Fmt(t.M, "F3"), ")"));
				selfCheck(t.DR == t.DM, cs::Cat("the fight resumed is the fight as it was (digest ", cs::Fmt(static_cast<double>(t.DR % 1000000007ull), "F0"), " vs ",
					cs::Fmt(static_cast<double>(t.DM % 1000000007ull), "F0"), ")"));
				int revived = 0, booked = 0;
				for (const auto& u : t.DiedSinceM)
				{
					if (u->Dead) continue; // dead at M too
					revived++;
					if (u->Ship != nullptr && u->Ship->Destroyed) booked++;
				}
				selfCheck(booked == 0, cs::Cat(revived, " hull(s) lost after the mark are back and none is struck off the world's books"));
				selfCheck(t.Fight->FxSeq >= t.FxSeqAtPress, "the effects go on from now (no effect shown twice)");
				selfCheck(std::fabs(h.FloorT - t.FloorAtResume) < 1e-9, "the resume point is the floor for the next hold");
				cs::String refusal = HoldRewindRefusal();
				selfCheck(cs::Strings::StartsWith(refusal, "REWIND ready in"), cs::Cat("a second hold is refused at once: ", refusal));
				t.Floor = h.FloorT;
				// Stop the clock for 2 s of the cooldown: the wall goes on, the lived time does not, so the floor (not the
				// window) is what the next hold meets.
				Clock->SetPaused(true);
				t.Phase = 5;
				t.PhaseWall = wall;
				return true;
			}
			case 5: // The cooldown: refused until it ends, free after.
			{
				if (wall - t.PhaseWall >= 2.0 && Clock->Paused()) Clock->SetPaused(false);
				// US-T423: the cooldown runs on the hold's frame clock (the phases' own timing stays on the wall clock).
				if (h.FrameClock < h.CooldownUntil - 0.5) return true;
				if (h.FrameClock < h.CooldownUntil)
				{
					if (t.SettleFrames >= 0)
					{
						selfCheck(!HoldRewindRefusal().empty(), "still refused just before the cooldown ends");
						t.SettleFrames = -1;
					}
					return true;
				}
				if (h.FrameClock < h.CooldownUntil + 0.2) return true;
				cs::String refusal = HoldRewindRefusal();
				selfCheck(refusal.empty(), cs::Cat("free again once the cooldown is over", refusal.empty() ? cs::String() : cs::Cat(": ", refusal)));
				selfCheck(std::fabs(HrTuning::CooldownS - 10) < 1e-9 && std::fabs(HrTuning::WindowS - 10) < 1e-9, "a 10 s window and a 10 s cooldown");
				t.Target = HrNone;
				t.ForceDown = true;
				t.SettleFrames = 0;
				t.Phase = 6;
				t.PhaseWall = wall;
				return true;
			}
			case 6: // The second hold: never back past the floor (the first rewind's resume point).
			{
				if (!h.Active) return true;
				if (h.Tau > h.Oldest + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				selfCheck(h.Oldest >= t.Floor - 1e-9, cs::Cat("the second hold stops at the floor or later (", cs::Fmt(h.PressT - h.Oldest, "F2"), " s back)"));
				selfCheck(h.Tau >= t.Floor - 1e-9, "never into a time already rewound through");
				if (h.LivedS - HrTuning::WindowS < t.Floor) selfCheck(std::fabs(h.Oldest - t.Floor) < 1e-6, "the floor, not the window, is the limit here");
				t.HaveResume = false;
				t.ForceDown = false;
				t.Phase = 7;
				t.PhaseWall = wall;
				return true;
			}
			case 7: // The second resume is at the floor or after; then the ease, checked in full.
			{
				if (!t.HaveResume) return true;
				selfCheck(t.ResumedAnchorT >= t.Floor - 1e-9, "the second resume is no earlier than the first");
				double last = -1;
				bool monotonic = true, capped = true;
				for (int i = 0; i <= 400; i++)
				{
					double r = HoldRewindCurve::Rate(i * 0.01, HrTuning::StartRate, HrTuning::TopRate, HrTuning::RampS);
					if (r < last - 1e-12) monotonic = false;
					if (r > HrTuning::TopRate + 1e-12) capped = false;
					last = r;
				}
				selfCheck(monotonic && capped && std::fabs(last - HrTuning::TopRate) < 1e-9, "the playback eases up and holds its top rate");
				t.Phase = 8;
				t.PhaseWall = wall;
				return true;
			}
			// US-T414: seat change, checked for real (the designer's own words: "could not answer without running
			// it"). Takes a hull's helm, records a moment, switches to one of its turrets (the same hull, a
			// different seat), holds back to before the switch and lets go: the hull is the same throughout, so
			// HrWhyChanged no longer treats this as a barrier (Ship is now whichever hull HeldSeat() says the
			// player is aboard, not only the one piloted), and the resume should reach the marked moment.
			case 8: // Part 1: take the helm of a turret-capable hull in the fight.
			{
				// The second resume (phase 7) started a cooldown: the hold in phase 10 would be refused inside it, so the
				// mark and the switch are made once it is over, keeping them well inside the window.
				if (t.SeatShip == nullptr && !HoldRewindRefusal().empty()) return true;
				if (t.SeatShip == nullptr)
				{
					for (const auto& u : t.Fight->Units)
					{
						if (u == nullptr || u->Kind != UnitKind::Ship || u->Ship == nullptr || !u->Present() || u->Side != 0) continue;
						if (Embodiables::TurretsOn(World(), u->Ship).Count() == 0) continue;
						t.SeatShip = u->Ship;
						break;
					}
					if (t.SeatShip == nullptr)
					{
						UnityCompat::Debug::Log("HoldRewind selftest: no turret-capable hull in the fight; the seat-change check is skipped");
						return finish(cs::String());
					}
					// Embodiables::MayEmbody refuses PickSeat on a hull already in battle ("no hijacking a hull
					// mid-fight", Embodiables.cpp ~214-219) -- the right UI rule for a real player, but not what
					// this check is about, so it calls EnterPiloting directly, the same primitive TakeSeat's own
					// helm branch uses once that gate has passed (UnitySpaceApp.Embodiment.cpp ~128-135).
					EnterPiloting(Target::OfShip(t.SeatShip));
					if (!IsPiloting() || World()->PilotedShip != t.SeatShip) return finish("could not take the helm for the seat-change check");
					t.PhaseWall = wall;
					return true;
				}
				if (wall - t.PhaseWall < 1.0) return true; // record a moment actually piloting it
				t.SeatMarkT = h.LivedS; // piloting, before the switch: where the resume should be able to reach
				t.PhaseWall = wall;
				t.Phase = 9;
				return true;
			}
			case 9: // Part 2: switch to a turret on the identical hull.
			{
				auto turrets = Embodiables::TurretsOn(World(), t.SeatShip);
				if (turrets.Count() == 0) return finish("the hull's turrets went away mid-check");
				cs::Ref<IEmbodiable> pick = turrets[0];
				bool canEmbody = pick->CanEmbody(), passesGate = PassesEmbodyGate(pick);
				// US-T414 diagnosis: what the session itself says when it refuses the take.
				auto refusals = std::make_shared<cs::String>();
				int token = session->EventRaised() += [refusals](cs::Ref<IGameEvent> e)
				{
					if (auto c = cs::As<CommandRefused>(e)) *refusals = cs::Cat(*refusals, refusals->empty() ? "" : " | ", c->Reason);
				};
				PickSeat(pick);
				session->EventRaised() -= token;
				if (!refusals->empty()) UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: seat change: the session refused: ", *refusals));
				if (IsPiloting()) return finish("still at the helm after picking a turret");
				auto seat = HeldSeat();
				if (seat == nullptr || seat->Subject().Ship != t.SeatShip)
				{
					// US-T414: this used to fail for every player, not just here -- TakeSeat let go of the helm before
					// the session weighed the take, so a gun on the hull flown looked like a hull in battle with no
					// hold on it. Fixed in TakeSeat; a failure here now means that has come back.
					UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: seat change: CanEmbody=", (canEmbody ? "true" : "false"),
						" PassesEmbodyGate=", (passesGate ? "true" : "false"), " held now = ", seat != nullptr ? seat->Name() : cs::String("null")));
					return finish("the turret picked is not on the marked hull (US-T414 again?)");
				}
				t.PhaseWall = wall;
				t.Phase = 10;
				return true;
			}
			case 10: // Part 3: record a moment at the turret, then hold back towards the mark (before the switch).
			{
				if (wall - t.PhaseWall < 1.0) return true;
				t.Target = t.SeatMarkT;
				t.ForceDown = true;
				t.LastTau = std::numeric_limits<double>::max();
				t.SettleFrames = 0;
				t.PhaseWall = wall;
				t.Phase = 11;
				return true;
			}
			case 11: // Held as far back as it goes (the mark, unless something else still blocks it).
			{
				if (!h.Active) return true;
				// The hold is aimed at the mark (t.Target), so it parks there, or at the oldest it may reach if that is
				// later (a barrier still standing): whichever is later is where it settles, as phase 3 checks against M.
				if (h.Tau > std::max(t.SeatMarkT, h.Oldest) + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				t.ForceDown = false;
				t.HaveResume = false;
				t.Phase = 12;
				t.PhaseWall = wall;
				return true;
			}
			case 12: // Let go: did it reach the mark, and is everything it touched still sane?
			{
				if (!t.HaveResume) return true;
				// US-T417: strictly at or before the switch (the mark was taken just before it): the seat change is undone.
				bool reachedMark = t.ResumedAnchorT <= t.SeatMarkT + 1e-6;
				UnityCompat::Debug::Log(cs::Cat("HoldRewind selftest: seat change: resumed at ", cs::Fmt(t.ResumedAnchorT, "F3"),
					" (marked ", cs::Fmt(t.SeatMarkT, "F3"), ", switched just after)"));
				selfCheck(reachedMark, "a same-hull seat change (helm to a turret) does not block the resume to before it");
				cs::Ref<BattleUnit> hullUnit;
				for (const auto& u : t.Fight->Units)
					if (u != nullptr && u->Ship == t.SeatShip) { hullUnit = u; break; }
				bool poseSane = hullUnit != nullptr &&
					std::isfinite(hullUnit->Pos.X) && std::isfinite(hullUnit->Pos.Y) && std::isfinite(hullUnit->Pos.Z) &&
					std::isfinite(hullUnit->Vel.X) && std::isfinite(hullUnit->Vel.Y) && std::isfinite(hullUnit->Vel.Z);
				selfCheck(poseSane, "the hull's own pose is still finite after resuming across the seat change");
				selfCheck(std::isfinite(pilotAim.X) && std::isfinite(pilotAim.Y) && std::isfinite(pilotAim.Z), "the player's own aim is still finite");
				// US-T417: the seat comes back with the fight: at the mark the player was at the helm, so they are again.
				selfCheck(IsPiloting() && World()->PilotedShip == t.SeatShip && HeldNonHelm() == nullptr,
					"the seat is put back with the fight: back at the helm, as at the mark, not left at the turret");
				selfCheck(hullUnit != nullptr && t.Fight->Manned.Count() == 0, "and the turret is the crew's again (nothing manned)");
				t.SeatUnit = hullUnit;
				t.PhaseWall = wall;
				t.Phase = 13;
				return true;
			}
			// US-T417: the player's own hull lost, undone. A real impact destroys the hull flown; its loss must be held back
			// (the hull still on the world's books, the helm held but dead), a hold to before it brings it back alive and
			// still the player's, and a second loss left alone is final exactly as before.
			case 13: // Out of the cooldown, still at the helm: mark a moment, then a killing impact on the hull flown.
			{
				if (!HoldRewindRefusal().empty()) return true;
				if (t.SeatUnit == nullptr || !IsPiloting() || World()->PilotedShip != t.SeatShip) return finish("not at the helm for the own-hull check");
				if (t.DeathMarkT == HrNone)
				{
					t.DeathMarkT = h.LivedS;
					t.PhaseWall = wall;
					return true;
				}
				if (wall - t.PhaseWall < 0.6) return true; // a resume point between the mark and the impact, at most AnchorS old
				t.Fight->ApplyImpact(t.SeatUnit, t.SeatUnit->Pos, 1e15);
				t.PhaseWall = wall;
				t.Phase = 14;
				return true;
			}
			case 14: // Lost, and held back.
			{
				if (wall - t.PhaseWall < 0.5) return true;
				// The readout as drawn, before the hold takes the screen (the shot is taken at the end of a later frame).
				if (t.ShotWall < 0)
				{
					shot("2-hull-lost");
					t.ShotWall = wall;
					return true;
				}
				if (wall - t.ShotWall < 0.4) return true;
				t.ShotWall = -1;
				selfCheck(t.SeatUnit->Dead, "a killing impact kills the hull flown in the fight");
				selfCheck(!t.SeatShip->Destroyed, "its loss is held back: the hull is still on the world's books");
				selfCheck(IsPiloting() && World()->PilotedShip == t.SeatShip, "the helm is still held (dead in the hands), not let go");
				selfCheck(h.OwnFateSince != HrNone, "the readout says the hull is lost and there is still time to undo it");
				t.Target = t.DeathMarkT;
				t.ForceDown = true;
				t.SettleFrames = 0;
				t.PhaseWall = wall;
				t.Phase = 15;
				return true;
			}
			case 15: // Hold back to before the impact.
			{
				if (!h.Active) return true;
				if (h.Tau > std::max(t.DeathMarkT, h.Oldest) + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				// US-T426: still held, before the impact: the hull is drawn whole again (its damage played back), not wrecked.
				selfCheck(t.SeatUnit->Combat != nullptr && t.SeatUnit->Combat->Hull > 0 && !t.SeatUnit->Dead,
					"held back before the impact, the hull's damage is played back too: whole again while still held");
				t.ForceDown = false;
				t.HaveResume = false;
				t.PhaseWall = wall;
				t.Phase = 16;
				return true;
			}
			case 16: // Played on from before it: alive, unbooked, still the player's.
			{
				if (!t.HaveResume) return true;
				selfCheck(t.ResumedAnchorT <= t.DeathMarkT + HrTuning::AnchorS + 1e-6 && h.BarrierT < t.DeathMarkT,
					"your own hull's loss does not block a resume to before it");
				selfCheck(!t.SeatUnit->Dead && t.SeatUnit->Combat != nullptr && t.SeatUnit->Combat->Hull > 0, "your hull is alive again after the hold");
				selfCheck(!t.SeatShip->Destroyed, "and its loss was never booked");
				selfCheck(IsPiloting() && World()->PilotedShip == t.SeatShip, "and you are still at its helm");
				t.PhaseWall = wall;
				t.Phase = 18;
				return true;
			}
			// US-T419: the fight's end undone. Every enemy struck down: the fight would be won, its end is held back (a
			// mop-up, the winner known), and a hold to before the kills plays on with the enemies alive and the fight on.
			case 18: // Out of the cooldown, at the helm: mark a moment, then strike down every enemy.
			{
				if (!HoldRewindRefusal().empty()) return true;
				if (!IsPiloting() || World()->PilotedShip != t.SeatShip) return finish("not at the helm for the fight's-end check");
				if (t.EndMarkT == HrNone)
				{
					t.EndMarkT = h.LivedS;
					t.PhaseWall = wall;
					return true;
				}
				if (wall - t.PhaseWall < 0.6) return true;
				if (killEnemies() == 0) return finish("no enemy left to strike down for the fight's-end check");
				t.PhaseWall = wall;
				t.Phase = 19;
				return true;
			}
			case 19: // Would be over: held open, the winner known.
			{
				if (!t.Fight->EndHeld && wall - t.PhaseWall < 3) return true;
				if (t.Fight->EndHeld && t.ShotWall < 0)
				{
					shot("3-victory-held");
					t.ShotWall = wall;
					return true;
				}
				if (t.ShotWall >= 0 && wall - t.ShotWall < 0.4) return true;
				t.ShotWall = -1;
				selfCheck(t.Fight->EndHeld && !t.Fight->Over, "a won fight's end is held back: still open (a mop-up), not settled");
				selfCheck(t.Fight->Winner == t.SeatUnit->Side, "and its winner is known: your side");
				selfCheck(h.EndHeldSince != HrNone, "and the readout says there is still time to undo it");
				selfCheck(HoldRewindBattle() == t.Fight, "and the rewind still keeps the fight");
				t.Target = t.EndMarkT;
				t.ForceDown = true;
				t.SettleFrames = 0;
				t.PhaseWall = wall;
				t.Phase = 20;
				return true;
			}
			case 20: // Hold back to before the kills.
			{
				if (!h.Active) return true;
				if (h.Tau > std::max(t.EndMarkT, h.Oldest) + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				t.ForceDown = false;
				t.HaveResume = false;
				t.PhaseWall = wall;
				t.Phase = 21;
				return true;
			}
			case 21: // Played on from before them: the enemies are back and the fight goes on.
			{
				if (!t.HaveResume) return true;
				selfCheck(t.ResumedAnchorT <= t.EndMarkT + HrTuning::AnchorS + 1e-6 && h.BarrierT < t.EndMarkT,
					"the fight's end does not block a resume to before it");
				selfCheck(enemiesStanding() > 0, "the enemies are back after the hold");
				selfCheck(!t.Fight->Over && !t.Fight->EndHeld, "and the fight is on again, its end undone");
				shot("4-fight-on-again");
				t.PhaseWall = wall;
				t.Phase = 22;
				return true;
			}
			// US-T422: leaving the hull (the helm let go, nothing held) is no barrier: a hold to before it puts the player back
			// at the helm, in the frame and pose the resume point had.
			case 22: // Out of the cooldown, at the helm: mark, then let go of the helm.
			{
				if (!HoldRewindRefusal().empty()) return true;
				if (t.LeaveMarkT == HrNone)
				{
					if (!IsPiloting() || World()->PilotedShip != t.SeatShip) return finish("not at the helm for the leave-the-hull check");
					t.LeaveMarkT = h.LivedS;
					t.PhaseWall = wall;
					return true;
				}
				if (wall - t.PhaseWall < 0.6) return true;
				ExitPiloting();
				t.PhaseWall = wall;
				t.Phase = 23;
				return true;
			}
			case 23: // Off the hull: hold back to before.
			{
				if (wall - t.PhaseWall < 0.6) return true;
				selfCheck(!IsPiloting() && HeldNonHelm() == nullptr, "the helm let go: the player holds nothing");
				selfCheck(h.Fight == t.Fight, "and the rewind still keeps the fight watched");
				t.Target = t.LeaveMarkT;
				t.ForceDown = true;
				t.SettleFrames = 0;
				t.PhaseWall = wall;
				t.Phase = 24;
				return true;
			}
			case 24:
			{
				if (!h.Active) return true;
				if (h.Tau > std::max(t.LeaveMarkT, h.Oldest) + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				t.ForceDown = false;
				t.HaveResume = false;
				t.PhaseWall = wall;
				t.Phase = 25;
				return true;
			}
			case 25: // Played on from before: back at the helm.
			{
				if (!t.HaveResume) return true;
				selfCheck(t.ResumedAnchorT <= t.LeaveMarkT + HrTuning::AnchorS + 1e-6 && h.BarrierT < t.LeaveMarkT,
					"leaving the hull does not block a resume to before it");
				selfCheck(IsPiloting() && World()->PilotedShip == t.SeatShip, "and the player is back at its helm");
				selfCheck(t.SeatShip->Battle != nullptr && pilotFrameBody == t.SeatShip->Battle->Body, "in the fight's frame");
				t.PhaseWall = wall;
				t.Phase = 26;
				return true;
			}
			// US-T424: flying clear of the fight is no barrier: the rewind keeps the fight left, and a hold to before puts the
			// hull back in it.
			case 26: // Out of the cooldown, at the helm: mark, then fly clear (the helm put well past the fight's edge).
			{
				if (!HoldRewindRefusal().empty()) return true;
				if (!IsPiloting() || World()->PilotedShip != t.SeatShip || t.SeatShip->Battle != t.Fight) return finish("not at the helm in the fight for the fly-clear check");
				if (t.FleeMarkT == HrNone)
				{
					t.FleeMarkT = h.LivedS;
					t.PhaseWall = wall;
					return true;
				}
				if (wall - t.PhaseWall < 0.6) return true;
				auto out = t.SeatUnit->Pos.LengthSquared() > 1e-9 ? t.SeatUnit->Pos.Normalized() : DVec3::UnitX;
				SeedPilot(t.Fight->Anchor + out * (Battle::EscapeKm * 2.0), DVec3::Zero, out);
				t.PhaseWall = wall;
				t.Phase = 27;
				return true;
			}
			case 27: // Clear of it: the hull is out, the rewind still keeps the fight; hold back to the mark.
			{
				if (t.SeatShip->Battle == t.Fight && wall - t.PhaseWall < 5) return true;
				selfCheck(t.SeatShip->Battle == nullptr && t.SeatUnit->Fled, "flown clear: the hull is out of the fight");
				selfCheck(h.Fight == t.Fight, "and the rewind still keeps the fight it left");
				t.Target = t.FleeMarkT;
				t.ForceDown = true;
				t.SettleFrames = 0;
				t.PhaseWall = wall;
				t.Phase = 28;
				return true;
			}
			case 28:
			{
				if (!h.Active) return true;
				if (h.Tau > std::max(t.FleeMarkT, h.Oldest) + 1e-6) return true;
				if (++t.SettleFrames < 3) return true;
				t.ForceDown = false;
				t.HaveResume = false;
				t.PhaseWall = wall;
				t.Phase = 29;
				return true;
			}
			case 29: // Played on from before: back in the fight, at its helm.
			{
				if (!t.HaveResume) return true;
				selfCheck(t.ResumedAnchorT <= t.FleeMarkT + HrTuning::AnchorS + 1e-6 && h.BarrierT < t.FleeMarkT,
					"flying clear of the fight does not block a resume to before it");
				selfCheck(t.SeatShip->Battle == t.Fight && !t.SeatUnit->Fled, "and the hull is back in the fight");
				selfCheck(IsPiloting() && World()->PilotedShip == t.SeatShip, "at its helm");
				t.PhaseWall = wall;
				t.Phase = 17;
				return true;
			}
			case 17: // Lost again inside the cooldown (no rewind to be had): after the window it is final, as before.
			{
				if (t.KilledAgainWall < 0)
				{
					if (!IsPiloting()) return finish("lost the helm before the second loss");
					// US-T419: every enemy struck down with it, so the fight would end too: left alone, it is settled
					// once the window is out, as before.
					killEnemies();
					t.Fight->ApplyImpact(t.SeatUnit, t.SeatUnit->Pos, 1e15);
					t.KilledAgainWall = wall;
					t.PhaseWall = wall;
					t.SettleFrames = 0;
					return true;
				}
				if (t.Fight->Over && t.OverWall < 0) t.OverWall = wall;
				// Timed by the test's own clock: once the loss is booked the readout's "held" moment is rightly gone.
				if ((!t.SeatShip->Destroyed || !t.Fight->Over) && wall - t.KilledAgainWall < HrTuning::WindowS + 3) return true;
				// Booked at the end of a frame; the helm lets go on the next frame's piloting, as for any loss.
				if (t.SeatShip->Destroyed && IsPiloting() && ++t.SettleFrames < 5) return true;
				selfCheck(t.SeatShip->Destroyed, "left alone past the window, the loss is booked: the hull is struck off");
				selfCheck(wall - t.KilledAgainWall >= HrTuning::WindowS - 0.5, "and not before the window was out (it was held back till then)");
				selfCheck(!IsPiloting(), "and the helm is let go, as a loss always was");
				selfCheck(t.Fight->Over, "left alone past the window, the fight's end is settled too");
				selfCheck(t.OverWall >= 0 && t.OverWall - t.KilledAgainWall >= HrTuning::WindowS - 0.5, "and not before the window was out");
				return finish(cs::String());
			}
			default:
				return finish("lost its place");
		}
	}
}
