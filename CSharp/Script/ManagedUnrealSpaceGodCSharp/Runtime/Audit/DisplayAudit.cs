using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using Unity.SpaceGod.Core;
using Unity.SpaceGod.UI;
using UnityEngine;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.Audit
{
    /// <summary>
    /// <see cref="FeatureAudit"/>'s display probes: render.msaa as the original's MSAA (US-T55) and the display modes with
    /// their real refresh rates, the exclusive-fullscreen rate asked for as the original's DisplayOptions.Apply does
    /// (US-T56). The C++ track's Smoke/FeatureAuditDisplay.cpp runs the same probes with the same ids and detail text.
    /// </summary>
    public static class DisplayAudit
    {
        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        static int CVar(string name)
        {
            try { return SystemLibrary.GetConsoleVariableIntValue(name); }
            catch { return -1; }
        }

        /// <summary>
        /// render.msaa off then on: the forward renderer is on (MSAA needs it), off is no anti-aliasing and one-pixel
        /// lines, on is 4x MSAA (the original's URP asset, m_MSAA: 4) and Direct3D's 1.4-pixel MSAA lines.
        /// </summary>
        public static void Msaa(Action<string, bool, string> result, Action<int> frames)
        {
            var msaa = EngineSettings.Msaa;
            bool before = msaa.Value;
            bool wasDefault = msaa.IsDefault;
            int forward = CVar("r.ForwardShading");
            msaa.Set(false);
            frames(1);
            int offMethod = CVar("r.AntiAliasingMethod"), offCount = CVar("r.MSAACount");
            float offWidth = ShaderBinding.LineWidthPx;
            msaa.Set(true);
            frames(1);
            int onMethod = CVar("r.AntiAliasingMethod"), onCount = CVar("r.MSAACount");
            float onWidth = ShaderBinding.LineWidthPx;
            if (wasDefault) msaa.Reset(); else msaa.Set(before);
            frames(1);
            result("render.msaa", forward == 1 && offMethod == 0 && offCount == 1 && offWidth == 1f && onMethod == 3 && onCount == 4 && onWidth == 1.4f,
                string.Format(Inv, "forward shading {0}; off: AA method {1}, {2} sample, lines {3:F1} px; on: AA method {4}, {5} samples, lines {6:F1} px",
                    forward, offMethod, offCount, offWidth, onMethod, onCount, onWidth));
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DEVMODEW
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public ushort dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
            public uint dmFields;
            public int dmPositionX, dmPositionY;
            public uint dmDisplayOrientation, dmDisplayFixedOutput;
            public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public ushort dmLogPixels;
            public uint dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
            public uint dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern bool EnumDisplaySettingsW(string? deviceName, int modeNum, ref DEVMODEW devMode);

        /// <summary>Windows' own list for the primary display (whole Hz), the yardstick DXGI's rates are checked against.</summary>
        static Dictionary<(int, int), int> WindowsBestRates()
        {
            var best = new Dictionary<(int, int), int>();
            if (!OperatingSystem.IsWindows()) return best;
            var dm = new DEVMODEW { dmSize = (ushort)Marshal.SizeOf<DEVMODEW>() };
            for (int i = 0; EnumDisplaySettingsW(null, i, ref dm); i++)
            {
                var key = ((int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
                if (!best.TryGetValue(key, out int hz) || dm.dmDisplayFrequency > hz) best[key] = (int)dm.dmDisplayFrequency;
            }
            return best;
        }

        /// <summary>
        /// The Display page's list (DisplayOptions.Resolutions, the original's code) carries each size's best real rate:
        /// it must match what Windows lists for that size (to the whole Hz Windows truncates to), not 60 everywhere. Then
        /// Apply in exclusive fullscreen at the largest size asks Screen.SetResolution for that size's best rate, as the
        /// original's TryFindMode does, and is put back.
        /// </summary>
        public static void Modes(Action<string, bool, string> result, Action<int> frames)
        {
            var raw = Screen.resolutions;
            var page = DisplayOptions.Resolutions();
            var windows = WindowsBestRates();
            var sizes = new HashSet<(int, int)>(raw.Select(r => (r.width, r.height)));
            var real = page.Where(r => sizes.Contains((r.Width, r.Height))).ToList();
            int matching = real.Count(r => windows.TryGetValue((r.Width, r.Height), out int hz) && Math.Abs(r.RefreshHz - hz) < 1.0);
            var rates = raw.Select(r => Math.Round(r.refreshRateRatio.value)).Distinct().OrderBy(v => v).ToList();
            var largest = real.OrderBy(r => (long)r.Width * r.Height).LastOrDefault();
            var exact = raw.Where(r => r.width == largest.Width && r.height == largest.Height).OrderBy(r => r.refreshRateRatio.value).LastOrDefault();
            result("display.modes", real.Count > 0 && matching == real.Count,
                string.Format(Inv, "{0} modes, {1} sizes on the page, {2} of them at Windows' best rate; rates {3} Hz; largest {4} ({5}/{6})",
                    raw.Length, real.Count, matching, string.Join(", ", rates), largest, exact.refreshRateRatio.numerator, exact.refreshRateRatio.denominator));

            var was = DisplayOptions.Current;
            DisplayOptions.Apply(new DisplaySettings(largest.Width, largest.Height, DisplayMode.Exclusive, was.VSync));
            frames(1);
            var asked = Screen.requestedRefreshRate;
            DisplayOptions.Apply(was);
            frames(1);
            result("display.exclusive-rate", largest.Width > 0 && asked.numerator == exact.refreshRateRatio.numerator && asked.denominator == exact.refreshRateRatio.denominator
                    && Screen.requestedRefreshRate.value == 0,
                string.Format(Inv, "Exclusive {0} x {1}: asked {2}/{3} Hz ({4:F3}); back to {5}: {6:F3} Hz asked",
                    largest.Width, largest.Height, asked.numerator, asked.denominator, asked.value, was, Screen.requestedRefreshRate.value));
        }
    }
}
