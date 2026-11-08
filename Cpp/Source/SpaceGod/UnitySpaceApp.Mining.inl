// UnitySpaceApp.Mining.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Mining.cpp.
//
// US-T264: hands-on mining. The beam itself is the Core's (Core::HandMining, stepped by the helm's own input, the primary
// trigger out of a fight, so co-op peers cut alike); here is only what the player sees of it: its readout over the bottom of
// the HUD, its notices (a crate cut, a rock worked out, a claim jumped) on toasts, and where the beam is drawn.

public:
	/// <summary>Where the mining beam is drawn this frame, scene units: from the hull flown to the face it is cutting. False
	/// when the beam is not cutting.</summary>
	bool MiningBeamScene(Core::DVec3& from, Core::DVec3& to);

private:
	cs::String miningReadout;
	cs::Ref<GUIStyle> miningPanel, miningStyle, miningSmall;
	cs::Ref<Texture2D> miningBack;

	/// <summary>Each frame at a helm (from UpdatePilotBattle): the beam's notices on toasts and its readout.</summary>
	void UpdateMining(const cs::Ref<Core::Ship>& ship);

	/// <summary>The beam's readout over the bottom of the HUD, above the tractor beam's when both show.</summary>
	void DrawMiningHud();
