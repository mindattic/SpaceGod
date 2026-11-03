using UnrealSharp.CoreUObject;

namespace UnityEngine
{
    /// <summary>
    /// The one Unity -> Unreal coordinate helper of the C# track (US-T27; US-T26's Camera/Input use it too), on the
    /// convention both tracks and the SpaceGodRender shaders share (Shared/Plugins/SpaceGodRender/README.md, the C++
    /// track's SolSystem.cpp): Unreal (X, Y, Z) = Unity (x, -z, y), i.e. ecliptic -> Unity (x, z, y) and
    /// ecliptic -> Unreal (x, -y, z). Both engines are left-handed and the mapping is a proper rotation (a quarter turn
    /// about X), so lengths, angles and handedness survive: a quaternion maps by mapping its vector part the same way.
    /// One Unity unit is <see cref="UnitsPerUnity"/> Unreal units (1: the scene is camera-relative and in the game's own
    /// units, see ScaleModel).
    /// </summary>
    public static class UnrealSpace
    {
        /// <summary>Unreal units per Unity unit.</summary>
        public const double UnitsPerUnity = 1.0;

        /// <summary>
        /// Unity's front face is clockwise as seen by the viewer, Unreal's counter-clockwise (UKismetProceduralMeshLibrary
        /// GenerateBoxMesh's faces); the axis mapping is a rotation, so triangles are re-wound on upload.
        /// </summary>
        public const bool FlipWinding = true;

        public static FVector ToUnreal(Vector3 p) => new FVector(p.x * UnitsPerUnity, -p.z * UnitsPerUnity, p.y * UnitsPerUnity);

        /// <summary>A direction or normal (no unit scale).</summary>
        public static FVector DirectionToUnreal(Vector3 d) => new FVector(d.x, -d.z, d.y);

        /// <summary>A per-axis scale: Unity's (x, y, z) scale lies along Unreal's (X, Z, Y).</summary>
        public static FVector ScaleToUnreal(Vector3 s) => new FVector(s.x, s.z, s.y);

        public static FQuat ToUnreal(Quaternion q) => new FQuat { X = q.x, Y = -q.z, Z = q.y, W = q.w };

        public static Vector3 FromUnreal(FVector p) =>
            new Vector3((float)(p.X / UnitsPerUnity), (float)(p.Z / UnitsPerUnity), (float)(-p.Y / UnitsPerUnity));

        public static Vector3 DirectionFromUnreal(FVector d) => new Vector3((float)d.X, (float)d.Z, (float)-d.Y);

        public static Quaternion FromUnreal(FQuat q) => new Quaternion((float)q.X, (float)q.Z, (float)-q.Y, (float)q.W);

        public static FTransform ToUnreal(Vector3 position, Quaternion rotation, Vector3 scale) =>
            new FTransform(ToUnreal(rotation), ToUnreal(position), ScaleToUnreal(scale));

        /// <summary>
        /// A Unity camera's rotation as an Unreal camera's (or scene capture's). Unity cameras look along local +Z with +Y
        /// up; Unreal's along local +X with +Z up. Unity's (+Z, +X, +Y) map to Unreal's (-Y, +X, +Z), so the camera also
        /// turns a quarter yaw: its +X onto -Y.
        /// </summary>
        public static FQuat CameraRotationToUnreal(Quaternion q) => ToUnreal(q * CameraFix);

        /// <summary>The Unity-space rotation taking Unity's +X to +Z, +Y to +Y, +Z to -X: composed after the camera's own
        /// rotation, then mapped, it sends Unreal's camera +X (= Unity x) to where the Unity camera's +Z points.</summary>
        static readonly Quaternion CameraFix = Quaternion.Euler(0, -90f, 0);
    }
}
