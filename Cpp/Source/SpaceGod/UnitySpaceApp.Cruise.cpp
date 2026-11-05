// US-T127: the in-system cruise drive at the helm (declarations in UnitySpaceApp.Cruise.inl; the drive itself is
// Core::SystemCruise, Combat/SystemCruise.h).

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "Camera/StarTravel.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/Pursuit.h"
#include "Combat/SystemCruise.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/HelmFlight.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "ScaleModel.h"
#include "Sensors/SensorPosture.h"
#include "Ships.h"
#include "SimClock.h"
#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "World.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		cs::String CruiseSpeedText(double kmS)
		{
			double c = kmS / SystemCruise::LightKmS;
			if (c >= 10) return cs::Cat(cs::Fmt(c, "N0"), " c");
			if (c >= 0.01) return cs::Cat(cs::Fmt(c, "0.00"), " c");
			return cs::Cat(cs::Fmt(kmS, "N0"), " km/s");
		}

		cs::String CruiseDistanceText(double km)
		{
			if (km >= 0.01 * Astro::AuKm) return cs::Cat(cs::Fmt(km / Astro::AuKm, "0.00"), " AU");
			return cs::Cat(cs::Fmt(km, "N0"), " km");
		}

		cs::String CruiseSecondsText(double s)
		{
			if (!(s < cs::Double::PositiveInfinity)) return "--";
			return s >= 10 ? cs::Cat(cs::Fmt(s, "N0"), " S") : cs::Cat(cs::Fmt(s, "0.0"), " S");
		}
	}

	bool UnitySpaceApp::SystemCruiseActive() const
	{
		return systemCruise.Active() && IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship == cruiseShip;
	}

	cs::String UnitySpaceApp::SystemCruiseBlocker()
	{
		if (!IsPiloting() || Piloted.Ship == nullptr) return "take a helm first";
		auto ship = Piloted.Ship;
		if (InFtlCruise() || Travel->Active) return "between the stars";
		if (ship->Battle != nullptr) return "not in a fight";
		if (ship->Phase == ShipPhase::Lane) return "riding a lane";
		if (PilotStunSeconds() > 0 || helmFlight->DrivesDown) return "the drives are knocked out";
		if (SensorPosture::Offline(ship->Combat)) return "powered down (Shift+F3 to power up)";
		if (ship->OutOfPropellant) return "the tanks are dry";
		if (!(HullDynamics::Of(ship->Combat->Spec).MainAccel > 0)) return "this hull has no drive";
		return cs::String();
	}

	// ---------------------------------------------------------------- engaging and dropping out

	void UnitySpaceApp::ToggleSystemCruise()
	{
		if (SystemCruiseActive())
		{
			DropSystemCruise(cs::Cat("Dropped out of cruise", cruiseTargetKm < cs::Double::PositiveInfinity ? cs::Cat(", ", CruiseDistanceText(cruiseTargetKm), " from ", cruiseTargetName) : cs::String()));
			return;
		}
		auto why = SystemCruiseBlocker();
		if (!why.empty())
		{
			ShowToast(cs::Cat("No cruise: ", why));
			cruiseTargetPicked = cruiseAlign = false;
			return;
		}
		auto ship = Piloted.Ship;
		auto helio = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		int nearest;
		double clearance = CruiseClearanceKm(helio, nearest);
		if (nearest >= 0 && clearance < (*Catalog())[nearest]->RadiusKm * 0.25)
		{
			ShowToast(cs::Cat("No cruise this deep in ", (*Catalog())[nearest]->Name, "'s gravity well: climb out first"));
			cruiseTargetPicked = cruiseAlign = false;
			return;
		}
		systemCruise.Drop();
		cruiseShip = ship;
		cruiseHull = -1;
		if (!cruiseTargetPicked)
		{
			// A hull, world or station locked is the target; else whatever the nose is on.
			auto locked = pilotLockTarget;
			DVec3 at;
			double standoff;
			cs::String name;
			if (!locked.IsNone() && !(locked.IsShip() && locked.Ship == ship) && CruiseTargetAt(locked, at, standoff, name))
			{
				cruiseTarget = locked;
				cruiseTargetPicked = true;
			}
			else cruiseTarget = CruiseNoseTarget(helio, pilotForward(), 20);
		}
		if (!systemCruise.Engage(helmFlight->Input, HullDynamics::Of(ship->Combat->Spec).MassKt))
		{
			ShowToast("No cruise: the drive answers only a helmsman's hands");
			return;
		}
		// Off at a thousand times light's speed: whoever was running the hull down is left behind.
		if (World()->Pursuit != nullptr && World()->Pursuit->Active()) World()->Pursuit->ReleaseAll(Clock->Jd);
		DVec3 at;
		double standoff;
		cs::String name;
		bool bound = !cruiseTarget.IsNone() && CruiseTargetAt(cruiseTarget, at, standoff, name);
		ShowToast(cs::Cat("CRUISE DRIVE SPOOLING, ", cs::Fmt(systemCruise.SpoolS, "0.0"), " s", bound ? cs::Cat(": bound for ", name, ", ", CruiseDistanceText((at - helio).Length())) : cs::String(": no target, put the nose on a world"),
			"  (Shift+J drops out; the mouse steers)"));
	}

	void UnitySpaceApp::CruiseTo(const Target& target)
	{
		if (!IsPiloting())
		{
			ShowToast("No cruise: take a helm first (Helm > Pilot the nearest ship)");
			return;
		}
		cruiseTarget = target;
		cruiseTargetPicked = true;
		cruiseAlign = true;
		if (!SystemCruiseActive()) ToggleSystemCruise();
		else
		{
			DVec3 at;
			double standoff;
			cs::String name;
			if (CruiseTargetAt(target, at, standoff, name)) ShowToast(cs::Cat("Cruise: now bound for ", name));
		}
	}

	void UnitySpaceApp::DropSystemCruise(const cs::String& why)
	{
		bool cruising = systemCruise.Cruising() && IsPiloting() && Piloted.Ship == cruiseShip;
		systemCruise.Drop();
		cruiseTargetPicked = cruiseAlign = false;
		cruiseTarget = Target::None();
		cruiseTargetKm = cruiseToDropKm = cs::Double::PositiveInfinity;
		if (cruising)
		{
			// Out at a gentle pace along the nose, in the frame of whatever it has come to; the fleet's record of the hull
			// brought up to where it is now.
			RebaseCruiseFrame();
			SetpilotVelKmS(pilotForward() * (PilotTuning::Of(Piloted.Ship->Spec->Class).MaxSpeed * 0.5));
			PinWherePiloted(Piloted.Ship);
		}
		if (!why.empty()) ShowToast(why);
	}

	// ---------------------------------------------------------------- each frame at the helm

	void UnitySpaceApp::CruiseStick(EmbodimentInput& stick)
	{
		if (!SystemCruiseActive()) return;
		if (cruiseAlign)
		{
			// The menu's Cruise to swings the nose onto the target, until the player takes the stick.
			bool touched = Input.MouseDelta.sqrMagnitude() > 4 || Mathf::Abs(Input.Move.x) > 0.1f || Mathf::Abs(Input.Move.y) > 0.1f || Mathf::Abs(Input.Roll) > 0.1f;
			DVec3 at;
			double standoff;
			cs::String name;
			if (touched || !CruiseTargetAt(cruiseTarget, at, standoff, name)) cruiseAlign = false;
			else
			{
				auto to = at - (Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm());
				if (to.LengthSquared() > 1e-6)
				{
					stick.Aim = to.Normalized();
					pilotAim = stick.Aim;
				}
			}
		}
		if (!systemCruise.Cruising()) return;
		// The drive carries the hull; the stick only turns it.
		stick.Throttle = stick.StrafeRight = stick.StrafeUp = 0;
		stick.Boost = stick.Cruise = false;
	}

	void UnitySpaceApp::UpdateSystemCruise(const cs::Ref<Ship>& ship, float dt)
	{
		if (cruiseShip != ship)
		{
			// Another hull flown: the drive was the last one's.
			systemCruise.Drop();
			cruiseShip = nullptr;
			cruiseTargetPicked = cruiseAlign = false;
			cruiseTarget = Target::None();
		}
		if (Input.ToggleSystemCruise) ToggleSystemCruise();
		if (!systemCruise.Active() || cruiseShip != ship) return;

		auto why = SystemCruiseBlocker();
		if (!why.empty())
		{
			DropSystemCruise(cs::Cat("Out of cruise: ", why));
			return;
		}
		double hull = ship->Combat->Hull;
		if (cruiseHull >= 0 && hull < cruiseHull - 1e-9)
		{
			DropSystemCruise("Out of cruise: hit");
			return;
		}
		cruiseHull = hull;

		auto helio = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		auto forward = pilotForward();
		// The target: the one picked while it stays ahead, else what the nose is put on.
		if (!cruiseTargetPicked)
		{
			auto onNose = CruiseNoseTarget(helio, forward, 6);
			if (!onNose.IsNone()) cruiseTarget = onNose;
		}
		cruiseTargetKm = cruiseToDropKm = cs::Double::PositiveInfinity;
		double aimDeg = 180;
		DVec3 at;
		double standoff = 0;
		cs::String name;
		if (!cruiseTarget.IsNone() && CruiseTargetAt(cruiseTarget, at, standoff, name))
		{
			auto to = at - helio;
			double km = to.Length();
			aimDeg = km > 1e-9 ? std::acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(to / km, forward)))) * 180 / cs::Math::PI : 0;
			if (aimDeg > 70 && !cruiseAlign)
			{
				// Turned away from it: a free cruise until the nose finds another.
				if (cruiseTargetPicked) ShowToast(cs::Cat("Cruise: turned off ", name, ", free cruise"));
				cruiseTarget = Target::None();
				cruiseTargetPicked = false;
			}
			else
			{
				cruiseTargetKm = km;
				cruiseTargetName = name;
				cruiseToDropKm = cs::Math::Max(0.0, km - standoff);
			}
		}
		else if (!cruiseTarget.IsNone())
		{
			cruiseTarget = Target::None();
			cruiseTargetPicked = false;
		}

		int nearest;
		double clearance = CruiseClearanceKm(helio, nearest);
		int targetBody = cruiseTarget.IsBody() ? cruiseTarget.Body : -1;
		// Aligning for the menu's Cruise to, it engages once the nose is near enough on the target.
		bool mayEngage = !cruiseAlign || aimDeg < 12;
		bool wasCruising = systemCruise.Cruising();
		double move = systemCruise.Step(helmFlight->Input, cruiseToDropKm, clearance, mayEngage, dt);
		if (!systemCruise.Active())
		{
			DropSystemCruise(cs::String());
			return;
		}
		if (!wasCruising && systemCruise.Cruising())
			ShowToast(cs::Cat("CRUISE DRIVE ENGAGED", cruiseTargetKm < cs::Double::PositiveInfinity ? cs::Cat(": ", name, " ", CruiseDistanceText(cruiseTargetKm)) : cs::String(": free cruise")));
		if (!systemCruise.Cruising()) return;

		SetpilotOffsetKm(pilotOffsetKm() + forward * move);
		SetpilotVelKmS(DVec3::Zero);
		RebaseCruiseFrame();
		if (cruiseToDropKm < cs::Double::PositiveInfinity && cruiseToDropKm - move <= 1e-6)
		{
			DropSystemCruise(cs::Cat("Dropped out of cruise ", CruiseDistanceText(standoff), " off ", name));
			return;
		}
		if (nearest >= 0 && nearest != targetBody && clearance < (*Catalog())[nearest]->RadiusKm * 0.25)
		{
			DropSystemCruise(cs::Cat("Pulled out of cruise by ", (*Catalog())[nearest]->Name, "'s gravity well"));
			return;
		}
		// Far out past the system's worlds there is nothing left to make for in cruise (the jump drive is the way on).
		if ((helio - Ephemeris()->Helio(Catalog()->Root()->Index)).Length() > 2000 * Astro::AuKm)
			DropSystemCruise("Out of cruise at the edge of the system's reach (the jump drive is the way on)");
	}

	// ---------------------------------------------------------------- targets, wells and frames

	bool UnitySpaceApp::CruiseTargetAt(const Target& t, DVec3& helio, double& standoffKm, cs::String& name)
	{
		double jd = Clock->Jd;
		if (t.IsBody())
		{
			if (t.Body < 0 || t.Body >= Catalog()->Count()) return false;
			auto def = (*Catalog())[t.Body];
			helio = Ephemeris()->Helio(t.Body);
			DVec3 outward;
			standoffKm = BodyStandoffKm(t.Body, outward);
			name = def->Name;
			return true;
		}
		if (t.IsStructure() && t.Structure != nullptr)
		{
			helio = t.Structure->HelioAt(Ephemeris(), jd);
			standoffKm = cs::Math::Max(4.0, t.Structure->LengthKm * 6);
			name = t.Structure->Name;
			return true;
		}
		if (t.IsShip() && t.Ship != nullptr && !t.Ship->Destroyed && Fleet()->IsPresent(t.Ship))
		{
			helio = Fleet()->StateOf(t.Ship, jd).HelioKm;
			standoffKm = cs::Math::Max(3.0, t.Ship->LengthKm() * 8 + 2);
			name = t.Ship->Name;
			return true;
		}
		return false;
	}

	Target UnitySpaceApp::CruiseNoseTarget(DVec3 helio, DVec3 forward, double coneDeg)
	{
		Target best = Target::None();
		double bestDeg = coneDeg;
		auto consider = [&](const Target& t, DVec3 at, double radiusKm, double standoffKm)
		{
			auto to = at - helio;
			double km = to.Length();
			if (km <= standoffKm * 1.2 || km < 1e-6) return; // already there
			double deg = std::acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(to / km, forward)))) * 180 / cs::Math::PI;
			// A world's own disc counts as on the nose.
			deg -= std::asin(cs::Math::Min(1.0, radiusKm / km)) * 180 / cs::Math::PI;
			if (deg < bestDeg)
			{
				bestDeg = deg;
				best = t;
			}
		};
		for (const auto& b : Catalog()->Bodies())
		{
			if (!b->IsVisible() || b->RadiusKm <= 0) continue;
			DVec3 outward;
			consider(Target::OfBody(b->Index), Ephemeris()->Helio(b->Index), b->RadiusKm, BodyStandoffKm(b->Index, outward));
		}
		for (const auto& st : World()->Infrastructure->Structures)
			consider(Target::OfStructure(st), st->HelioAt(Ephemeris(), Clock->Jd), st->LengthKm, cs::Math::Max(4.0, st->LengthKm * 6));
		return best;
	}

	double UnitySpaceApp::CruiseClearanceKm(DVec3 helio, int& nearest)
	{
		nearest = -1;
		double best = cs::Double::PositiveInfinity;
		for (const auto& b : Catalog()->Bodies())
		{
			if (b->RadiusKm <= 0) continue;
			double d = (helio - Ephemeris()->Helio(b->Index)).Length() - b->RadiusKm;
			if (d < best)
			{
				best = d;
				nearest = b->Index;
			}
		}
		return best;
	}

	bool UnitySpaceApp::CruiseHolds(int body, DVec3 helio)
	{
		auto b = (*Catalog())[body];
		if (b->ParentIndex < 0) return true;
		if ((helio - Ephemeris()->Helio(body)).Length() <= ScaleModel::NeighbourhoodKm(b)) return true;
		auto mine = Scale()->MapNear(Ephemeris(), bodyScene, body, helio);
		auto outer = Scale()->MapNear(Ephemeris(), bodyScene, b->ParentIndex, helio);
		return (mine - outer).Length() > 1e-7 * cs::Math::Max(1.0, (mine - bodyScene[body]).Length());
	}

	void UnitySpaceApp::RebaseCruiseFrame()
	{
		auto helio = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		int frame = pilotFrameBody;
		while (frame >= 0 && (*Catalog())[frame]->ParentIndex >= 0 && !CruiseHolds(frame, helio)) frame = (*Catalog())[frame]->ParentIndex;
		if (frame < 0) frame = Catalog()->Root()->Index;
		for (bool deeper = true; deeper;)
		{
			deeper = false;
			for (const auto& b : Catalog()->Bodies())
				if (b->ParentIndex == frame && CruiseHolds(b->Index, helio))
				{
					frame = b->Index;
					deeper = true;
					break;
				}
		}
		if (frame == pilotFrameBody) return;
		SetpilotOffsetKm(helio - Ephemeris()->Helio(frame));
		pilotFrameBody = frame;
		helmFlight->FrameBody = frame;
	}

	DVec3 UnitySpaceApp::PilotedScene()
	{
		auto def = (*Catalog())[pilotFrameBody];
		auto off = pilotOffsetKm();
		if (off.Length() <= ScaleModel::NeighbourhoodKm(def)) return bodyScene[pilotFrameBody] + Scale()->LocalOffset(def, off);
		return Scale()->MapNear(Ephemeris(), bodyScene, pilotFrameBody, Ephemeris()->Helio(pilotFrameBody) + off);
	}

	// ---------------------------------------------------------------- the readout

	void UnitySpaceApp::DrawSystemCruise()
	{
		if (!SystemCruiseActive() || ScreenCleared) return;
		if (cruiseStyle == nullptr)
		{
			cruiseBack = UI::Hud::Solid(Color(0.03f, 0.05f, 0.08f, 0.82f), Color(0.45f, 0.85f, 1.0f, 0.7f));
			cruiseBar = UI::Hud::Solid(Color(0.45f, 0.85f, 1.0f, 0.9f), Color::clear());
			cruisePanel = cs::New<GUIStyle>();
			cruisePanel->normal.background = cruiseBack;
			cruisePanel->border = RectOffset(2, 2, 2, 2);
			cruiseStyle = cs::New<GUIStyle>();
			cruiseStyle->fontSize = 15;
			cruiseStyle->fontStyle = FontStyle::Bold;
			cruiseStyle->alignment = TextAnchor::MiddleCenter;
			cruiseStyle->normal.textColor = UI::Hud::Accent;
			cruiseStyle->clipping = TextClipping::Clip;
			cruiseSmall = cs::New<GUIStyle>(*cruiseStyle);
			cruiseSmall->fontSize = 11;
			cruiseSmall->fontStyle = FontStyle::Normal;
			cruiseSmall->normal.textColor = UI::Hud::Dim;
		}
		bool bound = cruiseTargetKm < cs::Double::PositiveInfinity;
		cs::String head, tail;
		if (systemCruise.Spooling())
		{
			bool aligning = systemCruise.SpoolLeftS <= 0 && cruiseAlign;
			head = aligning ? cs::Cat("CRUISE DRIVE READY  ·  ALIGNING ON ", cs::Strings::ToUpperInvariant(cruiseTargetName))
				: cs::Cat("CRUISE DRIVE SPOOLING  ·  ", cs::Fmt(systemCruise.SpoolFraction() * 100, "N0"), "%  ·  ENGAGES IN ", CruiseSecondsText(systemCruise.SpoolLeftS));
			tail = bound ? cs::Cat("bound for ", cruiseTargetName, ", ", CruiseDistanceText(cruiseTargetKm), "   ·   Shift+J calls it off")
				: cs::String("no target: put the nose on a world or station to make for it   ·   Shift+J calls it off");
		}
		else
		{
			head = cs::Cat("CRUISE  ·  ", CruiseSpeedText(systemCruise.SpeedKmS), bound ? cs::Cat("  ·  ", cs::Strings::ToUpperInvariant(cruiseTargetName), " ", CruiseDistanceText(cruiseTargetKm),
				"  ·  DROP IN ", CruiseSecondsText(systemCruise.EtaS(cruiseToDropKm))) : cs::String("  ·  FREE CRUISE"));
			tail = cs::Cat(bound ? cs::String("the mouse steers; it slows as it closes and drops out off the target") : cs::String("put the nose on a world or station to make for it"),
				"   ·   Shift+J drops out   ·   true km and seconds at either scale (", ScaleGoal > 0.5 ? "game" : "realistic", " scale now)");
		}
		float w = 760, h = 48;
		auto r = Rect((Screen::width - w) * 0.5f, 78, w, h);
		GUI::Box(r, GUIContent::none(), cruisePanel);
		GUI::Label(Rect(r.x, r.y + 4, w, 22), head, cruiseStyle);
		GUI::Label(Rect(r.x, r.y + 26, w, 18), tail, cruiseSmall);
		if (systemCruise.Spooling())
			GUI::DrawTexture(Rect(r.x + 2, r.yMax() - 4, (w - 4) * static_cast<float>(systemCruise.SpoolFraction()), 3), cruiseBar);
	}

	// ---------------------------------------------------------------- the dev menu's entries (US-T126)

	void UnitySpaceApp::RegisterCruiseCheats()
	{
		using UI::CheatEntry;
		using UI::CheatRegistry;
		CheatRegistry::Add("Travel", CheatEntry::Action("Cruise drive", "spool it and fly for what the nose is on, or drop out (US-T127)", [](UnitySpaceApp& a) { a.ToggleSystemCruise(); })
			.WithKeys("Shift+J").At(-20)
			.WithState([](UnitySpaceApp& a)
			{
				if (!a.SystemCruiseActive()) return cs::String("OFF");
				return a.systemCruise.Spooling() ? cs::String("SPOOLING") : CruiseSpeedText(a.systemCruise.SpeedKmS);
			})
			.WithBlocker([](UnitySpaceApp& a) { return a.SystemCruiseActive() ? cs::String() : a.SystemCruiseBlocker(); }));
		CheatRegistry::Add("Travel", CheatEntry::Menu("Cruise to", "pick a world or station: the nose swings onto it and the cruise drive takes you there", [](UnitySpaceApp& a)
		{
			std::vector<CheatEntry> out;
			auto me = a.PlayerHelioKm();
			for (const auto& b : a.Catalog()->Bodies())
			{
				if (!b->IsVisible() || b->RadiusKm <= 0) continue;
				auto t = Target::OfBody(b->Index);
				out.push_back(CheatEntry::Action(b->Name, CruiseDistanceText((a.Ephemeris()->Helio(b->Index) - me).Length()), [t](UnitySpaceApp& app) { app.CruiseTo(t); }));
			}
			for (const auto& st : a.World()->Infrastructure->Structures)
			{
				auto t = Target::OfStructure(st);
				out.push_back(CheatEntry::Action(st->Name, cs::Cat("station  ·  ", CruiseDistanceText((st->HelioAt(a.Ephemeris(), a.Clock->Jd) - me).Length())), [t](UnitySpaceApp& app) { app.CruiseTo(t); }));
			}
			return out;
		}).At(-10).WithBlocker([](UnitySpaceApp& a) { return a.IsPiloting() ? cs::String() : cs::String("take a helm first (Helm > Pilot the nearest ship)"); }));
	}

	namespace
	{
		UI::CheatRegistration GCruiseCheats(&UnitySpaceApp::RegisterCruiseCheats);
	}
}
