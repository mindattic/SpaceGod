// Hud.Panels.inl: included INSIDE the Hud class body (UI/Hud.h). Port of Unity.SpaceGod's Runtime/UI/Hud.cs lines
// 1223-2382 (between the stars, opportunities, panels: top bar, navigator, selection card; US-T31). Defined in
// Hud.Panels.cpp; what these declarations need is in Hud.Panels.deps.h.

	// ---------------------------------------------------------------- between the stars

private:
	static constexpr Color WarpColor = Color(0.82f, 0.9f, 1.0f);
	static constexpr Color DustColor = Color(0.88f, 0.84f, 0.78f);
	static constexpr int WarpLineCount = 110, DustCount = 240;
	Vector2 warpCentre;

	/// <summary>
	/// Travelling between stars: warp lines stream out from the star ahead, long and fast at the height of the
	/// crossing, and stellar dust drifts past, slow near the centre and quickening toward the edges of the view.
	/// </summary>
	void DrawWarp();

	/// <summary>Above the status bar while riding a lane: the lane, how far along, the speed, the time to the off-ramp and its exits.</summary>
	void DrawLaneRide();

	static constexpr Color InterdictColor = Color(1.0f, 0.45f, 0.35f);

	/// <summary>
	/// A lane interdiction at the helm (U.SPC-T42): the field picked up ahead with its countdown and the answers to it
	/// (surge through, drop out), the drives knocked out after a pulse with the seconds until they come back, and, out
	/// of the lane near where the hull left it, the key to get back in.
	/// </summary>
	void DrawInterdiction();

	/// <summary>Where a direction (ecliptic, unit) vanishes on screen, if it is in front of the camera and on screen.</summary>
	bool DirectionToGui(Core::DVec3 direction, Vector2& p);

	/// <summary>An engine telegraph step as the helm calls it (U.SPC-T499).</summary>
	static cs::String TelegraphText(Core::EngineTelegraph t);

	/// <summary>The flight keys for the way the flown hull is steered (U.SPC-T269).</summary>
	static cs::String ControlHint(Core::PilotControl control);

public:
	/// <summary>
	/// The band of the screen the HUD's corner panels keep to (widescreen support, U.SPC-T289): the whole width up to
	/// 21:9, and on anything wider (32:9 at 5120 x 1440) a 21:9 band in the middle, so the readouts stay where the eye
	/// is rather than out at the far edges of the screen. The view itself still fills the whole width.
	/// </summary>
	static float SafeLeft();
	static float SafeRight();

private:
	static float SafeWidth();

	/// <summary>The widest aspect the HUD spreads across.</summary>
	static constexpr float MaxHudAspect = 21.0f / 9.0f;

	static constexpr Color CrosshairColor = Color(0.55f, 0.95f, 1.0f, 0.9f);

	/// <summary>The crosshair: four ticks about an open centre, so the target under it stays visible.</summary>
	void DrawCrosshair(Vector2 p, Color color);

	void DrawBoresight(Vector2 p, Color color);

	void DrawLine(Vector2 a, Vector2 b, Color color, float thickness);

	void DrawTargetMarker(const Target& t, bool selected);

	// ---------------------------------------------------------------- opportunities (U.SPC-T44)

	static Color OpportunityColor(const cs::Ref<Core::Opportunity>& op, float alpha = 1);

	/// <summary>Every open adventure is bracketed where it is, in its kind's colour, with its tag and how far it is.</summary>
	void DrawOpportunityMarkers();

	/// <summary>The open adventures near the player, most urgent first: tag, title, distance, time left and the hook.</summary>
	void DrawOpportunityBoard();

	cs::Ref<GUIStyle> opportunityHook;

	void DrawBrackets(Rect r, Color color, float thickness);

	void DrawOffscreenPointer(Core::DVec3 scene, const cs::String& name);

	void DrawIcon(const cs::Ref<Texture2D>& icon, Vector2 p, float size, Color tint);

	void DrawLabel(const cs::String& s, Vector2 p, Color color, bool minor);

	static Color BodyTint(const cs::Ref<Core::BodyDefinition>& b);

	// ---------------------------------------------------------------- panels

	/// One navigator row: the C# tuple (string name, string details, Target target, int indent, Color color).
	struct NavigatorRow
	{
		cs::String name;
		cs::String details;
		Target target;
		int indent = 0;
		Color color;
	};

	/// One line of the selection card: the C# tuple (string key, string value).
	struct SelectionLine
	{
		cs::String key;
		cs::String value;
	};

	/// AddSettlementLines' exports: the C# tuple (double value, string name).
	struct ExportShare
	{
		double value = 0;
		cs::String name;
	};

	/// AddSettlementLines' shortages: the C# tuple (double ratio, string name).
	struct Shortage
	{
		double ratio = 0;
		cs::String name;
	};

	Rect Panel(Rect r);

	void RegisterUi(Rect r);

	void DrawTopBar();

	void DrawNavigator();

	cs::Dictionary<cs::Ref<GUIStyle>, cs::Ref<GUIStyle>> textOnly = cs::Dictionary<cs::Ref<GUIStyle>, cs::Ref<GUIStyle>>();

	/// <summary><paramref name="style"/>'s text alone, left-aligned and unclipped, to draw over a row it has already drawn.</summary>
	cs::Ref<GUIStyle> TextOnly(const cs::Ref<GUIStyle>& style);

	static constexpr float MarqueeSpeed = 30, MarqueeHold = 1.5f, MarqueeGap = 40;

	/// <summary>
	/// One navigator line: <paramref name="name"/> stays put at the left, and <paramref name="details"/> (how far,
	/// how many gates, how old the news, what a ship is doing...) follow it; when they do not fit in what is left
	/// of the line they scroll past to the right of the name as a marquee, holding still at the start of each pass.
	/// </summary>
	void NameAndDetails(Rect r, const cs::String& name, const cs::String& details, const cs::Ref<GUIStyle>& style);

	void AddBody(cs::List<NavigatorRow>& rows, int index, int indent);

	static cs::Ref<Core::Ship> Flagship(const cs::Ref<Core::Battle>& b);

	static cs::String BattleScore(const cs::Ref<Core::Battle>& b);

	cs::String ShortStatus(const cs::Ref<Core::Ship>& ship);

	void DrawSelection();

	/// <summary>The tanks: how full, how many tonnes, and the delta-v that leaves at the ship's present mass.</summary>
	static cs::String PropellantText(const cs::Ref<Core::Ship>& ship);

	cs::String QueuePosition(const cs::Ref<Core::Ship>& ship);

	void StructureLines(const cs::Ref<Core::OrbitalStructure>& s, cs::List<SelectionLine>& lines, cs::String& heading, cs::String& sub, Color& headingColor);

	void RingLines(const cs::Ref<Core::DockingRing>& r, cs::List<SelectionLine>& lines, cs::String& heading, cs::String& sub, Color& headingColor);

	/// <summary>A trading company in a line: name, home port, hulls, cash, income, standing with its flag, or its fate.</summary>
	cs::String CompanyText(const cs::Ref<Core::Company>& co);

	/// <summary>
	/// A polity across the whole network (U.SPC-T91): what it is, its capital (and the system, when elsewhere), the
	/// systems it holds and its warships wherever they are; its treasury is the one on the line above.
	/// </summary>
	cs::String PolityText(const cs::Ref<Core::Faction>& f);

	void AddSettlementLines(cs::List<SelectionLine>& lines, const cs::Ref<Core::Settlement>& s);

	/// <summary>A found pirate haven is marked on its world's label (a hidden one is not, until found).</summary>
	cs::String HavenMark(const cs::Ref<Core::BodyDefinition>& body);

	static cs::String KindName(Core::SettlementKind k);
