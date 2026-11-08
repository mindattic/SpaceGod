// Hud.Boards.inl -- included INSIDE class Hud (see UI/Hud.h).
// Port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 2383-3745 (bounty board, contracts, fleet, market board, outfitting,
// spawn, cargo hold, news feed, status bars, salvage and boarding panels, seats and the take-charge picker); bodies in
// UI/Hud.Boards.cpp. A C# null string is the empty string here.

private:
	// ---------------------------------------------------------------- bounty board

	/// <summary>
	/// The bounty board (F4) of the port in view -- the selected world's biggest, else the one the piloted ship is
	/// alongside: everyone its owner has a bounty on (outlaws, and the player when wanted), the price, the record,
	/// where each was last seen and who is hunting them (see <see cref="Outlaws.BoardAt"/>).
	/// </summary>
	void DrawBounties();

	// ---------------------------------------------------------------- contracts board (U.SPC-T101)

	/// <summary>
	/// The contracts board (F7) of the port in view -- the selected world's biggest, else the one the piloted ship is
	/// alongside: the jobs it posts from what the system needs (freight, passengers and colonists, escorts, patrols,
	/// surveys, bounties), best paid first, with Take while the ship is alongside; and the player's own contracts,
	/// with Abandon (which costs standing with the patron). See <see cref="Contracts"/>.
	/// </summary>
	void DrawContracts();

	// ---------------------------------------------------------------- the player's fleet (U.SPC-T102)

	Vector2 fleetScroll;

	/// <summary>
	/// The player's fleet and property (F10): the house account and its books, every owned ship with its captain,
	/// standing orders and running profit, and what else the house owns. At the port in view (the selected world's
	/// biggest, else the one the piloted ship is alongside) it buys hulls, a warehouse or a share; on a selected
	/// world it stakes a claim. A ship's Orders button steps through what it can be told to do, using the selected
	/// world (a route or patrol to it) or the selected ship (an escort).
	/// </summary>
	void DrawFleet();

	/// <summary>Steps a ship to the next standing orders it can carry out: hold, a trade route between where it is and the
	/// selected world, mining the house's first claim it can work, escorting the selected (or flown) ship, a patrol.</summary>
	cs::Ref<Core::StandingOrder> NextOrders(const cs::Ref<Core::OwnedShip>& o, const Target& sel);

	// ---------------------------------------------------------------- market board

	void DrawMarket();

	/// <summary>The player's standing with a port's owner, and what it does to the prices there.</summary>
	static cs::String StandingText(const cs::Ref<Core::LawRecord>& r, const cs::Ref<Core::Faction>& owner);

	void ClickBuy(const cs::Ref<Core::Settlement>& market, Core::Good good);

	void ClickRefuel(const cs::Ref<Core::Settlement>& market);

	void ClickSell(const cs::Ref<Core::Settlement>& market, Core::Good good);

	// ---------------------------------------------------------------- outfitting (U.SPC-T65)

	/// <summary>
	/// The yard's outfitting panel: the piloted hull's hardpoints and what is on each, then its facilities inside the
	/// hull and the internal space left (U.SPC-T470). For a chosen hardpoint every module that fits it, for a fitted
	/// facility stripping it, and for the free space every facility the hull has room for, each with its price at
	/// this yard's market (materials at its prices, labour, less what the old part fetches). Prisoners in the brig
	/// are handed over here too, at any port the ship may dock at.
	/// </summary>
	void DrawOutfit();

	static cs::String Sections(double n);

	static cs::String Megawatts(double mw);

	/// <summary>What a facility takes (U.SPC-T521): its sections, and the reactor power and crew it needs, if any.</summary>
	static cs::String FacilityCost(const cs::Ref<Core::FacilityKind>& k, double size);

	/// C# tuple `(ModuleKind? module, FacilityKind facility, string name)`: one row of what the chosen row offers.
	struct OutfitChoice
	{
		cs::Nullable<Core::ModuleKind> module;
		cs::Ref<Core::FacilityKind> facility;
		cs::String name;
	};

	/// <summary>What the chosen row offers: the modules a hardpoint takes, stripping a fitted facility, or the
	/// facilities the free space has room for.</summary>
	static cs::List<OutfitChoice> OutfitOptions(const cs::Ref<Core::Loadout>& loadout, int rowIndex); // C#: int row (renamed: it would shadow the style field row)

	void OutfitOption(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& yard, const cs::Ref<Core::Loadout>& loadout,
		const OutfitChoice& option, float x, float y, float w, float rowH);

	// ---------------------------------------------------------------- the port screen (US-T266, UI/Hud.Port.cpp)

	/// <summary>The player's price factor at <paramref name="port"/> (their standing with its owner), 1 with no law.</summary>
	double PortPriceFactor(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& port);

	/// <summary>At a helm alongside a port, a banner offering Shift+M to request docking; docked with the screen closed, one to open it.</summary>
	void DrawDockPrompt();

	/// <summary>
	/// The port screen (Shift+M, docked): the port, its owner and the player's standing there, Undock, and one strip of tabs
	/// for everything it offers -- Market, Outfitting (at a yard), Repair &amp; Rearm, Contracts, Bounties, Fleet &amp; property,
	/// Trade desk, Crew, News. A tab for an existing panel opens that panel; Repair &amp; Rearm and Crew are drawn here.
	/// </summary>
	void DrawPort();

	/// <summary>Repair &amp; Rearm (<see cref="Core::PortRepairs"/>): a quote per item with its parts and price, and Repair all.</summary>
	void DrawPortRepairs(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& port, double factor, const cs::List<Core::RepairItem>& quote,
		Rect area, float rowH, int maxRows);

	/// <summary>The Crew tab: who is aboard against the hull's complement, the brig, and the fleet's captains.</summary>
	void DrawPortCrew(const cs::Ref<Core::Ship>& ship, Rect area, float rowH);

	/// <summary>One line of Repair &amp; Rearm, as its button does it (also a co-op frame's "repair").</summary>
	void RepairAt(const cs::Ref<Core::Settlement>& port, Core::RepairItemKind kind, int index);

	/// <summary>Repair all, as its button does it (also a co-op frame's "repairall").</summary>
	void RepairAllAt(const cs::Ref<Core::Settlement>& port);

	static cs::String RepairText(Core::RepairOutcome outcome, const cs::Ref<Core::Settlement>& port, double cost, const cs::String& done);

	// ---------------------------------------------------------------- spawn (dev/sandbox)

	/// C#: static readonly ShipClass[] SpawnClasses = (ShipClass[])Enum.GetValues(typeof(ShipClass));
	static const cs::List<Core::ShipClass>& SpawnClasses();

	/// <summary>
	/// U.SPC-T9 dev/sandbox tool, redesigned as its own full screen rather than a small corner panel:
	/// every <see cref="ShipClass"/> laid out as a grid of real rendered hull previews
	/// (<see cref="Rendering.ShipThumbnails"/>, the same model a live ship uses) -- click one to
	/// spawn a ship of it (a random design, if the class has more than one) at the camera's current
	/// position and immediately pilot it. See <see cref="UnitySpaceApp.SpawnShip"/>.
	/// </summary>
	void DrawSpawn();

	// ---------------------------------------------------------------- cargo hold

	/// <summary>
	/// The player-piloted ship's grid cargo hold (<see cref="CargoHold"/>): click an occupied cell
	/// to pick up its crate, then click an empty cell to drop it there; clicking the picked-up
	/// crate again cancels. Separate from, and drawn independently of, the market board above.
	/// </summary>
	void DrawCargo();

	static bool InHold(const cs::Ref<Core::CargoHold>& hold, const cs::Ref<Core::CargoItem>& item);

	void ClickCargoCell(const cs::Ref<Core::CargoHold>& hold, int x, int y, const cs::Ref<Core::CargoItem>& occupant);

	static cs::String GoodAbbreviation(Core::Good g);

	static cs::String Tonnes(double t);

	static cs::String Credits(double c);

	static cs::String People(double p);

	static cs::String SpaceText(const cs::Ref<Core::Compartment>& c);

	cs::String OrderText(const cs::Ref<Core::GroupOrder>& o);

	cs::Ref<GUIStyle> tickerStyle;

	static cs::String Percent(double f);

	void DrawTicker();

	static Color KindColor(Core::WorldEventKind k);

	cs::String NewsLine(const Core::WorldEvent& e);

	// ---------------------------------------------------------------- news feed

	/// C#: static readonly string[] NewsFilters = { "ALL", "MARKETS", "WAR", "SHIPS & OTHER" };
	static const cs::List<cs::String>& NewsFilters();
	int newsFilter = 0;
	Vector2 newsScroll;
	cs::Ref<GUIStyle> newsRow;
	cs::List<Core::WorldEvent> newsShown = cs::List<Core::WorldEvent>();

	/// <summary>Which filter tabs a report shows under: diplomacy (ceasefires, embargoes) is both markets and war news.</summary>
	static bool InFilter(Core::WorldEventKind k, int filter);

	/// <summary>
	/// The news feed (X), in place of the ticker: everything the sim has reported, newest first, filtered
	/// to markets (shocks, price spikes, embargoes), war (battles, captures, surrenders) or the rest. Every
	/// report is what actually happened; clicking one selects where it happened (and opens the market for
	/// market news) so the player can act on it.
	/// </summary>
	void DrawNews();

	/// <summary>How old the news here is from a settled system beyond the gates (U.SPC-T100): what is known of it is that old.</summary>
	cs::String NewsAge(const cs::Ref<Core::StarSystem>& system, double jd);

	static cs::String JourneyText(const cs::Ref<Core::Ship>& s);

	cs::String RouteText(const cs::Ref<Core::Ship>& s);

	void DrawStatus();

	/// <summary>The helm's sensor readout: running silent or not, the ship's own signature (lit up by a burn or a
	/// fight), and the contacts its scanners hold -- identified, by class only, and unknown blips.</summary>
	cs::String SensorText();

	/// <summary>A patrol's hail of the piloted ship, above the salvage readout: hold still for a scan, or pay the fine (1) or be fired on.</summary>
	void DrawHail();

	/// <summary>A record in a line: the bounties posted, notoriety, and how many crimes are on file.</summary>
	static cs::String RecordText(const cs::Ref<Core::LawRecord>& r);

	/// <summary>
	/// Above the status bar while piloting: the approach readout for the nearest derelict or wreck (range,
	/// closing speed, what is still needed to latch), or -- latched on -- the hulk's compartments with who is
	/// in each, what is still aboard, the part most worth stripping and the number-key choices.
	/// </summary>
	void DrawSalvage();

	/// <summary>
	/// The boarding panel (B at the controls, U.SPC-T468): the fight aboard the hull the player commands one side of, compartment
	/// by compartment (who holds it, the fighting, breaches), the compartment picked, the side's standing order and
	/// the number-key choices. The fight itself is the resolver's; the orders say only where people go.
	/// </summary>
	void DrawBoarding();

	/// <summary>Converts a scene speed near the camera into an equivalent true speed, using the local scale.</summary>
	double SceneSpeedToKmS(double sceneSpeed);

	/// <summary>
	/// At a seat taken from command (U.SPC-T422: a fighter's cockpit, a gun mount): a crosshair where the stick is
	/// pointing (the middle of the screen, which the view follows) and a line saying what is held and how to leave it.
	/// </summary>
	void DrawSeat();

	/// <summary>
	/// US-T135, the role panel of whatever seat is held (<see cref="Core::IEmbodiable.RolePanel"/>): the role, its state and
	/// every key of it down the left, greyed when it cannot be used now; a role's meter (the EVA worker's weld timing: the
	/// marker, the zone to tap it in, the job's progress, the weld's quality) above the seat line; and a bracket on the part
	/// of the hull it is working (or the nearest damaged one). One panel for every kind, drawn from the kind's own data.
	/// </summary>
	void DrawRolePanel();

	/// <summary>
	/// US-T106: at a helm, the hull's ability bar (<see cref="HelmAbilities.Of"/>): what its own hangars and bays let it order,
	/// each on its Shift+digit with how it stands (craft aboard and up, a team ready, reloading), bright when it can go now;
	/// with Shift+/ to jump into a launched craft. Nothing for a hull that carries none of them.
	/// </summary>
	void DrawHelmAbilities();

	// ---------------------------------------------------------------- the take-charge picker (U.SPC-T446)

	Vector2 pickerScroll;

	/// <summary>
	/// The take-charge picker (F9): every seat the player could take now, from <see cref="UnitySpaceApp.PickerSeats"/>
	/// (the hull in hand's own seat and its gun mounts, indented under it, then the fight in view, then the ships near
	/// the camera), each with what it is and whose. Clicking one jumps into it, the seat held handed back to its AI
	/// first; the clock never stops while it is open.
	/// </summary>
	void DrawPicker();

	static constexpr Color DeckLinedUpColor = Color(0.45f, 1.0f, 0.55f, 0.95f);

	/// <summary>
	/// Landing guidance in a fighter's cockpit (U.SPC-T518, see <see cref="FlightDecks"/>): the nearest friendly flight deck's
	/// approach gate as a ring, its stern mouth bracketed and the glide path between them, green once the craft is lined up
	/// closely enough for the deck to take it in (or the deck has it), amber until then; launching, the bow mouth it goes out of.
	/// </summary>
	void DrawDeckGuide();

	/// <summary>A seat's sight: four ticks about the middle of the screen, which the view follows.</summary>
	void DrawSeatSight();

	void DrawToast();
