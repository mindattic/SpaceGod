// UnitySpaceApp.Drift.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Drift.cpp.
//
// US-T165: drift, as Chorus's. R held at a fighter's controls (the helm of a fighter, heavy fighter or bomber, or a launched
// craft's seat) lets go of the dogfight model: the hull barrels on along its way while the mouse swings it freely (the
// string aim goes slack, any hull's view anywhere, and the camera with it), on its attitude thrusters, drawn here as RCS
// jets firing as the swing starts and stops. Let go, assisted flight snaps back in the new facing with a boost-out. The
// physics is Core's (Core::Drift, Core::DriftControl), so a crew can drift too (US-T164); this is the player's hands, the
// camera and the jets. Capitals, haulers, stations, guns and everything else ignore R (it toggles auto-reorient in command).

public:
	/// <summary>The flown hull is drifting this frame: the helm's, or a launched craft's seat.</summary>
	bool PilotDrifting();

	/// <summary>US-T242: how hard the flown hull's RCS jets fire this frame, 0..1 (0 with none shown): the puffs heard.</summary>
	float PilotRcsFiring() const;

	/// <summary>sg.Drift's hand on R (DriftCommand.cpp): the helm drifts as if R were held. Never set by play.</summary>
	bool DriftHeldByScript = false;

	/// <summary>sg.Drift's hand on the mouse: the helm's string aim swung right and up by so many degrees, as the mouse
	/// would (paid out at the hull's look rate on a taut string).</summary>
	void PilotLookBy(double rightDeg, double upDeg);

	/// <summary>sg.Drift's showcase, a step a call, true when done (false with <paramref name="failed"/> when it cannot be
	/// shown): 0 a light fighter of your own spawned at the camera and flown; 1 a heavy fighter (a gunship) instead;
	/// 2 the hull flown given way along its nose (most of its top speed), for a drift to carry it on.</summary>
	bool DriftShowcase(int step, cs::String& failed);

private:
	/// <summary>The RCS jets on the hull flown (a helm's ship view, or a seat craft's model); rebuilt when that changes.</summary>
	cs::Ref<Views::RcsJets> pilotRcs;

	/// <summary>Once a frame after the views are placed: the jets on the hull flown fire on its spin's change.</summary>
	void UpdatePilotRcs(float dt);
