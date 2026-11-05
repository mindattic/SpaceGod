// Port of Unity.SpaceGod Runtime/Camera/StarTravel.cs (US-T29).

#include "Camera/StarTravel.h"
#include "Camera/SpaceCamera.h"
#include "UnitySpaceApp.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "Settings/GameSetting.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "Views/BodyView.h"
#include "Views/GateView.h"
#include "Views/InterstellarView.h"
#include "Rendering/AssetStreamer.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Cameras
{
	const char* ToString(StarTravel::Leg v)
	{
		switch (v)
		{
			case StarTravel::Leg::Leave: return "Leave";
			case StarTravel::Leg::Cross: return "Cross";
			case StarTravel::Leg::Arrive: return "Arrive";
		}
		return "";
	}

	float StarTravel::Overall() const
	{
		return !Active ? 0 : ThroughGate ? (leg + static_cast<float>(GateJump::Overall(phase, t))) / cs::Math::Max(1, course->Count()) : Mathf::Clamp01((static_cast<int>(Stage) + t) / 3.0f);
	}

	float StarTravel::Warp() const
	{
		if (!Active) return 0;
		if (ThroughGate) return static_cast<float>(GateJump::Warp(phase, t));
		switch (Stage)
		{
			case Leg::Leave: return 0.6f * Smoother(Mathf::Clamp01((t - 0.55f) / 0.45f));
			case Leg::Cross: return 0.6f + 0.4f * Mathf::Sin(Mathf::PI * Mathf::Clamp01((t - 0.18f) / 0.82f));
			default: return 0.6f * (1 - Smoother(Mathf::Clamp01(t / 0.5f)));
		}
	}

	float StarTravel::GateFlash() const { return Active && ThroughGate ? static_cast<float>(GateJump::Flash(phase, EffectT())) : 0; }

	float StarTravel::GateGlow() const { return Active && ThroughGate ? static_cast<float>(GateJump::Glow(phase, EffectT())) : 0; }

	float StarTravel::FovKick() const { return Active && ThroughGate ? static_cast<float>(GateJump::FovKick(phase, EffectT())) : 0; }

	bool StarTravel::HoldingForLoad() const { return Active && ThroughGate && phase == GateJump::Phase::Tunnel && tunnel->Holding(); }

	float StarTravel::EffectT() const { return phase == GateJump::Phase::Tunnel ? static_cast<float>(tunnel->T()) : t; }

	double StarTravel::GateClearance() const { return Active && ThroughGate ? gateClearance : cs::Double::MaxValue; }

	DVec3 StarTravel::Heading() const
	{
		if (!Active) return DVec3::Zero;
		if (!ThroughGate) return (To->PositionLy - From->PositionLy).Normalized();
		auto f = app->SpaceCamera->Rotation * Vector3::forward();
		return DVec3(f.x, f.z, f.y);
	}

	cs::Ref<StarSystem> StarTravel::Hop() const { return ThroughGate && arrival != nullptr ? arrival->Home : To; }

	StarTravel::StarTravel(UnitySpaceApp* inApp)
		: app(inApp)
	{
	}

	void StarTravel::RefreshBanner()
	{
		// "   \xC2\xB7   " is C#'s "   ·   " (U+00B7 MIDDLE DOT, UTF-8).
		Banner = cs::Cat(cs::Strings::ToUpperInvariant(To->Name), "   \xC2\xB7   ", cs::Fmt(Core::Galaxy::DistanceLy(From, To), "0.00"), " LIGHT-YEARS");
		if (!ThroughGate) return;
		cs::StringBuilder sb;
		sb.Append(Banner);
		sb.Append("   \xC2\xB7   COURSE ").Append(cs::Strings::ToUpperInvariant(From->Name));
		for (const auto& g : *course) sb.Append(" > ").Append(cs::Strings::ToUpperInvariant(g->Partner->Home->Name));
		sb.Append("   \xC2\xB7   ");
		if (course->Count() > 1) sb.Append("LEG ").Append(leg + 1).Append(" OF ").Append(course->Count()).Append(": ");
		sb.Append(cs::Strings::ToUpperInvariant(departure->Name));
		Banner = sb.ToString();
	}

	void StarTravel::Begin(const cs::Ref<StarSystem>& to)
	{
		if (to == nullptr || to == app->ActiveSystem) return;
		From = app->ActiveSystem;
		To = to;
		Active = true;
		Stage = Leg::Leave;
		t = 0;
		auto root = app->BodyScene(0);
		leaveStart = app->SpaceCamera->ScenePosition - root;
		if (leaveStart.LengthSquared() < 1) leaveStart = DVec3(0, 0.4, -1);
		leaveFrom = leaveStart.Length();
		leaveTo = cs::Math::Max(leaveFrom * 25, app->Scale()->LocalDistance(app->Catalog()->Root(), LeaveSpanKm(From)));
		startRotation = app->SpaceCamera->Rotation;
		destinationBuilt = false;
		// The view keeps the plane it left until it passes into the destination's Oort cloud, then (with
		// auto-reorient on) swings to the destination's own plane before arriving.
		fromUp = app->SpaceCamera->UpFrame * Vector3::up();
		toUp = app->AutoReorient() ? UnitySpaceApp::PlaneNormal(To) : Vector3::up();
		oortLy = UnitySpaceApp::OortCloudLy(To);
		// Through the gates by the planned course, if this system has a way; the old way across if not.
		ThroughGate = GateJump::TryPlan(app->Galaxy->Network, From, To, course);
		leg = 0;
		departure = ThroughGate ? (*course)[0] : nullptr;
		arrival = ThroughGate ? departure->Partner : nullptr;
		phase = GateJump::Phase::Approach;
		gateClearance = cs::Double::MaxValue;
		RefreshBanner();
		// What the destination is drawn with starts loading now, in the background (U.SPC-T414), so the most of it
		// is in memory before the view gets there.
		Rendering::AssetStreamer::WarmSystem(To);
	}

	float StarTravel::ReorientShare() const
	{
		double dist = (To->PositionLy - CameraLy).Length();
		double span = cs::Math::Max(oortLy - StandOffLy, 1e-6);
		return Smoother(Mathf::Clamp01(static_cast<float>((oortLy - dist) / span)));
	}

	double StarTravel::LeaveSpanKm(const cs::Ref<StarSystem>& s)
	{
		return s->Catalog->Root()->Name == "Sun" ? 400 * Astro::AuKm : cs::Math::Max(s->ExtentKm * 2.5, 40 * Astro::AuKm);
	}

	void StarTravel::Finish()
	{
		if (!Active) return;
		if (app->ActiveSystem != To) app->Activate(To);
		Views::BodyView::CoronaPresence = 1.0f;
		app->StreamFocus = std::nullopt;
		Active = false;
		Banner = cs::String();
		app->Interstellar->SetVisible(false);
		// Built part way across, the destination is still hidden.
		app->SetSystemVisible(true);
		app->FocusSystemOverview();
	}

	void StarTravel::Update(float dt)
	{
		if (!Active) return;
		if (ThroughGate)
		{
			UpdateGate(dt);
			return;
		}
		switch (Stage)
		{
			case Leg::Leave: UpdateLeave(dt); break;
			case Leg::Cross: UpdateCross(dt); break;
			default: UpdateArrive(dt); break;
		}
	}

	// ---------------------------------------------------------------- through the gates

	void StarTravel::UpdateGate(float dt)
	{
		if (phase == GateJump::Phase::Emerge)
		{
			UpdateArrive(dt);
			if (Active)
			{
				DVec3 exit, unusedAxis;
				double exitRing = 0;
				GateScene(arrival, exit, unusedAxis, exitRing);
				gateClearance = Clearance(exit, exitRing);
			}
			return;
		}
		if (phase == GateJump::Phase::Tunnel)
		{
			// Held at its height while what the view will see on the far side is still loading (U.SPC-T414).
			tunnel->Advance(dt, destinationBuilt && app->StreamingPending > 0);
			t = static_cast<float>(tunnel->T());
			UpdateTunnel();
			return;
		}
		t = Mathf::Min(1.0f, t + dt / static_cast<float>(GateJump::Seconds(phase)));
		switch (phase)
		{
			case GateJump::Phase::Approach: UpdateApproach(); break;
			default: UpdatePlunge(); break;
		}
	}

	void StarTravel::UpdateApproach()
	{
		DVec3 gate, axis;
		double ring = 0;
		GateScene(departure, gate, axis, ring);
		auto root = app->BodyScene(0);
		double lineUp = (gate - root).Length() + ring * GateJump::LineUpDiameters;
		auto dir = Slerp(leaveStart / leaveFrom, axis, static_cast<float>(GateJump::ApproachSwing(t)));
		double d = cs::Math::Exp(cs::Math::Log(leaveFrom) + (cs::Math::Log(lineUp) - cs::Math::Log(leaveFrom)) * GateJump::ApproachDescent(t));
		auto position = root + dir * d;
		auto look = Look(ToVector(gate - position), fromUp);
		app->SpaceCamera->SetPose(position, Quaternion::Slerp(startRotation, look, static_cast<float>(GateJump::ApproachLook(t))));
		gateClearance = Clearance(gate, ring);
		if (t < 1) return;
		phase = GateJump::Phase::Plunge;
		t = 0;
	}

	void StarTravel::UpdatePlunge()
	{
		DVec3 gate, axis;
		double ring = 0;
		GateScene(departure, gate, axis, ring);
		auto position = gate + axis * (ring * GateJump::LineUpDiameters * GateJump::PlungeRemaining(t));
		forward = ToVector(-axis);
		app->SpaceCamera->SetPose(position, Look(forward, fromUp));
		gateClearance = Clearance(gate, ring);
		if (t < 1) return;
		app->SetSystemVisible(false);
		gateClearance = cs::Double::MaxValue;
		// The next stop is the system this gate leads to, level with its own plane.
		toUp = app->AutoReorient() ? UnitySpaceApp::PlaneNormal(Hop()) : Vector3::up();
		phase = GateJump::Phase::Tunnel;
		Stage = Leg::Cross;
		t = 0;
		tunnel->Begin(GateJump::TunnelSeconds, GateJump::HoldAt, Core::Streaming::GateCoverMaxSeconds()->Value);
	}

	void StarTravel::UpdateTunnel()
	{
		app->SpaceCamera->SetRotation(Look(forward, Vector3::Slerp(fromUp, toUp, Smoother(t))));
		if (!destinationBuilt && t >= GateJump::BuildAt)
		{
			app->Activate(Hop());
			app->SetSystemVisible(false);
			destinationBuilt = true;
			// What will be seen coming out of the far gate streams in first.
			DVec3 exitAt, exitAxis;
			double unusedRing = 0;
			GateScene(arrival, exitAt, exitAxis, unusedRing);
			app->StreamFocus = exitAt;
			app->StreamFocusHeading = -exitAxis;
		}
		if (tunnel->Active) return;

		if (!destinationBuilt) app->Activate(Hop());
		destinationBuilt = true;
		app->StreamFocus = std::nullopt;
		app->SpaceCamera->SetUpFrame(Quaternion::FromToRotation(Vector3::up(), toUp));
		Views::BodyView::CoronaPresence = 1.0f;
		app->SetSystemVisible(true);
		// In the ring of the far gate, looking back through it at the destination's star, and carried out of it
		// and back from there to the overview.
		DVec3 exit, axis;
		double ring = 0;
		GateScene(arrival, exit, axis, ring);
		auto root = app->BodyScene(0);
		PrepareArrival(axis, cs::Math::Max((exit - root).Length(), 1e-9), 1.0f);
		crossStartRotation = Look(ToVector(-axis), toUp);
		app->SpaceCamera->SetPose(exit, crossStartRotation);
		gateClearance = Clearance(exit, ring);
		phase = GateJump::Phase::Emerge;
		Stage = Leg::Arrive;
		t = 0;
	}

	void StarTravel::GateScene(const cs::Ref<JumpGate>& gate, DVec3& scene, DVec3& axis, double& ring)
	{
		auto root = app->BodyScene(0);
		scene = app->Scale()->MapNear(app->Ephemeris(), app->BodyScenes(), app->Catalog()->Root()->Index, gate->PositionKm);
		auto off = scene - root;
		axis = off.LengthSquared() > 1e-24 ? off.Normalized() : gate->PositionKm.Normalized().SwapYZ();
		ring = app->Scale()->ShipLength(Views::GateView::DiameterKm);
	}

	double StarTravel::Clearance(DVec3 gate, double ring) const
	{
		return cs::Math::Max((app->SpaceCamera->ScenePosition - gate).Length() - ring * 0.5, ring * 0.1);
	}

	Quaternion StarTravel::Look(Vector3 lookForward, Vector3 up)
	{
		if (lookForward.sqrMagnitude() < 1e-12f) lookForward = Vector3::forward();
		if (Mathf::Abs(Vector3::Dot(lookForward.normalized(), up.normalized())) > 0.999f)
			up = Mathf::Abs(lookForward.normalized().y) < 0.9f ? Vector3::up() : Vector3::forward();
		return Quaternion::LookRotation(lookForward, up);
	}

	// ---------------------------------------------------------------- across the interstellar view

	void StarTravel::UpdateLeave(float dt)
	{
		t = Mathf::Min(1.0f, t + dt / LeaveSeconds);
		float e = Smoother(t);
		auto root = app->BodyScene(0);
		auto dir = leaveStart / leaveFrom;
		double d = cs::Math::Exp(cs::Math::Log(leaveFrom) + (cs::Math::Log(leaveTo) - cs::Math::Log(leaveFrom)) * e);
		auto look = Quaternion::LookRotation(ToVector(-dir), fromUp);
		app->SpaceCamera->SetPose(root + dir * d, Quaternion::Slerp(startRotation, look, Smoother(Mathf::Min(1.0f, t * 1.6f))));
		if (t < 1) return;

		// Hand over to the view between the stars, looking the same way at the same star.
		forward = app->SpaceCamera->Rotation * Vector3::forward();
		auto fwdEcliptic = DVec3(forward.x, forward.z, forward.y);
		crossStart = From->PositionLy - fwdEcliptic * StandOffLy;
		auto travel = (To->PositionLy - From->PositionLy).Normalized();
		crossEnd = To->PositionLy - travel * StandOffLy;
		crossStartRotation = app->SpaceCamera->Rotation;
		CameraLy = crossStart;
		app->Interstellar->SetVisible(true);
		app->SetSystemVisible(false);
		Stage = Leg::Cross;
		t = 0;
	}

	void StarTravel::UpdateCross(float dt)
	{
		t = Mathf::Min(1.0f, t + dt / CrossSeconds);
		auto travel = (To->PositionLy - From->PositionLy).Normalized();
		// Turn toward the destination first, then glide: rotation leads, position follows.
		float turn = Smoother(Mathf::Clamp01(t / 0.4f));
		float glide = Smoother(Mathf::Clamp01((t - 0.18f) / 0.82f));
		CameraLy = DVec3::Lerp(crossStart, crossEnd, glide);
		auto up = Vector3::Slerp(fromUp, toUp, ReorientShare());
		auto lookAtDest = Quaternion::LookRotation(ToVector(travel.SwapYZ()), up);
		auto rotation = Quaternion::Slerp(crossStartRotation, lookAtDest, turn);
		app->SpaceCamera->SetRotation(rotation);
		app->Interstellar->Update(CameraLy, rotation, 1.0f);
		// Half way across, while nothing of either system is on screen, the destination is built (hidden), so
		// nothing has to be made at the moment it comes into view.
		if (!destinationBuilt && t >= 0.5f)
		{
			app->Activate(To);
			app->SetSystemVisible(false);
			destinationBuilt = true;
		}
		if (t < 1) return;

		// Arrive: the destination takes over, the camera so far out along the line of travel that the whole system
		// is one point where its star was, and it opens out from there as the camera falls in.
		if (!destinationBuilt) app->Activate(To);
		// The view arrives level with the destination's plane (or the ecliptic with auto-reorient off).
		app->SpaceCamera->SetUpFrame(Quaternion::FromToRotation(Vector3::up(), toUp));
		Views::BodyView::CoronaPresence = ArrivalCorona;
		app->Interstellar->SetVisible(false);
		app->SetSystemVisible(true);
		double overview = app->OverviewDistance();
		double span = app->Scale()->LocalDistance(app->Catalog()->Root(), LeaveSpanKm(To));
		double from = cs::Math::Min(cs::Math::Max(cs::Math::Max(overview * 30, span), span * app->PixelsPerUnitAtUnitDistance() / PointPixels), MaxArriveDistance);
		PrepareArrival(-travel.SwapYZ(), from, ArrivalCorona);
		crossStartRotation = rotation;
		Stage = Leg::Arrive;
		t = 0;
	}

	void StarTravel::PrepareArrival(DVec3 direction, double from, float corona)
	{
		auto frame = Quaternion::FromToRotation(Vector3::up(), toUp);
		arriveDirection = direction;
		arriveTo = app->OverviewDistance();
		arriveFrom = from;
		arriveCorona = corona;
		// End where the overview focus will put us, so handing over is seamless: the same elevation, measured
		// from the destination's plane.
		auto local = Quaternion::Inverse(frame) * ToVector(arriveDirection);
		auto flat = Vector3(local.x, 0, local.z);
		if (flat.sqrMagnitude() < 1e-6f) flat = Vector3::back();
		flat.Normalize();
		float el = UnitySpaceApp::OverviewPitch * Mathf::Deg2Rad;
		auto endDir = frame * Vector3(flat.x * Mathf::Cos(el), Mathf::Sin(el), flat.z * Mathf::Cos(el));
		arriveEndRotation = Quaternion::LookRotation(-endDir, toUp);
		arriveEndDirection = DVec3(endDir.x, endDir.y, endDir.z);
	}

	void StarTravel::UpdateArrive(float dt)
	{
		t = Mathf::Min(1.0f, t + dt / (ThroughGate ? static_cast<float>(GateJump::EmergeSeconds) : ArriveSeconds));
		float e = Smoother(t);
		Views::BodyView::CoronaPresence = Mathf::Lerp(arriveCorona, 1.0f, e);
		auto root = app->BodyScene(0);
		auto dir = Slerp(arriveDirection, arriveEndDirection, e);
		double d = cs::Math::Exp(cs::Math::Log(arriveFrom) + (cs::Math::Log(arriveTo) - cs::Math::Log(arriveFrom)) * e);
		auto look = Look(ToVector(-dir), toUp);
		app->SpaceCamera->SetPose(root + dir * d, Quaternion::Slerp(crossStartRotation, look, Smoother(Mathf::Min(1.0f, t * 2.0f))));
		if (t < 1) return;
		// Out of a gate with more gates still to take: cross this system to the next one, on to the destination.
		if (ThroughGate && leg + 1 < course->Count())
		{
			BeginNextLeg();
			return;
		}
		Active = false;
		Banner = cs::String();
		app->FocusSystemOverview();
	}

	void StarTravel::BeginNextLeg()
	{
		leg++;
		departure = (*course)[leg];
		arrival = departure->Partner;
		auto root = app->BodyScene(0);
		leaveStart = app->SpaceCamera->ScenePosition - root;
		if (leaveStart.LengthSquared() < 1) leaveStart = DVec3(0, 0.4, -1);
		leaveFrom = leaveStart.Length();
		startRotation = app->SpaceCamera->Rotation;
		fromUp = toUp;
		destinationBuilt = false;
		phase = GateJump::Phase::Approach;
		Stage = Leg::Leave;
		t = 0;
		gateClearance = cs::Double::MaxValue;
		RefreshBanner();
		Rendering::AssetStreamer::WarmSystem(arrival->Home);
	}

	DVec3 StarTravel::Slerp(DVec3 a, DVec3 b, float share)
	{
		auto v = Vector3::Slerp(ToVector(a), ToVector(b), share);
		return DVec3(v.x, v.y, v.z);
	}
}
