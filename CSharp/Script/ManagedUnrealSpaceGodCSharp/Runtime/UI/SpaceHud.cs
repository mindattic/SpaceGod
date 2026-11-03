using System;
using System.Collections.Generic;
using Unity.SpaceGod.Input;
using UnityEngine;
using UnityEngine.InputSystem;
using UnrealSharp.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.UI
{
    /// <summary>
    /// Where Unity.SpaceGod's IMGUI is drawn in Unreal: the game's AHUD. Each DrawHUD runs Unity's OnGUI cycle
    /// (<see cref="ImguiHost.Frame"/>: Layout, each input event, Layout, Repaint) over <see cref="Handlers"/> --
    /// every live MonoBehaviour's OnGUI (the app's HUD and pause menu, the main menu, the model viewer, the asteroid
    /// labels), as Unity calls them -- onto the Canvas (<see cref="HudCanvas"/>). Headless (-nullrhi) nothing is drawn, so
    /// the same cycle runs from Tick on a measuring canvas, which keeps the UI's logic and layout running (and lets the
    /// smoke run open every screen).
    ///
    /// Why Canvas (AHUD) and not UMG/Slate: the original is immediate-mode (OnGUI, GUI.Button returning true on the
    /// click), its layout is absolute rects in pixels computed every frame, and it draws its own textures, lines and
    /// labels; Canvas is Unreal's immediate-mode 2D API and UnrealSharp exposes all of it (DrawTexture with UVs, tint and
    /// rotation, DrawText with a font and scale, GetTextSize), so the original's code ports line for line on a thin
    /// IMGUI facade. UMG would mean rebuilding every screen as retained widgets and diffing state into them.
    /// </summary>
    [UClass]
    public partial class ASpaceHud : AHUD
    {
        /// <summary>The OnGUI handlers for a frame, in call order: every live MonoBehaviour's OnGUI (US-T27's UnityBridge
        /// lists them), then any added to <see cref="Extra"/>.</summary>
        public static IReadOnlyList<Action> Handlers()
        {
            var list = UnityBridge.GuiHandlers();
            list.AddRange(Extra);
            return list;
        }

        /// <summary>OnGUI handlers that are not MonoBehaviours.</summary>
        public static readonly List<Action> Extra = new List<Action>();

        /// <summary>Raised after each GUI frame (the smoke run steps through the screens on it).</summary>
        public static event Action<ASpaceHud> AfterFrame;

        /// <summary>The fonts and textures the GUI draws with, held here so Unreal's GC keeps them.</summary>
        [UProperty]
        public partial IList<UObject> Kept { get; set; }

        static readonly bool Headless = Bootstrap.HasCommandLineFlag("-nullrhi");

        /// <summary>-SpaceGodUiShots: the windowed screenshot tour (<see cref="UiSmoke.Shots"/>), stepped once a frame.</summary>
        static CaptureRunner.Coroutine shots = Bootstrap.HasCommandLineFlag("-SpaceGodUiShots") ? new CaptureRunner.Coroutine(UiSmoke.Shots()) : null;

        HudCanvas canvas;
        MeasuringCanvas measuring;
        UnrealInputDevice device;
        readonly GuiInput input = new GuiInput();

        /// <summary>Which font the canvas draws with (logged once).</summary>
        public string FontSource => canvas?.FontSource ?? "measuring canvas (headless)";

        public override void Tick(float deltaSeconds)
        {
            base.Tick(deltaSeconds);
            if (Headless) RunFrame(measuring ??= new MeasuringCanvas());
        }

        public override void ReceiveDrawHUD(int sizeX, int sizeY)
        {
            Screen.SetSize(sizeX, sizeY);
            if (canvas == null)
            {
                canvas = new HudCanvas(this, o => Kept.Add(o));
                Debug.Log("[Unity.SpaceGod] HUD canvas " + sizeX + "x" + sizeY + ", font " + canvas.FontSource);
            }
            RunFrame(canvas);
        }

        void RunFrame(IGuiCanvas target)
        {
            if (device == null && PlayerOwner != null) device = new UnrealInputDevice(PlayerOwner);
            IInputDevice d = device != null ? device : NoInputDevice.Instance;
            device?.BeginFrame();
            InputSystemDevices.Source = d;
            Fill(input, d);
            ImguiHost.Frame(target, input, Handlers(), Time.unscaledTime);
            AfterFrame?.Invoke(this);
            shots?.Step();
        }

        static readonly (Key Key, KeyCode Code, char Lower, char Upper)[] KeyTable = BuildKeys();

        /// <summary>The frame's keyboard and mouse as IMGUI wants them (GUI space: origin top-left).</summary>
        public static void Fill(GuiInput input, IInputDevice d)
        {
            input.Clear();
            input.Present = d.Present;
            if (!d.Present) return;
            var p = d.MousePosition;
            input.Mouse = new Vector2(p.x, Screen.height - p.y);
            for (int b = 0; b < 3; b++)
            {
                var mb = (MouseButton)b;
                input.Held[b] = d.IsPressed(mb);
                input.Pressed[b] = d.WasPressedThisFrame(mb);
                input.Released[b] = d.WasReleasedThisFrame(mb);
            }
            input.WheelNotches = d.ScrollY / 120f;
            bool shift = d.IsPressed(Key.LeftShift) || d.IsPressed(Key.RightShift);
            var m = EventModifiers.None;
            if (shift) m |= EventModifiers.Shift;
            if (d.IsPressed(Key.LeftCtrl) || d.IsPressed(Key.RightCtrl)) m |= EventModifiers.Control;
            if (d.IsPressed(Key.LeftAlt) || d.IsPressed(Key.RightAlt)) m |= EventModifiers.Alt;
            input.Modifiers = m;
            foreach (var (key, code, lower, upper) in KeyTable)
                if (d.WasPressedThisFrame(key)) input.KeysDown.Add((code, shift ? upper : lower));
        }

        static (Key, KeyCode, char, char)[] BuildKeys()
        {
            var t = new List<(Key, KeyCode, char, char)>();
            for (int i = 0; i < 26; i++) t.Add((Key.A + i, KeyCode.A + i, (char)('a' + i), (char)('A' + i)));
            const string shifted = ")!@#$%^&*(";
            for (int i = 0; i < 10; i++) t.Add((Key.Digit0 + i, KeyCode.Alpha0 + i, (char)('0' + i), shifted[i]));
            for (int i = 0; i < 12; i++) t.Add((Key.F1 + i, KeyCode.F1 + i, '\0', '\0'));
            t.Add((Key.Space, KeyCode.Space, ' ', ' '));
            t.Add((Key.Enter, KeyCode.Return, '\n', '\n'));
            t.Add((Key.NumpadEnter, KeyCode.KeypadEnter, '\n', '\n'));
            t.Add((Key.Escape, KeyCode.Escape, '\0', '\0'));
            t.Add((Key.Tab, KeyCode.Tab, '\t', '\t'));
            t.Add((Key.Backspace, KeyCode.Backspace, '\0', '\0'));
            t.Add((Key.Backquote, KeyCode.BackQuote, '`', '~'));
            t.Add((Key.Quote, KeyCode.Quote, '\'', '"'));
            t.Add((Key.Semicolon, KeyCode.Semicolon, ';', ':'));
            t.Add((Key.Comma, KeyCode.Comma, ',', '<'));
            t.Add((Key.Period, KeyCode.Period, '.', '>'));
            t.Add((Key.Slash, KeyCode.Slash, '/', '?'));
            t.Add((Key.Backslash, KeyCode.Backslash, '\\', '|'));
            t.Add((Key.LeftBracket, KeyCode.LeftBracket, '[', '{'));
            t.Add((Key.RightBracket, KeyCode.RightBracket, ']', '}'));
            t.Add((Key.Minus, KeyCode.Minus, '-', '_'));
            t.Add((Key.Equals, KeyCode.Equals, '=', '+'));
            t.Add((Key.NumpadPlus, KeyCode.KeypadPlus, '+', '+'));
            t.Add((Key.NumpadMinus, KeyCode.KeypadMinus, '-', '-'));
            t.Add((Key.NumpadMultiply, KeyCode.KeypadMultiply, '*', '*'));
            t.Add((Key.NumpadDivide, KeyCode.KeypadDivide, '/', '/'));
            t.Add((Key.Insert, KeyCode.Insert, '\0', '\0'));
            t.Add((Key.Delete, KeyCode.Delete, '\0', '\0'));
            t.Add((Key.Home, KeyCode.Home, '\0', '\0'));
            t.Add((Key.End, KeyCode.End, '\0', '\0'));
            t.Add((Key.PageUp, KeyCode.PageUp, '\0', '\0'));
            t.Add((Key.PageDown, KeyCode.PageDown, '\0', '\0'));
            t.Add((Key.UpArrow, KeyCode.UpArrow, '\0', '\0'));
            t.Add((Key.DownArrow, KeyCode.DownArrow, '\0', '\0'));
            t.Add((Key.LeftArrow, KeyCode.LeftArrow, '\0', '\0'));
            t.Add((Key.RightArrow, KeyCode.RightArrow, '\0', '\0'));
            return t.ToArray();
        }
    }
}
