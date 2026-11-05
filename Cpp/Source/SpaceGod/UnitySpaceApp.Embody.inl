// UnitySpaceApp.Embody.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Embody.cpp.
//
// <summary>
// US-T135, the one embody layer: the SpaceGod takes anything through one action. P on what is selected (or under the
// mouse) embodies it, whatever it is: a ship's helm, a hull or station in a fight, a launched craft; F9 picks from the
// list; ' ; cycle the seats in the fight in view, / the guns on the hull held, Shift+P its EVA repair workers out on the
// plating; Backspace moves up to the host; P again lets go to the free SpaceGod camera. Every kind is driven only through
// <see cref="Core::IEmbodiable"/>'s contract: its input profile (and the frame's hands are its alone, US-T90), its camera
// rig (placed here; how a chase flies is the camera's own, US-T129), its HUD role panel, and its subject in the world.
// Every attempt goes through one gate (<see cref="Core::Embodiables::MayEmbody"/>, US-T140's rules), refused with its reason.
// </summary>

public:
	/// <summary>A unit someone embodies now (US-T135): who, the seat, and what it is in the world.</summary>
	struct EmbodiedUnit
	{
		Core::PlayerId Player = Core::PlayerId::None();
		cs::Ref<Core::IEmbodiable> Seat;
		Core::EmbodimentSubject Subject;
		/// <summary>The helm flown by the app's pilot loop (not a session seat).</summary>
		bool Helm = false;
	};

	/// <summary>
	/// Everything embodied now and by whom (US-T135): every session seat (<see cref="Core::ISessionState.Held"/>, the same on
	/// every lockstep peer), and the ship's helm flown by the pilot loop, held by the co-op helm holder (or the local player
	/// alone). What a view reads to put a glint on units other players hold (co-op or PvP, a later ticket). Transient: no
	/// player identity, rating or credit is kept; deeds belong to the unit held.
	/// </summary>
	cs::List<EmbodiedUnit> EmbodiedNow();

	/// <summary>Who embodies <paramref name="unit"/> (its helm, a gun, a worker on it...), or nobody (US-T135).</summary>
	Core::PlayerId EmbodiedBy(const cs::Ref<Core::BattleUnit>& unit);

	/// <summary>Who embodies <paramref name="ship"/> (its helm, or anything on its unit in a fight), or nobody (US-T135).</summary>
	Core::PlayerId EmbodiedBy(const cs::Ref<Core::Ship>& ship);

	/// <summary>The HUD role panel of what the local player holds, helm or seat; false when nothing is held.</summary>
	bool HeldRolePanel(Core::EmbodimentRolePanel& panel);

	/// <summary>What <paramref name="t"/> embodies as: a ship's helm, a unit's own seat or its station, a station at what is
	/// selected; null for nothing embodiable.</summary>
	cs::Ref<Core::IEmbodiable> EmbodiableAt(const Target& t);

	/// <summary>P from the free view on anything but a ship (US-T135): embodies the selection (or what is under the mouse).
	/// False if there was nothing to embody there.</summary>
	bool EmbodyTarget();

	/// <summary>Shift+P (US-T135): out on the plating as the next EVA repair worker of the hull held (or flown, or selected).</summary>
	void CycleWorker();

private:
	/// <summary>The one embody gate as the player meets it: true to go on, else toasts its reason.</summary>
	bool PassesEmbodyGate(const cs::Ref<Core::IEmbodiable>& next);

	/// <summary>The hull in hand for / and Shift+P: the one the seat held is on (a gun, a station, a worker), the one flown, or the one
	/// selected in a fight, or a station fighting at what is selected; null when none.</summary>
	cs::Ref<Core::BattleUnit> HullInHand(cs::Ref<Core::Battle>& battle);

	/// <summary>Puts the camera on <paramref name="next"/> by its rig (<see cref="Core::IEmbodiable.CameraRig"/>): the chase about
	/// it, and for a mount or a shoulder the eye placed on the hull.</summary>
	void RideCameraRig(const cs::Ref<Core::IEmbodiable>& next, const Core::EmbodimentPose& pose, bool switching);

	/// <summary>Where the eye sits for a mount or shoulder rig, from the seat's subject: on the hull as it is drawn, lifted
	/// off the plating and (a shoulder) set back behind the look, all in the host's radii, so either scale reads the same.</summary>
	Core::DVec3 RigEye(const cs::Ref<Core::IEmbodiable>& s);
