using Unity.SpaceGod.Input;

// The slice of Unity's Input System that the UI classes read directly (MainMenu's Esc, ModelViewer's F9/Esc, right-drag
// orbit and wheel zoom): Keyboard.current / Mouse.current over the Input port's IInputDevice. The HUD actor sets
// InputSystemDevices.Source each frame; with none, current is null (as Unity's is with no keyboard).
namespace UnityEngine.InputSystem
{
    public static class InputSystemDevices
    {
        public static IInputDevice Source;
    }

    public sealed class ButtonControl
    {
        readonly System.Func<bool> held, pressed, released;

        internal ButtonControl(System.Func<bool> held, System.Func<bool> pressed, System.Func<bool> released)
        {
            this.held = held;
            this.pressed = pressed;
            this.released = released;
        }

        public bool isPressed => held();
        public bool wasPressedThisFrame => pressed();
        public bool wasReleasedThisFrame => released();
    }

    public sealed class Vector2Control
    {
        readonly System.Func<Vector2> read;
        internal Vector2Control(System.Func<Vector2> read) => this.read = read;
        public Vector2 ReadValue() => read();
    }

    public sealed class Keyboard
    {
        readonly IInputDevice d;
        Keyboard(IInputDevice d) => this.d = d;

        public static Keyboard current => InputSystemDevices.Source is { Present: true } s ? new Keyboard(s) : null;

        public ButtonControl this[Key key] => new ButtonControl(() => d.IsPressed(key), () => d.WasPressedThisFrame(key), () => false);

        public ButtonControl escapeKey => this[Key.Escape];
        public ButtonControl enterKey => this[Key.Enter];
        public ButtonControl spaceKey => this[Key.Space];
        public ButtonControl tabKey => this[Key.Tab];
        public ButtonControl upArrowKey => this[Key.UpArrow];
        public ButtonControl downArrowKey => this[Key.DownArrow];
        public ButtonControl leftArrowKey => this[Key.LeftArrow];
        public ButtonControl rightArrowKey => this[Key.RightArrow];
        public ButtonControl f1Key => this[Key.F1];
        public ButtonControl f9Key => this[Key.F9];
        public ButtonControl f12Key => this[Key.F12];
        public ButtonControl shiftKey => new ButtonControl(() => d.IsPressed(Key.LeftShift) || d.IsPressed(Key.RightShift), () => false, () => false);
    }

    public sealed class Mouse
    {
        readonly IInputDevice d;
        Mouse(IInputDevice d) => this.d = d;

        public static Mouse current => InputSystemDevices.Source is { Present: true } s ? new Mouse(s) : null;

        ButtonControl Button(MouseButton b) => new ButtonControl(() => d.IsPressed(b), () => d.WasPressedThisFrame(b), () => d.WasReleasedThisFrame(b));

        public ButtonControl leftButton => Button(MouseButton.Left);
        public ButtonControl rightButton => Button(MouseButton.Right);
        public ButtonControl middleButton => Button(MouseButton.Middle);
        public Vector2Control position => new Vector2Control(() => d.MousePosition);
        public Vector2Control delta => new Vector2Control(() => d.MouseDelta);
        public Vector2Control scroll => new Vector2Control(() => new Vector2(0, d.ScrollY));
    }
}
