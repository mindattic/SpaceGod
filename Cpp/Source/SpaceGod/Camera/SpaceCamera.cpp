// Port of Unity.SpaceGod Runtime/Camera/SpaceCamera.cs (US-T29).

#include "Camera/SpaceCamera.h"
#include "Input/InputState.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Cameras
{
	const char* ToString(SpaceCamera::CameraMode v)
	{
		switch (v)
		{
			case SpaceCamera::CameraMode::Free: return "Free";
			case SpaceCamera::CameraMode::Travel: return "Travel";
			case SpaceCamera::CameraMode::Focus: return "Focus";
		}
		return "";
	}

	const cs::Ref<RangeSetting>& SpaceCamera::LookSensitivitySetting()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.lookSensitivity", SettingGroups::Controls,
			"Mouse look sensitivity", 1.0, 0.25, 4.0, 0.05,
			"How far the view (and a fighter's aim) turns for a mouse movement.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.00"), "x"); }), "mouse camera speed aim");
		return value;
	}

	const cs::Ref<ToggleSetting>& SpaceCamera::InvertLook()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("controls.invertLook", SettingGroups::Controls,
			"Invert vertical look", false, "Pushing the mouse forward looks (and pitches) down instead of up.", "mouse y pitch flight");
		return value;
	}

	float SpaceCamera::LookSensitivity() { return BaseLookSensitivity * static_cast<float>(LookSensitivitySetting()->Value); }

	float SpaceCamera::LookY(float dy) { return InvertLook()->Value ? -dy : dy; }

	SpaceCamera::SpaceCamera(ICameraWorld* inWorld)
		: world(inWorld)
	{
	}

	// ---------------------------------------------------------------- commands

	void SpaceCamera::Level(Vector3 planeNormal)
	{
		if (planeNormal.sqrMagnitude() < 1e-12f) planeNormal = Vector3::up();
		auto before = Rotation;
		UpFrame = Quaternion::FromToRotation(Vector3::up(), planeNormal.normalized());
		RederiveAngles();
		Rotation = ComposedRotation();
		levelOffset = before * Quaternion::Inverse(Rotation);
		levelT = 0.0f;
	}

	void SpaceCamera::SetUpFrame(Quaternion upFrame)
	{
		UpFrame = upFrame;
		levelT = 1.0f;
		RederiveAngles();
	}

	void SpaceCamera::RederiveAngles()
	{
		auto forward = ToLocal(Rotation * Vector3::forward());
		yaw = Mathf::Atan2(forward.x, forward.z) * Mathf::Rad2Deg;
		pitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(forward.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -89.0f, 89.0f);
		if (Mode == CameraMode::Travel)
		{
			// Mid-flight: re-express where the flight ends, not where the camera happens to be.
			auto look = -ToLocal(ToVector(travelEndDirection));
			orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
			orbitPitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -88.0f, 88.0f);
		}
		else if (Mode == CameraMode::Focus && !FocusTarget.IsNone())
		{
			auto offset = ScenePosition - OrbitCentre(FocusTarget);
			if (offset.LengthSquared() > 0)
			{
				auto look = -ToLocal(ToVector(offset.Normalized()));
				orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
				orbitPitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -88.0f, 88.0f);
			}
		}
	}

	Quaternion SpaceCamera::ComposedRotation() const
	{
		return Mode == CameraMode::Focus ? UpFrame * Quaternion::Euler(orbitPitch, orbitYaw, 0) : UpFrame * Quaternion::Euler(pitch, yaw, 0);
	}

	void SpaceCamera::PlaceFree(DVec3 scenePosition, float yawDeg, float pitchDeg)
	{
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		SetRotation(Quaternion::Euler(Mathf::Clamp(pitchDeg, -89.0f, 89.0f), yawDeg, 0));
		velocity = Vector3::zero();
		SetScenePosition(scenePosition);
	}

	void SpaceCamera::SetPose(DVec3 scenePosition, Quaternion rotation)
	{
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		SetRotation(rotation);
		velocity = Vector3::zero();
		SetScenePosition(scenePosition);
	}

	void SpaceCamera::SetRotation(Quaternion rotation)
	{
		Rotation = rotation;
		auto forward = ToLocal(rotation * Vector3::forward());
		yaw = Mathf::Atan2(forward.x, forward.z) * Mathf::Rad2Deg;
		pitch = Mathf::Clamp(-Mathf::Asin(Mathf::Clamp(forward.y, -1.0f, 1.0f)) * Mathf::Rad2Deg, -89.0f, 89.0f);
	}

	void SpaceCamera::FocusOn(const Target& target, double radii, cs::Nullable<float> pitchOverride)
	{
		if (target.IsNone()) return;
		auto targetScene = OrbitCentre(target);
		double r = cs::Math::Max(world->TargetRadius(target), 1e-9);
		auto offset = ScenePosition - targetScene;
		double distance = offset.Length();

		double framing = !cs::Double::IsNaN(radii) ? radii : DefaultFraming(target);
		double currentRadii = distance / r;
		if (cs::Double::IsNaN(radii) && Mode != CameraMode::Free && FocusTarget == target) framing = zoomTarget;
		else if (cs::Double::IsNaN(radii) && currentRadii > MinZoom(target) * 1.05 && currentRadii < framing) framing = currentRadii;

		// Work in the level frame: elevation is measured from its horizon.
		auto dir = distance > 0 ? ToLocal(ToVector(offset / distance)) : Vector3(0, 0.3f, -1).normalized();
		// Approach from the side we are already on, but never from straight over a pole.
		auto flat = Vector3(dir.x, 0, dir.z);
		float elevation = Mathf::Asin(Mathf::Clamp(dir.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
		elevation = pitchOverride.has_value() ? *pitchOverride : Mathf::Clamp(elevation, -55.0f, 55.0f);
		if (flat.sqrMagnitude() < 1e-8f) flat = Vector3::back();
		flat.Normalize();
		float e = elevation * Mathf::Deg2Rad;
		auto endLocal = Vector3(flat.x * Mathf::Cos(e), Mathf::Sin(e), flat.z * Mathf::Cos(e));
		auto endWorld = UpFrame * endLocal;

		travelStartOffset = distance > 0 ? offset : DVec3(endWorld.x, endWorld.y, endWorld.z) * r * framing * 3;
		travelStartRotation = Rotation;
		travelEndDirection = DVec3(endWorld.x, endWorld.y, endWorld.z);
		travelEndZoom = framing;
		travelT = 0;
		travelSeconds = TravelSeconds;

		// The orbit angles that reproduce endDir: offset = Euler(pitch, yaw) * back, in the level frame.
		auto look = -endLocal;
		orbitYaw = Mathf::Atan2(look.x, look.z) * Mathf::Rad2Deg;
		orbitPitch = -Mathf::Asin(Mathf::Clamp(look.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
		zoom = zoomTarget = framing;

		FocusTarget = target;
		Mode = CameraMode::Travel;
		velocity = Vector3::zero();
	}

	void SpaceCamera::ExitFocus()
	{
		if (Mode == CameraMode::Free) return;
		auto rotation = Rotation;
		Mode = CameraMode::Free;
		FocusTarget = Target::None();
		PilotLocked = false;
		PilotView = std::nullopt;
		PilotEye = nullptr;
		SetRotation(rotation);
		velocity = Vector3::zero();
		SetScenePosition(ScenePosition);
	}

	void SpaceCamera::EnterPilotChase(const Target& target, double radii)
	{
		FocusOn(target, radii);
		PilotLocked = true;
		PilotEye = nullptr;
	}

	double SpaceCamera::DefaultFraming(const Target& target)
	{
		return target.IsShip() || target.IsUnit() ? 5.0 : 3.4;
	}

	// ---------------------------------------------------------------- per frame

	void SpaceCamera::Update(float dt, const Input::InputState& input)
	{
		bool moving = input.Move.sqrMagnitude() > 0.01f;
		if (Mode != CameraMode::Free && moving && !PilotLocked) ExitFocus();

		switch (Mode)
		{
			case CameraMode::Travel:
				UpdateTravel(dt);
				break;
			case CameraMode::Focus:
				UpdateFocus(dt, input);
				break;
			default:
				UpdateFree(dt, input);
				break;
		}

		// Levelling: the old orientation's difference from the new one eases away.
		if (levelT < 1.0f)
		{
			levelT = Mathf::Min(1.0f, levelT + dt / LevelSeconds);
			Rotation = Quaternion::Slerp(levelOffset, Quaternion::identity(), SmootherStep(levelT)) * Rotation;
		}
	}

	void SpaceCamera::UpdateFree(float dt, const Input::InputState& input)
	{
		if (input.LookHeld)
		{
			yaw += input.LookDelta.x * LookSensitivity();
			pitch = Mathf::Clamp(pitch - LookY(input.LookDelta.y) * LookSensitivity(), -89.0f, 89.0f);
		}
		Rotation = UpFrame * Quaternion::Euler(pitch, yaw, 0);

		if (cs::Math::Abs(input.Scroll) > 0)
			SpeedMultiplier = cs::Math::Max(MinSpeedMultiplier, cs::Math::Min(MaxSpeedMultiplier, SpeedMultiplier * cs::Math::Pow(1.3, input.Scroll)));

		// Re-derive the position from the frame (the frame body may have moved or the scale changed).
		ScenePosition = FramePosition();

		double surface = cs::Math::Max(world->NearestSurface(ScenePosition), 1e-6);
		double speed = surface * 0.9 * SpeedMultiplier * (input.Boost ? 6 : 1) * (input.Slow ? 0.15 : 1);
		CurrentSpeed = speed;

		auto desired = Rotation * Vector3::ClampMagnitude(input.Move, 1.0f) * static_cast<float>(speed);
		float response = 1.0f - Mathf::Exp(-dt * 7.0f);
		velocity = Vector3::Lerp(velocity, desired, response);
		if (input.Move.sqrMagnitude() < 0.01f && velocity.magnitude() < speed * 0.002) velocity = Vector3::zero();

		auto delta = DVec3(velocity.x, velocity.y, velocity.z) * dt;
		if (delta.LengthSquared() > 0) ApplySceneDelta(delta);

		ScenePosition = FramePosition();
		if (PushOutOfBodies()) velocity *= 0.3f;
		UpdateFrame();
		ScenePosition = FramePosition();
	}

	void SpaceCamera::UpdateFocus(float dt, const Input::InputState& input)
	{
		if (PilotLocked)
		{
			auto shipRotation = world->TargetRotation(FocusTarget);
			auto wantRotation = PilotView.value_or(shipRotation);
			if (!PilotView.has_value() && !PilotLockTarget.IsNone())
			{
				auto toTarget = world->TargetScene(PilotLockTarget) - world->TargetScene(FocusTarget);
				if (toTarget.LengthSquared() > 1e-12)
				{
					auto lookAt = Quaternion::LookRotation(ToVector(toTarget.Normalized()), shipRotation * Vector3::up());
					wantRotation = Quaternion::Slerp(shipRotation, lookAt, PilotLockTrackStrength);
				}
			}
			// Eases toward the hull's facing (and any lock-on blend) rather than welding to it
			// instantly, so sharp turns read as a chase rather than a rigid 1:1 attachment.
			// A view the mouse sets (free aim, a fighter's seat, a gun, a capital's look-around) is the mouse's own: it
			// turns with it at once and the hull catches up after it (US-T129).
			if (PilotView.has_value()) Rotation = *PilotView;
			else Rotation = Quaternion::Slerp(Rotation, wantRotation, 1.0f - Mathf::Exp(-dt * PilotCamResponseHz));
			if (cs::Math::Abs(input.Scroll) > 0)
				zoomTarget = cs::Math::Max(MinZoom(FocusTarget), cs::Math::Min(20.0, zoomTarget * cs::Math::Pow(0.82, input.Scroll)));
			zoom = cs::Math::Exp(cs::Math::Log(zoom) + (cs::Math::Log(zoomTarget) - cs::Math::Log(zoom)) * (1 - cs::Math::Exp(-dt * 10)));
			auto chaseBack = Rotation * Vector3::back();
			double chaseR = world->TargetRadius(FocusTarget);
			ScenePosition = world->TargetScene(FocusTarget) + DVec3(chaseBack.x, chaseBack.y, chaseBack.z) * (zoom * chaseR);
			if (PilotView.has_value())
			{
				auto lift = Rotation * Vector3::up();
				ScenePosition += DVec3(lift.x, lift.y, lift.z) * (zoom * chaseR * PilotViewLift);
			}
			// At a gun mount the eye is the mount's own, not a chase (U.SPC-T445).
			if (PilotEye) ScenePosition = PilotEye();
			CurrentSpeed = 0;
			return;
		}
		if (input.LookHeld)
		{
			orbitYaw += input.LookDelta.x * LookSensitivity();
			orbitPitch = Mathf::Clamp(orbitPitch - LookY(input.LookDelta.y) * LookSensitivity(), -88.0f, 88.0f);
		}
		if (cs::Math::Abs(input.Scroll) > 0)
			zoomTarget = cs::Math::Max(MinZoom(FocusTarget), cs::Math::Min(5e7, zoomTarget * cs::Math::Pow(0.82, input.Scroll)));
		zoom = cs::Math::Exp(cs::Math::Log(zoom) + (cs::Math::Log(zoomTarget) - cs::Math::Log(zoom)) * (1 - cs::Math::Exp(-dt * 10)));

		Rotation = UpFrame * Quaternion::Euler(orbitPitch, orbitYaw, 0);
		auto back = Rotation * Vector3::back();
		double r = world->TargetRadius(FocusTarget);
		ScenePosition = OrbitCentre(FocusTarget) + DVec3(back.x, back.y, back.z) * (zoom * r);
		CurrentSpeed = 0;
	}

	void SpaceCamera::UpdateTravel(float dt)
	{
		travelT = Mathf::Min(1.0f, travelT + dt / travelSeconds);
		float e = SmootherStep(travelT);

		auto target = OrbitCentre(FocusTarget);
		double r = world->TargetRadius(FocusTarget);
		double startDist = cs::Math::Max(travelStartOffset.Length(), 1e-9);
		double endDist = travelEndZoom * r;
		double dist = cs::Math::Exp(cs::Math::Log(startDist) + (cs::Math::Log(endDist) - cs::Math::Log(startDist)) * e);

		auto startDir = (travelStartOffset / startDist);
		Vector3 dir;
		Quaternion lookAt;
		if (PilotLocked)
		{
			// Settles in behind the hull's own facing from the start (U.SPC-T370), not round whatever side
			// the camera already happened to be on: a chase has no "approach from the side we're on", only
			// the hull's own forward, which the player may already be turning under their own input while
			// this plays out. Read live (not just at the moment the chase began) so a turn mid-transition
			// is followed rather than fought.
			auto shipRotation = world->TargetRotation(FocusTarget);
			auto endDirV = shipRotation * Vector3::back();
			dir = Vector3::Slerp(ToVector(startDir), endDirV, e).normalized();
			lookAt = PilotEye && PilotView.has_value() ? *PilotView : shipRotation;
		}
		else
		{
			dir = Vector3::Slerp(ToVector(startDir), ToVector(travelEndDirection), e).normalized();
			lookAt = Quaternion::LookRotation(-dir, Up());
		}
		ScenePosition = target + DVec3(dir.x, dir.y, dir.z) * dist;
		// Gliding to a gun mount (U.SPC-T445): the glide ends at the mount's eye, not behind the hull.
		if (PilotLocked && PilotEye)
		{
			auto eye = PilotEye();
			ScenePosition = ScenePosition + (eye - ScenePosition) * e;
		}
		Rotation = Quaternion::Slerp(travelStartRotation, lookAt, SmootherStep(Mathf::Min(1.0f, travelT * 1.8f)));

		if (travelT >= 1.0f)
		{
			Mode = CameraMode::Focus;
			zoom = zoomTarget = travelEndZoom;
		}
	}

	// ---------------------------------------------------------------- reference frames

	DVec3 SpaceCamera::FramePosition()
	{
		auto body = (*world->Catalog())[frame];
		return world->BodyScene(frame) + world->Scale()->LocalOffset(body, trueOffset);
	}

	void SpaceCamera::ApplySceneDelta(DVec3 delta)
	{
		auto body = (*world->Catalog())[frame];
		const auto& scale = world->Scale();
		double d = trueOffset.Length();
		if (d <= 0)
		{
			trueOffset = delta.SwapYZ() * (body->RadiusKm / cs::Math::Max(scale->DisplayRadius(body), 1e-12));
			return;
		}
		double sceneDist = scale->LocalDistance(body, d);
		auto dirScene = trueOffset.SwapYZ() / d;
		double radial = DVec3::Dot(delta, dirScene);
		auto tangential = delta - dirScene * radial;
		double derivative = cs::Math::Max(scale->LocalDistanceDerivative(body, d), 1e-15);
		auto trueDeltaScene = dirScene * (radial / derivative) + tangential * (d / sceneDist);
		trueOffset += trueDeltaScene.SwapYZ();

		double minimum = body->RadiusKm * 1.002;
		if (trueOffset.Length() < minimum) trueOffset = trueOffset.Normalized() * minimum;
	}

	void SpaceCamera::SetScenePosition(DVec3 scenePosition)
	{
		int f = FrameAt(scenePosition);
		frame = f;
		trueOffset = world->Scale()->InverseLocalOffset((*world->Catalog())[f], scenePosition - world->BodyScene(f));
		ScenePosition = scenePosition;
	}

	int SpaceCamera::FrameAt(DVec3 scenePosition)
	{
		int f = 0;
		while (true)
		{
			int best = -1;
			double bestRatio = 1.0;
			for (int child : world->Catalog()->ChildrenOf(f))
			{
				double soi = world->Scale()->SceneSoi((*world->Catalog())[child]);
				double ratio = (scenePosition - world->BodyScene(child)).Length() / soi;
				if (ratio < bestRatio)
				{
					bestRatio = ratio;
					best = child;
				}
			}
			if (best < 0) break;
			f = best;
		}
		return f;
	}

	void SpaceCamera::UpdateFrame()
	{
		auto pos = FramePosition();
		if (frame != 0)
		{
			auto body = (*world->Catalog())[frame];
			if ((pos - world->BodyScene(frame)).Length() > world->Scale()->SceneSoi(body) * 1.08)
			{
				int parent = body->ParentIndex;
				frame = parent;
				trueOffset = world->Scale()->InverseLocalOffset((*world->Catalog())[parent], pos - world->BodyScene(parent));
				return;
			}
		}
		for (int child : world->Catalog()->ChildrenOf(frame))
		{
			auto c = (*world->Catalog())[child];
			if ((pos - world->BodyScene(child)).Length() < world->Scale()->SceneSoi(c) * 0.92)
			{
				frame = child;
				trueOffset = world->Scale()->InverseLocalOffset(c, pos - world->BodyScene(child));
				return;
			}
		}
	}

	bool SpaceCamera::PushOutOfBodies()
	{
		auto pos = ScenePosition;
		bool pushed = false;
		for (int i = 0; i < world->Catalog()->Count(); i++)
		{
			auto c = world->BodyScene(i);
			double minimum = world->Scale()->DisplayRadius((*world->Catalog())[i]) * 1.003;
			auto d = pos - c;
			double len = d.Length();
			if (len < minimum)
			{
				pos = c + (len > 0 ? d / len : DVec3::UnitY) * minimum;
				pushed = true;
			}
		}
		if (pushed) SetScenePosition(pos);
		return pushed;
	}
}
