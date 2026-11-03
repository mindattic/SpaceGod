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
    /// frame from its canvas): the display modes, through Unreal's GameUserSettings (resolution, window mode, VSync) and the adapter's
    /// fullscreen resolutions. Unreal reports no refresh rates: each mode is listed at 60 Hz.
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

        public static Resolution currentResolution
        {
            get
            {
                var s = Settings;
                if (s == null) return new Resolution { width = width, height = height, refreshRateRatio = Hz60 };
                var d = s.DesktopResolution;
                return new Resolution { width = d.X, height = d.Y, refreshRateRatio = Hz60 };
            }
        }

        public static Resolution[] resolutions
        {
            get
            {
                var list = new List<Resolution>();
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

        public static void SetResolution(int w, int h, FullScreenMode mode) => SetResolution(w, h, mode, Hz60);

        public static void SetResolution(int w, int h, FullScreenMode mode, RefreshRate preferredRefreshRate)
        {
            var s = Settings;
            if (s == null) return;
            s.ScreenResolution = new FIntPoint(w, h);
            s.SetFullscreenMode(mode == FullScreenMode.ExclusiveFullScreen ? EWindowMode.Fullscreen
                : mode == FullScreenMode.FullScreenWindow ? EWindowMode.WindowedFullscreen : EWindowMode.Windowed);
            s.ApplyResolutionSettings(false);
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
