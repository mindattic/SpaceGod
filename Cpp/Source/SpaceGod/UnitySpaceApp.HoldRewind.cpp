// US-T409 (docs/REWIND.md, "Hold to rewind"): the designer's short rewind. Members are declared in
// UnitySpaceApp.HoldRewind.inl.
//
// Two records are kept, both by a read-only observer at the end of each frame that ran the simulation (never in Core,
// never with a random draw, so AI-only runs and the Bench are what they were):
//   - tracks: each hull's, craft's, missile's, round's and decoy's pose in the fight in view (capitals at
//     HoldRewindTuning::CapitalHz, the rest at CraftHz, the hull held every frame) and the flown hull's helm state,
//     with whether each hull was dead, gone or docked; they are what a hold draws, played back smoothly (Hermite on
//     position and velocity, the nose lerped and normalised: docs/research/rewind-short-track.md);
//   - resume points: every HoldRewindTuning::AnchorS, the fight's whole state by value (the Battle, every unit, its
//     combat state, subsystems, reactor, fighter and broadside state, every missile, round, mine and decoy) and the
//     helm's flight state. Letting go puts the newest one at or before the moment reached back over the live objects
//     (what Sands of Time does for its actors), so play goes on from a real state, not from the drawn one.
// What the galaxy booked from the fight (a hull lost, taken, fled, a craft launched or landed, a seat changed) cannot be
// put back this way: each such change is a barrier, and a resume never goes back past the last one (the hold may still
// draw further back). The world's clock, and everything outside the fight, goes on from now.

#include "UnitySpaceApp.h"
#include "UI/Hud.h"
#include "Coop/CoopPeer.h"
#include "Input/KeyBindings.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/PilotFlight.h"
#include "Systems/HelmFtl.h"
#include "Camera/StarTravel.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		using HrTuning = UnitySpaceApp::HoldRewindTuning;

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

		struct HrClockMark
		{
			double T = 0;
			double S = 0;
		};

		struct HrPilotPose
		{
			double T = 0;
			PilotState State;
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
			bool HasPilot = false;
			PilotState Pilot;
			cs::Ref<Ship> PilotShip;
			int PilotFrame = -1;
		};

		/// <summary>What a resume cannot put back: who is in the fight and in what state, the hangars, the seats. Any
		/// change between two frames is a barrier.</summary>
		struct HrCensus
		{
			const void* Fight = nullptr;
			bool Over = false;
			int Units = 0, Dead = 0, Fled = 0, Docked = 0, Struck = 0, Taken = 0, Lost = 0, Hangars = 0, Seats = 0;
			unsigned long long Flags = 0;
			const void* Ship = nullptr;
			int Frame = -1;
			bool Piloting = false;

			bool operator==(const HrCensus& o) const
			{
				return Fight == o.Fight && Over == o.Over && Units == o.Units && Dead == o.Dead && Fled == o.Fled && Docked == o.Docked &&
					Struck == o.Struck && Taken == o.Taken && Lost == o.Lost && Hangars == o.Hangars && Seats == o.Seats && Flags == o.Flags &&
					Ship == o.Ship && Frame == o.Frame && Piloting == o.Piloting;
			}
		};

		HrCensus HrTakeCensus(const cs::Ref<Battle>& fight, const cs::Ref<Ship>& ship, int frame, bool piloting)
		{
			HrCensus c;
			c.Ship = ship.get();
			c.Frame = frame;
			c.Piloting = piloting;
			if (fight == nullptr) return c;
			c.Fight = fight.get();
			c.Over = fight->Over;
			c.Units = fight->Units.Count();
			c.Seats = fight->Mechs.Count() + fight->Embedded.Count() + fight->Manned.Count() + fight->Flown.Count();
			unsigned long long h = 1469598103934665603ull;
			for (const auto& u : fight->Units)
			{
				if (u == nullptr) continue;
				c.Dead += u->Dead ? 1 : 0;
				c.Fled += u->Fled ? 1 : 0;
				c.Docked += u->Docked ? 1 : 0;
				c.Struck += u->Surrendering ? 1 : 0;
				c.Taken += u->Captured ? 1 : 0;
				c.Lost += u->Ship != nullptr && u->Ship->Destroyed ? 1 : 0;
				if (u->Combat != nullptr)
				{
					c.Hangars += u->Combat->Hangar != nullptr ? u->Combat->Hangar->Count() : 0;
					c.Hangars += u->Combat->ShuttleBay != nullptr ? u->Combat->ShuttleBay->Count() : 0;
				}
				unsigned long long bits = (u->Dead ? 1u : 0u) | (u->Fled ? 2u : 0u) | (u->Docked ? 4u : 0u) | (u->Surrendering ? 8u : 0u) |
					(u->Captured ? 16u : 0u) | (u->PlayerControlled() ? 32u : 0u);
				h = (h ^ (static_cast<unsigned long long>(u->Id) * 64 + bits)) * 1099511628211ull;
			}
			c.Flags = h;
			return c;
		}

		/// <summary>In words, what changed between two censuses (the barrier's reason).</summary>
		cs::String HrWhyChanged(const HrCensus& a, const HrCensus& b)
		{
			if (a.Fight != b.Fight) return "another fight came into view";
			if (a.Over != b.Over) return "the fight ended";
			if (a.Ship != b.Ship || a.Piloting != b.Piloting) return "you changed seat";
			if (a.Frame != b.Frame) return "you crossed into another body's space";
			if (b.Dead > a.Dead || b.Lost > a.Lost) return "a hull was lost";
			if (b.Fled > a.Fled) return "a hull escaped the fight";
			if (b.Taken != a.Taken || b.Struck != a.Struck) return "a hull struck or was taken";
			if (b.Units != a.Units) return "a craft launched or a hull joined the fight";
			if (b.Hangars != a.Hangars || b.Docked != a.Docked) return "a craft launched or landed";
			if (b.Seats != a.Seats) return "a gun, a craft or a mech was taken or let go";
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

		std::shared_ptr<HrAnchor> HrCaptureAnchor(double t, const cs::Ref<Battle>& fight, bool piloting, const PilotState& pilot, const cs::Ref<Ship>& ship, int frame)
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
					}
					if (u->Power != nullptr) a->Powers.emplace_back(u->Power, *u->Power);
					if (u->Fighter != nullptr) a->Fighters.emplace_back(u->Fighter, *u->Fighter);
					if (u->Broadside != nullptr) a->Broadsides.emplace_back(u->Broadside, *u->Broadside);
				}
				for (const auto& m : fight->Missiles)
					if (m != nullptr) a->Missiles.emplace_back(m, *m);
				for (const auto& r : fight->Rounds)
					if (r != nullptr) a->Rounds.emplace_back(r, *r);
				for (const auto& m : fight->Mines)
					if (m != nullptr) a->Mines.emplace_back(m, *m);
				for (const auto& d : fight->Decoys)
					if (d != nullptr) a->Decoys.emplace_back(d, *d);
			}
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
		/// fight (its events) stay as they are now; everything else of the Battle, each unit and what it owns is the
		/// resume point's.</summary>
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
				auto helpDueS = b.HelpDueS;
				auto fighterStep = b.FighterStep;
				auto launch = b.Launch;
				auto wantsAlive = b.WantsAlive;
				auto boards = b.Boards;
				b = *a.Copy;
				b.UnitCaptured = std::move(unitCaptured);
				b.UnitDestroyed = std::move(unitDestroyed);
				b.UnitSurrendered = std::move(unitSurrendered);
				b.SurrenderDecided = std::move(surrenderDecided);
				b.CargoDemanded = std::move(cargoDemanded);
				b.HelpDueS = std::move(helpDueS);
				b.FighterStep = std::move(fighterStep);
				b.Launch = std::move(launch);
				b.WantsAlive = std::move(wantsAlive);
				b.Boards = std::move(boards);
			}
			for (const auto& p : a.Units) *p.first = p.second;
			for (const auto& p : a.Combats) *p.first = p.second;
			for (const auto& p : a.Subsystems) *p.first = p.second;
			for (const auto& p : a.Powers) *p.first = p.second;
			for (const auto& p : a.Fighters) *p.first = p.second;
			for (const auto& p : a.Broadsides) *p.first = p.second;
			for (const auto& p : a.Missiles) *p.first = p.second;
			for (const auto& p : a.Rounds) *p.first = p.second;
			for (const auto& p : a.Mines) *p.first = p.second;
			for (const auto& p : a.Decoys) *p.first = p.second;
		}
	}

	struct UnitySpaceApp::HoldRewindState
	{
		/// <summary>Real seconds the simulation has run while recorded (a pause, the screens between the stars and the
		/// hold itself do not count): the key every pose and resume point is kept by.</summary>
		double LivedS = 0;

		cs::Ref<Battle> Fight;
		double FightSince = 0;
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
		double BarrierT = std::numeric_limits<double>::lowest();
		cs::String BarrierWhy;

		bool Active = false, KeyWasDown = false;
		double PressT = 0, Tau = 0, Oldest = 0, HeldS = 0;
		std::shared_ptr<HrAnchor> AtPress;
		/// <summary>Wall-clock seconds (Time::realtimeSinceStartupAsDouble) before the next hold may start.</summary>
		double CooldownUntil = 0;

		cs::Ref<GUIStyle> Text;
	};

	bool UnitySpaceApp::HoldRewindActive() const { return holdRewind != nullptr && holdRewind->Active; }

	cs::Ref<Battle> UnitySpaceApp::HoldRewindBattle()
	{
		if (IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr && !Piloted.Ship->Battle->Over) return Piloted.Ship->Battle;
		if (battleTimeFor != nullptr && !battleTimeFor->Over) return battleTimeFor;
		return nullptr;
	}

	void UnitySpaceApp::ClearHoldRewind()
	{
		if (holdRewind == nullptr) return;
		double cooldown = holdRewind->CooldownUntil;
		bool keyDown = holdRewind->KeyWasDown;
		auto text = holdRewind->Text;
		*holdRewind = HoldRewindState();
		holdRewind->CooldownUntil = cooldown;
		holdRewind->KeyWasDown = keyDown;
		holdRewind->Text = text;
	}

	void UnitySpaceApp::RecordHoldRewind(float dt)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto& h = *holdRewind;
		if (h.Active) return;
		// Single player, in a run that keeps a rewind at all (not the tour, a capture or -norewind).
		if (!rewindEnabled || Coop::CoopPeer::Current() != nullptr || Clock == nullptr || World() == nullptr)
		{
			if (!h.Clock.empty() || h.Fight != nullptr) ClearHoldRewind();
			return;
		}
		// Nothing lived: the clock stopped, or between the stars (a barrier, through the seat, once flying again).
		if (Clock->Paused() || Travel->Active || InFtlCruise()) return;

		h.LivedS += dt;
		double t = h.LivedS;
		bool piloting = IsPiloting();
		auto ship = piloting ? World()->PilotedShip : nullptr;
		auto fight = HoldRewindBattle();
		if (fight != h.Fight)
		{
			// Another fight (or none): its tracks start now, and nothing before now can be played on from.
			h.Units.clear();
			h.Missiles.clear();
			h.Rounds.clear();
			h.Decoys.clear();
			h.Anchors.clear();
			h.Fight = fight;
			h.FightSince = t;
			h.NextAnchorAt = 0;
		}
		HrCensus census = HrTakeCensus(fight, ship, pilotFrameBody, piloting);
		if (h.HasLast && !(census == h.Last))
		{
			h.BarrierWhy = HrWhyChanged(h.Last, census);
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
		h.Clock.push_back(HrClockMark { t, s });
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
		if (piloting) h.Pilot.push_back(HrPilotPose { t, pilot() });

		if (t >= h.NextAnchorAt && (fight != nullptr || piloting))
		{
			h.Anchors.push_back(HrCaptureAnchor(t, fight, piloting, pilot(), ship, pilotFrameBody));
			h.NextAnchorAt = t + HrTuning::AnchorS;
		}

		// Only the window (and one pose before it, for the curve into it) is kept.
		double cutoff = t - HrTuning::WindowS - 1.0;
		HrPruneTracks(h.Units, cutoff, h.Samples);
		HrPruneTracks(h.Missiles, cutoff, h.Samples);
		HrPruneTracks(h.Rounds, cutoff, h.Samples);
		HrPruneTracks(h.Decoys, cutoff, h.Samples);
		HrPruneFront(h.Clock, cutoff, nullptr);
		HrPruneFront(h.Pilot, cutoff, nullptr);
		while (h.Anchors.size() >= 2 && h.Anchors[1]->T < cutoff) h.Anchors.pop_front();
	}

	cs::String UnitySpaceApp::HoldRewindRefusal()
	{
		auto& h = *holdRewind;
		double wall = Time::realtimeSinceStartupAsDouble;
		if (Coop::CoopPeer::Current() != nullptr) return "Rewind is single-player for now";
		if (!rewindEnabled) return "Rewind is off in this run (-norewind, -demo, -capture)";
		if (wall < h.CooldownUntil) return cs::Cat("REWIND ready in ", static_cast<int>(std::ceil(h.CooldownUntil - wall)), " s");
		if (Travel->Active || InFtlCruise() || Ftl->Stage != FtlStage::Idle) return "No rewind between the stars or under FTL";
		if (rewindReplaying) return "A rewind is already replaying";
		if (h.Fight == nullptr && h.Pilot.empty()) return "Nothing here to rewind: fly a hull or watch a fight";
		if (h.Clock.size() < 2 || h.LivedS - h.Clock.front().T < HrTuning::MinHoldS) return "Nothing to rewind yet";
		return cs::String();
	}

	bool UnitySpaceApp::UpdateHoldRewind(float dt)
	{
		if (holdRewind == nullptr) holdRewind = std::make_shared<HoldRewindState>();
		auto& h = *holdRewind;
		bool down = Input.HoldRewind;
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
			h.AtPress = HrCaptureAnchor(h.LivedS, h.Fight, piloting, pilot(), piloting ? h.PilotShip : nullptr, pilotFrameBody);
			h.Active = true;
			h.PressT = h.LivedS;
			h.Tau = h.LivedS;
			h.HeldS = 0;
			h.Oldest = std::max(h.LivedS - HrTuning::WindowS, h.Clock.front().T);
			return true;
		}
		if (!down || Coop::CoopPeer::Current() != nullptr)
		{
			EndHoldRewind();
			return false;
		}
		h.HeldS += dt;
		h.Tau = std::max(h.Oldest, h.Tau - static_cast<double>(dt) * HrTuning::PlaybackSpeed);
		PlayHoldRewind();
		return true;
	}

	void UnitySpaceApp::PlayHoldRewind()
	{
		auto& h = *holdRewind;
		double tau = h.Tau;
		if (h.Fight != nullptr)
		{
			if (!h.Clock.empty())
			{
				auto it = std::lower_bound(h.Clock.begin(), h.Clock.end(), tau, [](const HrClockMark& m, double t) { return m.T < t; });
				if (it == h.Clock.end()) h.Fight->Seconds = h.Clock.back().S;
				else if (it == h.Clock.begin() || it->T <= tau) h.Fight->Seconds = it->S;
				else
				{
					const HrClockMark& a = *(it - 1);
					double f = (tau - a.T) / std::max(1e-9, it->T - a.T);
					h.Fight->Seconds = a.S + (it->S - a.S) * f;
				}
			}
			for (const auto& u : h.Fight->Units)
			{
				if (u == nullptr) continue;
				auto found = h.Units.find(u.get());
				if (found == h.Units.end()) continue;
				const auto& poses = found->second.Poses;
				HrPose p;
				if (!HrPoseAt(poses, tau, p)) continue;
				// A hull that came into the fight after its tracks began is not there yet before its first pose.
				bool notYet = !poses.empty() && poses.front().T > h.FightSince + 0.5 && tau < poses.front().T;
				u->Pos = p.Pos;
				u->Vel = p.Vel;
				u->Forward = p.Fwd;
				u->Dead = p.Dead;
				u->Fled = p.Fled;
				u->Docked = p.Docked || notYet;
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
			pilot() = it->State;
			pilotAim = pilot().Forward;
		}
	}

	void UnitySpaceApp::EndHoldRewind()
	{
		auto& h = *holdRewind;
		h.Active = false;
		auto atPress = h.AtPress;
		h.AtPress = nullptr;
		double wall = Time::realtimeSinceStartupAsDouble;
		auto putBack = [this](const HrAnchor& a)
		{
			HrRestoreFight(a);
			if (a.HasPilot && IsPiloting() && World()->PilotedShip == a.PilotShip && pilotFrameBody == a.PilotFrame)
			{
				pilot() = a.Pilot;
				pilotAim = a.Pilot.Forward;
			}
		};
		// A tap, or a hold that never got back: the present as it was at the press.
		if (h.HeldS < HrTuning::MinHoldS || h.PressT - h.Tau < 1e-3)
		{
			if (atPress != nullptr) putBack(*atPress);
			return;
		}
		// The resume point: the newest kept at or before the moment reached, none from before the last barrier (then
		// the oldest after it, nearer now than the moment reached).
		std::shared_ptr<HrAnchor> best;
		for (const auto& a : h.Anchors)
			if (a->T >= h.BarrierT && a->T <= h.Tau + 1e-9) best = a;
		bool clamped = false;
		if (best == nullptr)
			for (const auto& a : h.Anchors)
				if (a->T >= h.BarrierT && a->T > h.Tau && a->T < h.PressT)
				{
					best = a;
					clamped = true;
					break;
				}
		h.CooldownUntil = wall + HrTuning::CooldownS;
		if (best == nullptr)
		{
			if (atPress != nullptr) putBack(*atPress);
			ShowToast(cs::Cat("Could not play on from back there: ", h.BarrierWhy.empty() ? cs::String("nothing kept to play on from") : h.BarrierWhy,
				", and that is not undone yet. Back to now"));
			return;
		}
		putBack(*best);
		double went = h.PressT - best->T;
		// The old future is gone: what was kept after the resume point is dropped, and the rest moves up so that the
		// resume point is now (the next hold goes back through it).
		double by = h.LivedS - best->T;
		HrTrimAndShift(h.Units, best->T, by, h.Samples);
		HrTrimAndShift(h.Missiles, best->T, by, h.Samples);
		HrTrimAndShift(h.Rounds, best->T, by, h.Samples);
		HrTrimAndShift(h.Decoys, best->T, by, h.Samples);
		HrTrimBack(h.Clock, best->T, nullptr);
		HrShiftPoses(h.Clock, by);
		HrTrimBack(h.Pilot, best->T, nullptr);
		HrShiftPoses(h.Pilot, by);
		while (!h.Anchors.empty() && h.Anchors.back()->T > best->T) h.Anchors.pop_back();
		for (auto& a : h.Anchors) a->T += by;
		if (h.BarrierT > std::numeric_limits<double>::lowest()) h.BarrierT += by;
		h.HasLast = false;
		// US-T99's frame log can no longer replay across this: the fight was set back by hand, not by the logged frames.
		// A seek lands on its keyframes from here (they hold the whole world, so they are still exact).
		rewindFrames.clear();
		rewindKeySerial.clear();
		ShowToast(cs::Cat("Rewound ", cs::Fmt(went, "F1"), " s", clamped ? cs::Cat(" (no further: ", h.BarrierWhy, ")") : cs::String(),
			h.Fight == nullptr ? cs::String(": your hull's flight; the rest of the system goes on from now") : cs::String(": play on")));
	}

	void UnitySpaceApp::DrawHoldRewind()
	{
		if (holdRewind == nullptr || ScreenCleared || Coop::CoopPeer::Current() != nullptr || !rewindEnabled) return;
		auto& h = *holdRewind;
		if (!h.Active && h.Fight == nullptr && !IsPiloting()) return;
		if (h.Text == nullptr)
		{
			h.Text = cs::New<GUIStyle>();
			h.Text->fontSize = 12;
			h.Text->alignment = TextAnchor::MiddleRight;
			h.Text->clipping = TextClipping::Clip;
		}
		double wall = Time::realtimeSinceStartupAsDouble;
		cs::String line;
		Color tint = UI::Hud::Dim;
		Rect r(Screen::width - 520.0f, 44, 500, 20);
		if (h.Active)
		{
			double back = h.PressT - h.Tau;
			line = cs::Cat("<< REWIND  -", cs::Fmt(back, "F1"), " s");
			if (h.Tau <= h.Oldest + 1e-6) line = cs::Cat(line, "  (as far back as it goes)");
			if (h.BarrierT > h.Tau) line = cs::Cat(line, "  -  plays on no earlier than -", cs::Fmt(h.PressT - h.BarrierT, "F1"), " s: ", h.BarrierWhy);
			line = cs::Cat(line, "  -  let go to play on");
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
		}
		h.Text->normal.textColor = tint;
		GUI::Label(r, line, h.Text);
	}
}
