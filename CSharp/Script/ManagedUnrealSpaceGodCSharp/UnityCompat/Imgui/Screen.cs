using System;
using System.Collections.Generic;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace UnityEngine
{
    public enum FullScreenMode
    {
        ExclusiveFullScreen = 0,
        FullScreenWindow = 1,
        MaximizedWindow = 2,
        Windowed = 3,
    }

    public struct RefreshRate
    {
        public uint numerator, denominator;
        public double value => denominator == 0 ? 0 : (double)numerator / denominator;
    }

    public struct Resolution
    {
        public int width, height;
        public RefreshRate refreshRateRatio;
        public int refreshRate => (int)Math.Round(refreshRateRatio.value);
        public override string ToString() => $"{width} x {height} @ {refreshRateRatio.value}Hz";
    }

    /// <summary>
    /// Unity's Screen, its display-mode half (the viewport size is US-T26's UnityScreen.cs; the HUD sets it every
    /// frame from its canvas): the display modes, through Unreal's GameUserSettings (resolution, window mode, VSync) and the
    /// monitor's own mode list with each mode's refresh rate (DXGI, through the primary module, US-T56).
    /// </summary>
    public static partial class Screen
    {
        // width, height, dpi and SetSize are US-T26's (Runtime/UnityCompat/UnityScreen.cs); these are the display modes.

        static UGameUserSettings Settings
        {
            get
            {
                try { return UGameUserSettings.GameUserSettings; }
                catch { return null; }
            }
        }

        public static FullScreenMode fullScreenMode
        {
            get
            {
                var s = Settings;
                if (s == null) return FullScreenMode.Windowed;
                switch (s.GetFullscreenMode())
                {
                    case EWindowMode.Fullscreen: return FullScreenMode.ExclusiveFullScreen;
                    case EWindowMode.WindowedFullscreen: return FullScreenMode.FullScreenWindow;
                    default: return FullScreenMode.Windowed;
                }
            }
            set => SetResolution(width, height, value);
        }

        public static bool fullScreen => fullScreenMode != FullScreenMode.Windowed;

        static RefreshRate Hz60 => new RefreshRate { numerator = 60, denominator = 1 };

        /// <summary>US-T56: the primary module's sg.QueryDisplayModes asks DXGI for the monitor's modes (each with its exact
        /// rate, as Unity's refreshRateRatio) and leaves them in sg.DisplayModes / sg.CurrentDisplayMode, "W H NUM DEN" each.</summary>
        static List<Resolution> QueryModes(string variable)
        {
            var list = new List<Resolution>();
            try
            {
                SystemLibrary.ExecuteConsoleCommand("sg.QueryDisplayModes");
                foreach (var mode in SystemLibrary.GetConsoleVariableStringValue(variable).Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    var f = mode.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (f.Length == 4 && int.TryParse(f[0], out int w) && int.TryParse(f[1], out int h) && uint.TryParse(f[2], out uint n) && uint.TryParse(f[3], out uint d))
                        list.Add(new Resolution { width = w, height = h, refreshRateRatio = new RefreshRate { numerator = n, denominator = d } });
                }
            }
            catch
            {
                // no engine to ask
            }
            return list;
        }

        /// <summary>The monitor's desktop mode and its refresh rate (US-T56; the game user settings' desktop size when
        /// the monitor cannot be asked).</summary>
        public static Resolution currentResolution
        {
            get
            {
                var now = QueryModes("sg.CurrentDisplayMode");
                if (now.Count > 0) return now[0];
                var s = Settings;
                if (s == null) return new Resolution { width = width, height = height, refreshRateRatio = Hz60 };
                var d = s.DesktopResolution;
                return new Resolution { width = d.X, height = d.Y, refreshRateRatio = Hz60 };
            }
        }

        /// <summary>Every mode of the monitor the game is on, each at each of its real refresh rates (US-T56), as Unity
        /// lists them; Unreal's own list (no rates, each at 60 Hz) only where DXGI cannot be asked.</summary>
        public static Resolution[] resolutions
        {
            get
            {
                var list = QueryModes("sg.DisplayModes");
                if (list.Count > 0) return list.ToArray();
                try
                {
                    if (SystemLibrary.GetSupportedFullscreenResolutions(out var modes))
                        foreach (var m in modes) list.Add(new Resolution { width = m.X, height = m.Y, refreshRateRatio = Hz60 });
                }
                catch
                {
                    // headless: no adapter to ask
                }
                return list.ToArray();
            }
        }

        /// <summary>The last rate an exclusive-fullscreen SetResolution asked for (0 Hz: none), for the audit (US-T56).</summary>
        public static RefreshRate requestedRefreshRate { get; private set; }

        public static void SetResolution(int w, int h, FullScreenMode mode) => SetResolution(w, h, mode, default(RefreshRate));

        public static void SetResolution(int w, int h, FullScreenMode mode, RefreshRate preferredRefreshRate)
        {
            var s = Settings;
            if (s == null) return;
            s.ScreenResolution = new FIntPoint(w, h);
            s.SetFullscreenMode(mode == FullScreenMode.ExclusiveFullScreen ? EWindowMode.Fullscreen
                : mode == FullScreenMode.FullScreenWindow ? EWindowMode.WindowedFullscreen : EWindowMode.Windowed);
            s.ApplyResolutionSettings(false);
            // US-T56: as Unity's, an exclusive-fullscreen mode switches the monitor to the rate asked for (Unreal's swap
            // chain goes fullscreen at "any" rate; the primary module sets this one once the window is fullscreen).
            bool exclusive = mode == FullScreenMode.ExclusiveFullScreen && preferredRefreshRate.value > 0;
            requestedRefreshRate = exclusive ? preferredRefreshRate : default;
            try
            {
                SystemLibrary.ExecuteConsoleCommand(exclusive
                    ? $"sg.SetFullscreenRefresh {w} {h} {preferredRefreshRate.numerator} {preferredRefreshRate.denominator}"
                    : "sg.SetFullscreenRefresh 0");
            }
            catch
            {
                // no engine to ask
            }
        }

        public static void SetResolution(int w, int h, bool fullscreen) =>
            SetResolution(w, h, fullscreen ? FullScreenMode.FullScreenWindow : FullScreenMode.Windowed);
    }

    /// <summary>Unity's QualitySettings, as far as the display options use it: VSync, as Unreal's.</summary>
    public static class QualitySettings
    {
        static int vSync = 1;

        public static int vSyncCount
        {
            get => vSync;
            set
            {
                vSync = value;
                try
                {
                    var s = UGameUserSettings.GameUserSettings;
                    if (s == null) return;
                    s.VSyncEnabled = value > 0;
                    s.ApplyNonResolutionSettings();
                }
                catch
                {
                    // headless
                }
            }
        }
    }
}
