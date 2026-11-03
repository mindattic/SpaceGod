// UnityEngine's Screen, Cursor and AudioListener (US-T26, the Runtime port's part of the shared Unity facade): what
// the app reads of the window and asks of the pointer and the master volume. The host (SpaceGameMode, Bridge/UnrealHost.cs)
// feeds Screen from the viewport each frame and applies Cursor and AudioListener to Unreal after the app's Update.
namespace UnityEngine
{
    /// <summary>Unity's Screen: the game viewport in pixels (1920x1080 until the host reads the viewport, and headless).</summary>
    public static partial class Screen
    {
        public static int width { get; private set; } = 1920;
        public static int height { get; private set; } = 1080;
        public static float dpi { get; set; } = 96;

        /// <summary>Set by the host from Unreal's viewport.</summary>
        public static void SetSize(int w, int h)
        {
            if (w > 0) width = w;
            if (h > 0) height = h;
        }
    }

    public enum CursorLockMode
    {
        None,
        Locked,
        Confined,
    }

    /// <summary>Unity's Cursor: what the game asks for; the host applies it to the player controller.</summary>
    public static class Cursor
    {
        public static CursorLockMode lockState { get; set; }
        public static bool visible { get; set; } = true;
    }

    /// <summary>Unity's AudioListener: the one master volume everything the game plays goes through (0-1).</summary>
    public static class AudioListener
    {
        public static float volume { get; set; } = 1f;
    }
}
