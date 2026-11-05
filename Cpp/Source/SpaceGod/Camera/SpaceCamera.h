#pragma once
// Port of Unity.SpaceGod Runtime/Camera/SpaceCamera.cs (US-T29).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "Target.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class ScaleModel;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod::Input
{
	class InputState;
}

namespace SpaceGod::Cameras
{
	/// <summary>What the camera needs to know about the world.</summary>
	class ICameraWorld
	{
	public:
		virtual ~ICameraWorld() = default;

		virtual const cs::Ref<Core::BodyCatalog>& Catalog() const = 0;
		virtual const cs::Ref<Core::ScaleModel>& Scale() const = 0;
		virtual Core::DVec3 BodyScene(int body) = 0;
		virtual Core::DVec3 TargetScene(const Target& target) = 0;

		/// <summary>Where focus mode (F) orbits and looks: the middle of the target's model as drawn, which for a hull
		/// is not its root (US-T78). TargetScene unless the world knows better.</summary>
		virtual Core::DVec3 FocusScene(const Target& target) { return TargetScene(target); }

		/// <summary>A target's framing radius in scene units (body radius, or half a ship's length).</summary>
		virtual double TargetRadius(const Target& target) = 0;

		/// <summary>Distance from a scene point to the nearest surface (body or ship hull), scene units.</summary>
		virtual double NearestSurface(Core::DVec3 scenePosition) = 0;

		/// <summary>A target's own facing, scene (Unity) axes; identity when it has none.</summary>
		virtual Quaternion TargetRotation(const Target& target) = 0;
	};

	/// <summary>
	/// Homeworld 3 style space camera.
	///
	///  - Free: swim like Quake. WASD along the view, Q/E (Space/C) up and down, hold the right
	///    mouse button to look. Speed scales with distance to the nearest surface, so the same keys
	///    cross an AU in deep space and creep along a hull up close.
	///  - Focus (F): fly to the selected body or ship and orbit it; right-drag orbits, the wheel
	///    zooms, and the camera rides along with the target as it moves. Any movement key drops
	///    straight back into free swim from where you are.
	///
	/// Free-camera position is stored as a true (km) offset from a reference body, the body whose
	/// sphere of influence you are in, so the camera co-moves with whatever you are near under any
	/// time warp, and keeps its place relative to it when the scale is toggled.
	///
	/// "Up" is a frame of its own (<see cref="UpFrame"/>): the ecliptic in Sol, but any plane can be
	/// made level (Alpha Centauri's A-B orbit lies steeply across the ecliptic). Yaw, pitch and the
	/// focus orbit are measured in that frame; <see cref="Level"/> swings to a new one smoothly.
	/// </summary>
	// C#: [SettingsSource] -- its settings (LookSensitivitySetting, InvertLook, in that order) register when their
	// accessors are first called; GameSettingsHost touches both.
	class SpaceCamera final
	{
	public:
		enum class CameraMode : int
		{
			Free,
			Travel,
			Focus,
		};

		CameraMode Mode = CameraMode::Free;
		Core::DVec3 ScenePosition;
		Quaternion Rotation = Quaternion::identity();
		Target FocusTarget = Target::None();
		int FrameBody() const { return frame; }

		/// <summary>Which way is up: Unity's +Y turned onto the plane the view keeps level.</summary>
		Quaternion UpFrame = Quaternion::identity();

		/// <summary>True while swinging round to a new level.</summary>
		bool Levelling() const { return levelT < 1.0f; }

		/// <summary>Current swim speed in scene units per second (for the HUD).</summary>
		double CurrentSpeed = 0;

		double SpeedMultiplier = 1.0;

		/// <summary>True while piloting: <see cref="Focus"/> rides a fixed distance behind the target and
		/// matches its own facing instead of taking right-drag orbit input, which steers the ship instead.</summary>
		bool PilotLocked = false;

		/// <summary>While piloting: a ship the camera leans toward keeping framed, without taking the
		/// piloted ship's own facing away from the player (see the <see cref="PilotLocked"/> branch of
		/// <see cref="UpdateFocus"/>). <see cref="Target.None"/> when nothing is locked.</summary>
		Target PilotLockTarget = Target::None();

		/// <summary>While piloting: where the view looks when it is not simply the hull's own facing (U.SPC-T269)
		/// -- along a light fighter's crosshair, or swung about a capital ship by the mouse. Null follows the hull.</summary>
		cs::Nullable<Quaternion> PilotView;

		/// <summary>While piloting: where the eye itself sits when it is not a chase behind the hull (U.SPC-T445) -- at a
		/// manned gun mount, looking down its barrel along <see cref="PilotView"/>. Read each frame, so the eye rides the
		/// hull as it moves; null keeps the chase. Cleared whenever a new chase begins or the focus is let go.</summary>
		cs::Func<Core::DVec3> PilotEye;

	private:
		/// <summary>How fast the view catches up to <see cref="PilotView"/>: quick, since in free aim the
		/// crosshair is the middle of the screen and must stay under the mouse.</summary>
		static constexpr float PilotViewResponseHz = 30.0f;

		/// <summary>How far above the line of sight a free-aim chase sits, in chase distances, so the
		/// hull flies below the crosshair instead of covering it.</summary>
		static constexpr double PilotViewLift = 0.22;

		static constexpr float TravelSeconds = 1.6f;
		static constexpr float LevelSeconds = 1.4f;
		/// <summary>Mouse look, in degrees a count at the default sensitivity; the free, focus and pilot views alike.</summary>
		static constexpr float BaseLookSensitivity = 0.12f;

	public:
		/// <summary>Controls (U.SPC-T431): how far the mouse turns the view, as a multiple of the usual.</summary>
		static const cs::Ref<Core::RangeSetting>& LookSensitivitySetting();

		/// <summary>Controls (U.SPC-T431): mouse forward looks down, as in a flight stick.</summary>
		static const cs::Ref<Core::ToggleSetting>& InvertLook();

		/// <summary>Degrees of turn per mouse count, as the player has set it.</summary>
		static float LookSensitivity();

		/// <summary>A mouse movement's up-down part, the way the player wants it read.</summary>
		static float LookY(float dy);

	private:
		static constexpr double MinSpeedMultiplier = 1.0 / 64, MaxSpeedMultiplier = 64;

		/// <summary>How fast the pilot chase camera's rotation catches up to the hull's own facing
		/// (and, with a lock, the blended look-at-target facing) -- high enough to feel tight behind
		/// the ship, low enough that sharp turns read as a chase rather than a rigid weld.</summary>
		static constexpr float PilotCamResponseHz = 12.0f;

		/// <summary>How far the camera leans from the ship's own facing toward looking at a locked
		/// target (0 = ignores the lock, 1 = looks straight at it regardless of the ship's facing).</summary>
		static constexpr float PilotLockTrackStrength = 0.6f;

		/// The world outlives the camera (the app owns both).
		ICameraWorld* const world;

		int frame = 0;
		Core::DVec3 trueOffset;
		float yaw = 0, pitch = 0;
		Vector3 velocity;

		float orbitYaw = 0, orbitPitch = 0;
		double zoom = 4, zoomTarget = 4;

		Core::DVec3 travelStartOffset;
		Quaternion travelStartRotation;
		Core::DVec3 travelEndDirection;
		double travelEndZoom = 0;
		float travelT = 0;

		// The rotation the view had before levelling, relative to where it is now, eased away.
		Quaternion levelOffset = Quaternion::identity();
		float levelT = 1.0f;

	public:
		explicit SpaceCamera(ICameraWorld* inWorld);

	private:
		Vector3 Up() const { return UpFrame * Vector3::up(); }

		/// <summary>A world direction expressed in the level frame.</summary>
		Vector3 ToLocal(Vector3 v) const { return Quaternion::Inverse(UpFrame) * v; }

		// ---------------------------------------------------------------- commands

	public:
		/// <summary>
		/// Swings the view round, over a second and a half, so that the plane with normal
		/// <paramref name="planeNormal"/> (Unity axes) is level: the camera stays where it is and keeps
		/// looking the same way as nearly as it can, losing only its roll against the new horizon.
		/// </summary>
		void Level(Vector3 planeNormal);

		/// <summary>Takes a new level frame at once (switching systems, say), keeping the view as it is.</summary>
		// C#'s parameter is `frame`; renamed since it would shadow the field (UE builds treat shadowing as an error).
		void SetUpFrame(Quaternion upFrame);

	private:
		/// <summary>Yaw/pitch (free) and orbit angles (focus) from the current view, in the current frame.</summary>
		void RederiveAngles();

		Quaternion ComposedRotation() const;

	public:
		/// <summary>Places the free camera at a scene position looking along yaw/pitch (world axes).</summary>
		void PlaceFree(Core::DVec3 scenePosition, float yawDeg, float pitchDeg);

		/// <summary>Puts the free camera at a scene position with a rotation (driven from outside, as by star travel).</summary>
		void SetPose(Core::DVec3 scenePosition, Quaternion rotation);

		void SetRotation(Quaternion rotation);

		/// <summary>Flies to <paramref name="target"/> and enters focus. <paramref name="radii"/> overrides the framing distance.</summary>
		void FocusOn(const Target& target, double radii = cs::Double::NaN, cs::Nullable<float> pitchOverride = std::nullopt);

		void ExitFocus();

		/// <summary>Settles into a close chase a fixed distance behind a piloted ship: its own facing
		/// drives the view, so right-drag is free for flight input instead of orbiting.</summary>
		void EnterPilotChase(const Target& target, double radii);

		static double DefaultFraming(const Target& target);

	private:
		static double MinZoom(const Target& target) { return target.IsShip() || target.IsUnit() ? 1.6 : 1.12; }

		// ---------------------------------------------------------------- per frame

	public:
		void Update(float dt, const Input::InputState& input);

	private:
		void UpdateFree(float dt, const Input::InputState& input);

		void UpdateFocus(float dt, const Input::InputState& input);

		void UpdateTravel(float dt);

		static float SmootherStep(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		static Vector3 ToVector(Core::DVec3 v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }

		/// <summary>The point focus mode orbits and looks at (US-T78): the model's drawn middle (ICameraWorld::FocusScene);
		/// the pilot's chase keeps to the hull's own root, as the helm flies it.</summary>
		Core::DVec3 OrbitCentre(const Target& target) { return PilotLocked ? world->TargetScene(target) : world->FocusScene(target); }

		// ---------------------------------------------------------------- reference frames

		Core::DVec3 FramePosition();

		/// <summary>Moves by a scene-space delta, converted to true km through the local scale mapping's Jacobian.</summary>
		void ApplySceneDelta(Core::DVec3 delta);

	public:
		/// <summary>Re-anchors to whichever body's sphere of influence contains the scene point, keeping the scene position.</summary>
		void SetScenePosition(Core::DVec3 scenePosition);

		/// <summary>The innermost body whose sphere of influence contains the scene point (the root if none does).
		/// <see cref="FrameBody"/> is only kept up to date while flying free; this holds in any mode.</summary>
		int FrameAt(Core::DVec3 scenePosition);

	private:
		/// <summary>Frame hysteresis: enter a child's SOI below 92% of it, leave ours above 108%.</summary>
		void UpdateFrame();

		bool PushOutOfBodies();
	};

	const char* ToString(SpaceCamera::CameraMode v);
}
