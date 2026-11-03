using System;
using System.Collections.Generic;

namespace UnityEngine
{
    /// <summary>One frame of keyboard and mouse as the GUI needs it, filled by the host (the HUD actor from its player
    /// controller; the smoke run by script). Mouse in GUI space: pixels, origin top-left.</summary>
    public sealed class GuiInput
    {
        public bool Present;
        public Vector2 Mouse;
        public readonly bool[] Held = new bool[3], Pressed = new bool[3], Released = new bool[3];
        /// <summary>Wheel notches this frame, positive away from the user (up).</summary>
        public float WheelNotches;
        public EventModifiers Modifiers;
        /// <summary>Keys that went down this frame, with the character each types (or '\0').</summary>
        public readonly List<(KeyCode Code, char Character)> KeysDown = new List<(KeyCode, char)>();

        public void Clear()
        {
            for (int i = 0; i < 3; i++) Pressed[i] = Released[i] = false;
            WheelNotches = 0;
            KeysDown.Clear();
        }
    }

    /// <summary>
    /// Runs Unity's OnGUI cycle once a frame: turns the frame's input into IMGUI events -- MouseMove / MouseDrag,
    /// MouseDown / MouseUp (with Unity's click count), ScrollWheel (3 a notch, down positive, as Unity's delta.y),
    /// KeyDown with its character -- and for each, as Unity does, calls every OnGUI handler with a Layout event and
    /// then the event; last, Layout and Repaint. Only Repaint draws.
    /// </summary>
    public static class ImguiHost
    {
        /// <summary>What the frame drew, and what its controls did (the smoke run logs it).</summary>
        public struct DrawStats
        {
            public int Labels, Boxes, Buttons, Textures, Texts, Clicks, Passes;

            public override string ToString() =>
                $"{Labels} labels, {Boxes} boxes, {Buttons} buttons, {Textures} texture quads, {Texts} text runs ({Passes} OnGUI passes)";
        }

        public static DrawStats Stats;

        /// <summary>Where each enabled button with text was drawn in the last Repaint (screen space, cut to its clip),
        /// so a scripted run can click it by its label.</summary>
        public static readonly Dictionary<string, Rect> Buttons = new Dictionary<string, Rect>();

        internal static void NoteButton(string text, Rect screen)
        {
            if (screen.width > 0 && screen.height > 0 && !Buttons.ContainsKey(text)) Buttons[text] = screen;
        }

        /// <summary>Set by a text field that took a click this event (any other click drops keyboard focus).</summary>
        internal static bool FocusTaken;

        static Vector2 lastMouse = new Vector2(float.NaN, float.NaN);
        static float lastClickTime = -10f;
        static Vector2 lastClickAt;
        static int clickCount;

        /// <summary>One frame of GUI for <paramref name="handlers"/> (each an OnGUI), in that order.</summary>
        public static void Frame(IGuiCanvas canvas, GuiInput input, IReadOnlyList<Action> handlers, float time)
        {
            var events = Collect(input, time);
            Stats = default;
            foreach (var e in events) Dispatch(canvas, e, handlers);
            Buttons.Clear();
            Dispatch(canvas, Make(EventType.Repaint, input), handlers);
        }

        static void Dispatch(IGuiCanvas canvas, Event e, IReadOnlyList<Action> handlers)
        {
            var layout = e.Clone();
            layout.type = layout.rawType = EventType.Layout;
            Run(canvas, layout, handlers);
            FocusTaken = false;
            Run(canvas, e, handlers);
            if (e.rawType == EventType.MouseDown && !FocusTaken) GUIUtility.keyboardControl = 0;
            // A press released where nothing took it: let go, so no control stays hot.
            if (e.rawType == EventType.MouseUp) GUIUtility.hotControl = 0;
        }

        static void Run(IGuiCanvas canvas, Event e, IReadOnlyList<Action> handlers)
        {
            for (int i = 0; i < handlers.Count; i++)
            {
                Event.current = e;
                GUI.BeginPass(canvas);
                try
                {
                    handlers[i]();
                }
                catch (ExitGUIException)
                {
                }
                finally
                {
                    GUI.EndPass();
                }
                Stats.Passes++;
            }
        }

        static Event Make(EventType type, GuiInput input) => new Event
        {
            type = type,
            rawType = type,
            modifiers = input.Modifiers,
            screenMouse = input.Present ? input.Mouse : new Vector2(-100000, -100000),
        };

        static List<Event> Collect(GuiInput input, float time)
        {
            var list = new List<Event>();
            if (!input.Present) return list;
            var mouse = input.Mouse;
            bool anyHeld = input.Held[0] || input.Held[1] || input.Held[2];
            if (!float.IsNaN(lastMouse.x) && (mouse.x != lastMouse.x || mouse.y != lastMouse.y))
            {
                var move = Make(anyHeld ? EventType.MouseDrag : EventType.MouseMove, input);
                move.delta = mouse - lastMouse;
                move.button = input.Held[0] ? 0 : input.Held[1] ? 1 : input.Held[2] ? 2 : 0;
                list.Add(move);
            }
            lastMouse = mouse;

            for (int b = 0; b < 3; b++)
            {
                if (input.Pressed[b])
                {
                    clickCount = time - lastClickTime < 0.5f && (mouse - lastClickAt).magnitude < 5 ? clickCount + 1 : 1;
                    lastClickTime = time;
                    lastClickAt = mouse;
                    var down = Make(EventType.MouseDown, input);
                    down.button = b;
                    down.clickCount = clickCount;
                    list.Add(down);
                }
                if (input.Released[b])
                {
                    var up = Make(EventType.MouseUp, input);
                    up.button = b;
                    up.clickCount = clickCount;
                    list.Add(up);
                }
            }

            if (input.WheelNotches != 0)
            {
                var wheel = Make(EventType.ScrollWheel, input);
                wheel.delta = new Vector2(0, -input.WheelNotches * 3f);
                list.Add(wheel);
            }

            foreach (var (code, ch) in input.KeysDown)
            {
                var k = Make(EventType.KeyDown, input);
                k.keyCode = code;
                k.character = ch;
                list.Add(k);
            }
            return list;
        }
    }
}
