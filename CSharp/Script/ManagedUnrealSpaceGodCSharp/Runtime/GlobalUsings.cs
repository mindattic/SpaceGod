// The Unity facade (namespace UnityEngine, ManagedUnrealSpaceGodCSharp/UnityCompat) is visible in every file of the
// port, as `using UnityEngine;` made it in each of the original's (US-T26). Files that touch Unreal types directly
// keep their own `using UnrealSharp.*;`.
global using UnityEngine;

// UnrealSharp's generated glue (obj/.../UnrealTypeDiscoveryGenerator) writes IntPtr unqualified, expecting implicit usings,
// which are off for this project (see the csproj): the System names it writes, without importing all of System.
global using IntPtr = System.IntPtr;
global using NotSupportedException = System.NotSupportedException;
