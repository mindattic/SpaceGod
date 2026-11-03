using System.Collections.Generic;
using UnityEngine;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;
using UnrealSharp.InputCore;

namespace Unity.SpaceGod.Input
{
    /// <summary>
    /// The keyboard and mouse through Unreal: raw key polling on the local <see cref="APlayerController"/>
    /// (<c>IsInputKeyDown</c>, <c>WasInputKeyJustPressed/Released</c>, <c>GetMousePosition</c>,
    /// <c>GetInputMouseDelta</c>, the <c>MouseWheelAxis</c> key's analog state).
    ///
    /// Why polling and not Enhanced Input: the original polls every key once a frame in one place
    /// (<see cref="InputState.Poll"/>) and resolves the shared keys itself (F is focus or flight assist, T reset time
    /// or lock target, B watch battle or boarding...). UnrealSharp exposes the player controller's polling API
    /// directly, while Enhanced Input from C# needs an InputAction and InputMappingContext asset per binding (editor
    /// content) plus delegate binding; polling keeps the bindings in code, in one table, exactly as the original's.
    ///
    /// Unity → Unreal key names: backquote is Tilde, quote is Apostrophe, minus is Hyphen, the numpad keys are
    /// Add/Subtract/Multiply/Divide, and Unreal has one Enter for the main and numpad keys.
    /// </summary>
    public sealed class UnrealInputDevice : IInputDevice
    {
        static readonly Dictionary<Key, FKey> Map = BuildMap();

        readonly APlayerController controller;
        Vector2 mousePosition;
        Vector2 mouseDelta;
        float scroll;

        public UnrealInputDevice(APlayerController controller) => this.controller = controller;

        public bool Present => controller != null && controller.IsValid();

        /// <summary>Unreal's mouse delta comes in input counts; pixels are what the original's sensitivities are tuned for.</summary>
        public static float MouseDeltaScale = 1f;

        /// <summary>Reads the pointer for this frame (call once, before the game's poll).</summary>
        public void BeginFrame()
        {
            if (!Present) return;
            if (controller.GetMousePosition(out float x, out float y)) mousePosition = new Vector2(x, Screen.height - y); // Unreal: origin top-left; Unity: bottom-left
            controller.GetInputMouseDelta(out float dx, out float dy);
            mouseDelta = new Vector2(dx, dy) * MouseDeltaScale; // both y up
            // Unreal's wheel axis is a notch a unit; Unity's raw wheel is 120 a notch (InputState scales it back).
            float notches = controller.GetInputAnalogKeyState(Keys.MouseWheelAxis);
            if (notches == 0f)
                notches = (controller.WasInputKeyJustPressed(Keys.MouseScrollUp) ? 1f : 0f) - (controller.WasInputKeyJustPressed(Keys.MouseScrollDown) ? 1f : 0f);
            scroll = notches * 120f;
        }

        public Vector2 MousePosition => mousePosition;
        public Vector2 MouseDelta => mouseDelta;
        public float ScrollY => scroll;

        public bool IsPressed(Key key) => Present && Map.TryGetValue(key, out var k) && controller.IsInputKeyDown(k);
        public bool WasPressedThisFrame(Key key) => Present && Map.TryGetValue(key, out var k) && controller.WasInputKeyJustPressed(k);
        public bool IsPressed(MouseButton button) => Present && controller.IsInputKeyDown(Of(button));
        public bool WasPressedThisFrame(MouseButton button) => Present && controller.WasInputKeyJustPressed(Of(button));
        public bool WasReleasedThisFrame(MouseButton button) => Present && controller.WasInputKeyJustReleased(Of(button));

        static FKey Of(MouseButton b) => b == MouseButton.Left ? Keys.LeftMouseButton : b == MouseButton.Right ? Keys.RightMouseButton : Keys.MiddleMouseButton;

        /// <summary>Applies the cursor the game asked for: locked (hidden, captured, the helm and right-drag look) or free.</summary>
        public void ApplyCursor(CursorLockMode mode, bool visible)
        {
            if (!Present) return;
            bool locked = mode == CursorLockMode.Locked;
            if (locked == appliedLocked && visible == appliedVisible) return;
            appliedLocked = locked;
            appliedVisible = visible;
            controller.ShowMouseCursor = visible;
            if (locked) UnrealSharp.UMG.WidgetLibrary.SetInputMode_GameOnly(controller);
            else UnrealSharp.UMG.WidgetLibrary.SetInputModeGameAndUI(controller, null, EMouseLockMode.DoNotLock, false);
        }

        bool? appliedLocked, appliedVisible;

        static Dictionary<Key, FKey> BuildMap()
        {
            var m = new Dictionary<Key, FKey>();
            for (var k = Key.A; k <= Key.Z; k++) m[k] = new FKey(k.ToString());
            string[] digits = { "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine" };
            for (int i = 0; i < 10; i++) m[Key.Digit0 + i] = new FKey(digits[i]);
            for (int i = 0; i < 12; i++) m[Key.F1 + i] = new FKey("F" + (i + 1));
            m[Key.Space] = Keys.SpaceBar;
            m[Key.Enter] = Keys.Enter;
            m[Key.NumpadEnter] = Keys.Enter; // Unreal reports the numpad's Enter as Enter
            m[Key.Escape] = Keys.Escape;
            m[Key.Tab] = Keys.Tab;
            m[Key.Backspace] = Keys.BackSpace;
            m[Key.Backquote] = Keys.Tilde;
            m[Key.Quote] = Keys.Apostrophe;
            m[Key.Semicolon] = Keys.Semicolon;
            m[Key.Comma] = Keys.Comma;
            m[Key.Period] = Keys.Period;
            m[Key.Slash] = Keys.Slash;
            m[Key.Backslash] = Keys.Backslash;
            m[Key.LeftBracket] = Keys.LeftBracket;
            m[Key.RightBracket] = Keys.RightBracket;
            m[Key.Minus] = Keys.Hyphen;
            m[Key.Equals] = Keys.Equals;
            m[Key.NumpadPlus] = Keys.Add;
            m[Key.NumpadMinus] = Keys.Subtract;
            m[Key.NumpadMultiply] = Keys.Multiply;
            m[Key.NumpadDivide] = Keys.Divide;
            m[Key.LeftShift] = Keys.LeftShift;
            m[Key.RightShift] = Keys.RightShift;
            m[Key.LeftCtrl] = Keys.LeftControl;
            m[Key.RightCtrl] = Keys.RightControl;
            m[Key.LeftAlt] = Keys.LeftAlt;
            m[Key.RightAlt] = Keys.RightAlt;
            m[Key.Insert] = Keys.Insert;
            m[Key.Delete] = Keys.Delete;
            m[Key.Home] = Keys.Home;
            m[Key.End] = Keys.End;
            m[Key.PageUp] = Keys.PageUp;
            m[Key.PageDown] = Keys.PageDown;
            m[Key.Pause] = Keys.Pause;
            m[Key.UpArrow] = new FKey("Up");
            m[Key.DownArrow] = new FKey("Down");
            m[Key.LeftArrow] = new FKey("Left");
            m[Key.RightArrow] = new FKey("Right");
            return m;
        }
    }
}
