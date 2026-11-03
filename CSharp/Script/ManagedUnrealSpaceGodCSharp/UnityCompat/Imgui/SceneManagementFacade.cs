// Unity's SceneManagement as the main menu's Asset Library page uses it: a candidate package's demo scene is "in the
// build" only in Unity (Asset Store packages); Unreal has none, so none is installed and none is loaded. The game's
// own two scenes go through GameLaunch (US-T26's SceneLoader), not here.
namespace UnityEngine.SceneManagement
{
    public static class SceneUtility
    {
        public static int GetBuildIndexByScenePath(string scenePath) => -1;
    }

    public static class SceneManager
    {
        public static void LoadScene(string sceneName) =>
            Debug.LogWarning("[Unity.SpaceGod] Scene '" + sceneName + "' is a Unity Asset Store demo; there is no such scene in Unreal.");
    }
}

namespace UnityEngine
{
    public enum RuntimeInitializeLoadType
    {
        AfterSceneLoad = 0,
        BeforeSceneLoad = 1,
        AfterAssembliesLoaded = 2,
        BeforeSplashScreen = 3,
        SubsystemRegistration = 4,
    }

    /// <summary>Unity's hook attribute; nothing scans for it here, the host calls the hooked method itself.</summary>
    [System.AttributeUsage(System.AttributeTargets.Method)]
    public sealed class RuntimeInitializeOnLoadMethodAttribute : System.Attribute
    {
        public RuntimeInitializeOnLoadMethodAttribute() { }
        public RuntimeInitializeOnLoadMethodAttribute(RuntimeInitializeLoadType loadType) { }
    }
}
