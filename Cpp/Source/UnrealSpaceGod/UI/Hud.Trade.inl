// Hud.Trade.inl -- included INSIDE class Hud (see UI/Hud.h).
// Port of Unity.SpaceGod's Runtime/UI/Hud.Trade.cs (the trade desk); bodies in UI/Hud.Trade.cpp.

private:
	// ---------------------------------------------------------------- the trade desk (U.SPC-T426)

	enum class TradeTab : int { Runs, News, Charters, Insurance, Warehouses };

	/// C#'s enum ToString (the tab's label).
	friend const char* ToString(TradeTab v)
	{
		switch (v)
		{
		case TradeTab::Runs: return "Runs";
		case TradeTab::News: return "News";
		case TradeTab::Charters: return "Charters";
		case TradeTab::Insurance: return "Insurance";
		case TradeTab::Warehouses: return "Warehouses";
		}
		return "";
	}

	TradeTab tradeTab = TradeTab::Runs;
	Vector2 tradeScroll;

	/// <summary>The port the desk deals with: the selected world's biggest discovered port, else the one the piloted ship is alongside.</summary>
	cs::Ref<Core::Settlement> TradePortInView();

	/// <summary>
	/// The trade desk (backslash): the trader's rank and what the house is worth, and five pages: the runs the player's price
	/// intelligence adds up to (with a broker's price sheet to buy at the port in view, and a house freighter to send
	/// on the best), the trade news, the trading companies' charter offers and the house's hulls out on charter, the
	/// underwriters' quotes and policies, and the house's warehouses with their stock to store or sell. Alongside a
	/// lawful port the back door shows too: what in the hold the open market will not take, and what it pays.
	/// </summary>
	void DrawTradeDesk();

	static bool HasBackDoorCargo(const cs::Ref<Core::TradeDesk>& desk, const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& port);

	/// <summary>The runs the desk's prices add up to for the flown hold (or a house freighter's), with a price sheet to buy.</summary>
	void DrawTradeRuns(const cs::Ref<Core::TradeDesk>& desk, const cs::Ref<Core::Settlement>& port, const cs::Ref<Core::Faction>& purse, Rect view, float rowH, double jd);

	/// <summary>A crewed house hull with a hold, holding in port and not on charter: the one a run is offered to.</summary>
	cs::Ref<Core::OwnedShip> IdleFreighter();

	void DrawTradeNews(const cs::Ref<Core::TradeDesk>& desk, Rect view, float rowH, double jd);

	void DrawTradeCharters(const cs::Ref<Core::TradeDesk>& desk, Rect view, float rowH, double jd);

	void DrawTradeInsurance(const cs::Ref<Core::TradeDesk>& desk, Rect view, float rowH, double jd);

	void DrawTradeWarehouses(const cs::Ref<Core::TradeDesk>& desk, Rect view, float rowH, double jd);

	/// <summary>What in the flown hold the open market here will not take, what the back door pays, and customs' odds.</summary>
	void DrawBackDoor(const cs::Ref<Core::TradeDesk>& desk, const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& port, Rect r, float rowH, double jd);
