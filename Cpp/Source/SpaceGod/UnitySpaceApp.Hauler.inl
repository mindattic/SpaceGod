// UnitySpaceApp.Hauler.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Hauler.cs; bodies in UnitySpaceApp.Hauler.cpp.
//
// <summary>
// The hauler at the helm (U.SPC-T443, <see cref="HaulerFlightModel"/>, docs/FLIGHT_MODELS.md, Haulers): what the pilot loop
// tells the hauler model each frame (how laden the hull is, and whether it is inside a port's approach, where Ctrl is
// docking mode), and what the HUD shows of it (the full-retro stop point, and docking mode).
// </summary>

public:
	/// <summary>Where a piloted hauler comes to rest under full retro, with what it carries aboard; null for any other hull.</summary>
	cs::Nullable<Core::HaulerStop> PilotStop;

	/// <summary>Whether the piloted hauler flew this frame in docking mode (Ctrl held inside a port's approach).</summary>
	bool PilotDocking = false;

	/// <summary>Whether the piloted hauler is inside a port's approach (U.SPC-T443): near enough a port of the world it flies
	/// by to deal with it (<see cref="NearPort"/>), welcome or not.</summary>
	bool PilotInPortApproach = false;

private:
	/// <summary>Whether the hull is inside a port's approach this frame, for the helm to fly a hauler's docking mode in
	/// (the load aboard and Ctrl are the helm's own, U.SPC-T449).</summary>
	bool PortApproach();

	/// <summary>After the hauler model has flown the frame: the stop point and docking mode, for the HUD.</summary>
	void HaulerReadout(const cs::Ref<Core::IFlightModel>& model, const Core::HullDynamics& hull, const Core::PilotInput& input);
