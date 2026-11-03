// Hud.Coop.inl -- included INSIDE class Hud (see Hud.h).
// US-T44: the HUD's actions that write the Core, in co-op: the C# track's Runtime/UI/Hud.Coop.cs (US-T43) in C++; bodies
// in Hud.Coop.cpp. The ported panels call in here only at lines marked US-T44: in single-player every call is false and
// the original goes on.

private:
	/// A panel's button that writes the Core, in co-op: the action goes into the frames by what it acts on (an index each
	/// peer reads back the same), and CoopExecute does at its frame, on every peer, what the button does. False in
	/// single-player and while carrying it out. The arguments are joined with spaces (C#: string.Join(" ", args)).
	template <class... A>
	bool CoopSends(const cs::String& verb, const A&... args)
	{
		cs::String joined;
		bool first = true;
		((joined += (first ? cs::String() : cs::String(" ")) + CoopArg(args), first = false), ...);
		return CoopSendsText(verb, joined);
	}
	bool CoopSendsText(const cs::String& verb, const cs::String& args);
	static cs::String CoopArg(int v);
	static cs::String CoopArg(const cs::String& s);
	static cs::String CoopArg(const char* s) { return cs::String(s); }

	int K(const cs::Ref<Core::Settlement>& s);
	cs::Ref<Core::Settlement> Port(const cs::String& k);

	template <class T>
	static int K(const cs::List<cs::Ref<T>>& list, const cs::Ref<T>& item)
	{
		for (int i = 0; i < list.Count(); i++)
			if (list[i] == item) return i;
		return -1;
	}

	template <class T>
	static cs::Ref<T> At(const cs::List<cs::Ref<T>>& list, const cs::String& k);

	long long observedFrame = -1;
	cs::Ref<Core::Settlement> observedAt;

	/// Prices seen in port go into the trade desk's intelligence: in co-op once a frame per market, as an entry.
	bool CoopObserve(const cs::Ref<Core::Settlement>& market);
	/// The outfitting panel's Fit/Strip, by the yard, the row and the option chosen.
	bool CoopSendsOutfit(const cs::Ref<Core::Settlement>& yard, const OutfitChoice& option);
	/// A selection key with its spaces as '|' (one field of an entry).
	cs::String CoopKeyText(const Target& sel);

	// ---- the original's actions, as their buttons do them (moved here so a frame can carry them out too)
	void PayBounty(const cs::Ref<Core::Settlement>& market);
	void HandOverPrisoners(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& port);
	void DeliverOutlaw(const cs::Ref<Core::Outlaws>& outlaws, const cs::Ref<Core::Outlaw>& o, const cs::Ref<Core::Settlement>& port);
	void RansomOutlaw(const cs::Ref<Core::Outlaws>& outlaws, const cs::Ref<Core::Outlaw>& o, const cs::Ref<Core::Settlement>& port);
	void TakeContract(const cs::Ref<Core::Contracts>& contracts, const cs::Ref<Core::Contract>& c, const cs::Ref<Core::Ship>& ship, double jd);
	void OutfitApply(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Settlement>& yard, const cs::Ref<Core::Loadout>& loadout, const OutfitChoice& option);

public:
	/// A frame's HUD action, on every peer: what the button does (the original's call, by the same objects read back
	/// from their keys). False for a verb the HUD does not know.
	bool CoopExecute(const cs::String& verb, const std::vector<cs::String>& a);
	/// For the smoke run: what the market board's Buy button does for this good at this market.
	void CoopSmokeBuy(const cs::Ref<Core::Settlement>& market, Core::Good good) { ClickBuy(market, good); }
