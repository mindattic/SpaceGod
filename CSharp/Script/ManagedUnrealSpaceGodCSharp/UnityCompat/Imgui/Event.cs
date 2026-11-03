namespace UnityEngine
{
    /// <summary>
    /// Unity's IMGUI event: the one being handled by this OnGUI pass. <see cref="mousePosition"/> is local to the
    /// innermost group / scroll view / clip, as Unity's is, and far off-screen when the pointer is outside that
    /// clip, so nothing hidden by a scroll view reacts to it.
    /// </summary>
    public sealed class Event
    {
        public static Event current { get; internal set; } = new Event { type = EventType.Layout };

        public EventType type;
        public EventType rawType;
        public int button;
        public int clickCount;
        public Vector2 delta;
        public KeyCode keyCode;
        public char character;
        public EventModifiers modifiers;
        public string commandName = "";

        /// <summary>Where the pointer is in GUI screen space (pixels, origin top-left).</summary>
        internal Vector2 screenMouse;

        public Vector2 mousePosition
        {
            get => GUI.ToLocalPoint(screenMouse);
            set => screenMouse = GUI.ToScreenPoint(value);
        }

        public bool shift => (modifiers & EventModifiers.Shift) != 0;
        public bool control => (modifiers & EventModifiers.Control) != 0;
        public bool alt => (modifiers & EventModifiers.Alt) != 0;
        public bool command => (modifiers & EventModifiers.Command) != 0;
        public bool isMouse => rawType == EventType.MouseDown || rawType == EventType.MouseUp || rawType == EventType.MouseMove || rawType == EventType.MouseDrag;
        public bool isKey => rawType == EventType.KeyDown || rawType == EventType.KeyUp;
        public bool isScrollWheel => rawType == EventType.ScrollWheel;

        public void Use()
        {
            if (type == EventType.Repaint || type == EventType.Layout) return; // Unity warns and ignores this
            type = EventType.Used;
        }

        /// <summary>The type as a control sees it: input is ignored while the GUI is disabled.</summary>
        public EventType GetTypeForControl(int controlID)
        {
            if (type == EventType.Repaint || type == EventType.Layout || type == EventType.Used) return type;
            if (!GUI.enabled) return EventType.Ignore;
            if (GUIUtility.hotControl != 0 && GUIUtility.hotControl != controlID && isMouse && type != EventType.MouseDown) return EventType.Ignore;
            return type;
        }

        internal Event Clone() => (Event)MemberwiseClone();
    }
}
