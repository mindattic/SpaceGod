using UnityEngine;

namespace Unity.SpaceGod.UI
{
    // Unity's main menu is a scene of its own (MainMenu.unity: a camera, the starfield and this component). Unreal has
    // no such scene: Bootstrap's scene loader (US-T26) shows and hides the menu as one GameObject made here, which is
    // what loading and unloading the Unity scene did to it (Awake on load, OnDestroy on unload).
    public sealed partial class MainMenu
    {
        static GameObject shown;

        /// <summary>The menu exists in this port (Bootstrap opens it unless a flag skips it).</summary>
        public static bool Available => true;

        /// <summary>Whether the menu is up.</summary>
        public static bool Showing => shown != null;

        public static void Show()
        {
            if (shown != null) return;
            shown = new GameObject("MainMenu");
            var menu = shown.AddComponent<MainMenu>();
            menu.Awake();
        }

        public static void Hide()
        {
            if (shown == null) return;
            Object.Destroy(shown);
            shown = null;
        }
    }
}
