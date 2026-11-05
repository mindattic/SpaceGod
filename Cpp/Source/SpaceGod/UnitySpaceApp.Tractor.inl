// UnitySpaceApp.Tractor.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Tractor.cpp.
//
// US-T95: the tractor beam. A hull that dies with cargo aboard spills most of it as drifting pods (Core::CargoPods); cargo
// dropped to a threat (US-T137) floats out as pods too. At any helm, Shift+T locks the beam onto the nearest pod in reach
// (reach and pull by hull class, Core::TractorSpec) and hauls it in; alongside, its crates go into the hull's own grid hold
// through the crate loader latch-and-salvage uses, and onto the record of the pilot flying it. The beam then takes the next
// pod in reach by itself; Shift+T again lets go. Everything is real km and sim seconds, so it works the same at either
// scale; the view draws the pod sliding down the beam onto the hull wherever the scale model puts them.

public:
	/// <summary>The pod the beam holds, or null.</summary>
	cs::Ref<Core::CargoPod> TractorPod() const { return tractorPod; }

	/// <summary>Where a pod is drawn: anchored like a wreck at the battle scale; the one the beam holds slides down the beam
	/// onto the hull as it comes in, at either scale.</summary>
	Core::DVec3 CargoPodScene(const cs::Ref<Core::CargoPod>& p);

	/// <summary>Locks the beam onto the nearest pod in reach of the hull flown; false (with <paramref name="said"/>) if none.</summary>
	bool TractorLock(cs::String& said);

	/// <summary>Lets the pod go; says <paramref name="why"/> on a toast if given.</summary>
	void TractorRelease(const cs::String& why);

	/// <summary>The dev menu's cargo wreck: a laden freighter spawned a little ahead of the hull flown (or the camera) and blown
	/// apart there through the same loss a collision is, so it spills its cargo as pods. Null (with <paramref name="said"/>) if
	/// it cannot be done here.</summary>
	cs::Ref<Core::Wreck> SpawnCargoWreck(cs::String& said);

	/// <summary>sg.Tractor's showcase (TractorCommand.cpp), a step a call, true when done: 0 takes a helm if none is held (a
	/// corvette of your own at the camera), 1 blows a cargo-laden wreck open ahead of it, 2 locks the beam. False with
	/// <paramref name="failed"/> set when it cannot.</summary>
	bool TractorShowcase(int step, cs::String& failed);

	static void RegisterTractorCheats();

private:
	cs::Ref<Core::CargoPod> tractorPod;
	cs::Ref<Core::Ship> tractorShip;
	/// <summary>The pod's speed down the beam, km/s; its distance when locked and now, km; its tow when locked (the view's start).</summary>
	double tractorSpeedKmS = 0, tractorStartKm = 0, tractorDistKm = 0;
	Core::DVec3 tractorTowAtLock;
	cs::String tractorReadout;
	int tractorCrates = 0;

	cs::Ref<Views::CargoPodsView> cargoPodsView;
	cs::Ref<Transform> cargoPodsRoot;

	cs::Ref<GUIStyle> tractorPanel, tractorStyle, tractorSmall;
	cs::Ref<Texture2D> tractorBack;

	/// <summary>Each frame at a helm (from UpdatePilotBattle): Shift+T, the pull, the catch, the next pod, the readout.</summary>
	void UpdateTractor(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>Each frame with the other views: the pods and the beam drawn (the view made for the system shown), and the beam
	/// let go once nobody flies the hull that held it.</summary>
	void UpdateCargoPodsView();

	/// <summary>The beam's readout over the bottom of the HUD.</summary>
	void DrawTractorHud();

	Core::DVec3 TractorShipHelio();
