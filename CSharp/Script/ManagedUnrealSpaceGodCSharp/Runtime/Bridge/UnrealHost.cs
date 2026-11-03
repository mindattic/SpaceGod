using System;
using Unity.SpaceGod.Input;
using UnrealSharp;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.Bridge
{
    /// <summary>
    /// What Unity did around the app each frame, done for Unreal (US-T26): the window size into Screen, the keyboard
    /// and mouse into the app's <see cref="InputState"/> (an <see cref="UnrealInputDevice"/> on the local player
    /// controller), and after the app's Update the camera (Camera.main's pose and lens onto an ACameraActor that is
    /// the player's view target), the cursor lock and the frame cap.
    ///
    /// The camera stays at Unreal's origin, as Unity's did: the app keeps every position in double-precision scene
    /// units and places everything camera-relative (floating origin), and the facade's UnrealSpace maps Unity axes
    /// (left-handed Y up) onto Unreal's (left-handed Z up) with UE(X, Y, Z) = Unity(x, −z, y) (SpaceGodRender's convention, a proper rotation), one unit a unit.
    /// </summary>
    public sealed class UnrealHost
    {
        readonly AActor owner;
        APlayerController controller;
        ACameraActor cameraActor;
        UnrealInputDevice device;
        float appliedNear = -1;
        int appliedFrameCap = int.MinValue;

        public UnrealHost(AActor owner) => this.owner = owner;

        /// <summary>The device the app reads; null headless (no local player).</summary>
        public UnrealInputDevice Device => device;

        /// <summary>A script stands in for the keyboard and mouse (the smoke run).</summary>
        public IInputDevice Override { get; set; }

        public void Begin()
        {
            controller = UGameplayStatics.GetPlayerController(0);
            if (controller != null && controller.IsValid())
            {
                device = new UnrealInputDevice(controller);
                cameraActor = UObject.SpawnActor<ACameraActor>(new TSubclassOf<ACameraActor>(), new FTransform(), ESpawnActorCollisionHandlingMethod.AlwaysSpawn, null, owner);
                if (cameraActor != null) controller.SetViewTargetWithBlend(cameraActor);
            }
        }

        /// <summary>Before the app's Update: the viewport and this frame's input.</summary>
        public void BeforeUpdate(UnitySpaceApp app)
        {
            if (controller != null && controller.IsValid())
            {
                controller.GetViewportSize(out int w, out int h);
                Screen.SetSize(w, h);
            }
            device?.BeginFrame();
            if (app != null) app.Input.Device = Override ?? (IInputDevice)device ?? NoInputDevice.Instance;
        }

        /// <summary>After the app's Update: camera, cursor, frame cap.</summary>
        public void AfterUpdate()
        {
            ApplyCamera();
            device?.ApplyCursor(Cursor.lockState, Cursor.visible);
            if (Application.targetFrameRate != appliedFrameCap)
            {
                appliedFrameCap = Application.targetFrameRate;
                SystemLibrary.ExecuteConsoleCommand("t.MaxFPS " + Math.Max(0, appliedFrameCap));
            }
        }

        void ApplyCamera()
        {
            var cam = Camera.main;
            if (cam == null || cameraActor == null || !cameraActor.IsValid()) return;
            // The camera never leaves the origin (floating origin); it turns and changes its lens.
            var rotation = UnrealSpace.ToUnreal(cam.transform.rotation);
            cameraActor.SetActorTransform(new FTransform(rotation, UnrealSpace.ToUnreal(cam.transform.position)));
            var component = cameraActor.CameraComponent;
            if (component != null)
            {
                // Unity's field of view is vertical, Unreal's horizontal.
                float aspect = Screen.height > 0 ? (float)Screen.width / Screen.height : 16f / 9f;
                component.FieldOfView = HorizontalFov(cam.fieldOfView, aspect);
                component.ConstrainAspectRatio = false;
            }
            // Unreal's near plane is global (reversed-Z, infinite far): set it when it has moved by more than 10%.
            float near = (float)(cam.nearClipPlane * UnrealSpace.UnitsPerUnity);
            if (appliedNear < 0 || Math.Abs(near - appliedNear) > appliedNear * 0.1f)
            {
                appliedNear = near;
                SystemLibrary.ExecuteConsoleCommand("r.SetNearClipPlane " + near.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
            }
        }

        /// <summary>A vertical field of view (Unity's) as the horizontal one (Unreal's) for a width/height aspect.</summary>
        public static float HorizontalFov(float verticalDeg, float aspect) =>
            (float)(2.0 * Math.Atan(Math.Tan(verticalDeg * 0.5 * Math.PI / 180.0) * aspect) * 180.0 / Math.PI);
    }
}
