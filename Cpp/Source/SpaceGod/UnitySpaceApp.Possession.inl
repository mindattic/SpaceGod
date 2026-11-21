// UnitySpaceApp.Possession.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Possession.cpp.
//
// <summary>
// US-T446: possession is a show. Whenever this machine's player takes a seat (any kind: a helm, a craft, a gun, a station, a
// satellite, a mech, a person out on the plating or in open space) an almost imperceptible energy crosses from the god's eye
// to it and seeps into the hull (Views::PossessionFx, the hull's own rim, a breath of the view), and what can be seen then
// becomes what that pilot perceives: its own sensors, a station's wide traffic picture, or a person's eyes alone. Letting
// go, the energy comes back out and the god's full view returns. Seen from what is held, not from how it was taken, so
// every path (the keys, the picker, a co-op frame granting a seat, a load) plays it. View only, per peer: the simulation's
// state, the sensors' pictures and the co-op digest are never touched (the seat's picture is a view-side ContactBook).
// Every tunable is in Views::PossessionTune.
// </summary>

public:
	/// <summary>What the view is seen through: the god's own eye, a pilot's sensors, a person's eyes, a station's panorama.</summary>
	enum class PerceptionKind : int { God, Sensors, Eyes, Panorama };

	/// <summary>What the view is seen through now (the kind held, or the one being let go while the god's view comes back).</summary>
	PerceptionKind Perceiving() const { return perceiving; }

	/// <summary>0 the god's full view .. 1 the possessed view fully in (eased in after the energy has gone in, out on letting go).</summary>
	float PerceptionShown() const { return perceptionShown; }

	/// <summary>
	/// The picture the view is filtered by: null for the god's full view (or while the possessed view is still coming in); at
	/// the helm the ship's own (Sensors::Player); in any other seat the seat's own view-side picture, swept from its sensors
	/// (or eyes) every PossessionTune::SweepSeconds, its tracks where the light showed them (light-lagged), cold ones held.
	/// </summary>
	cs::Ref<Core::ContactBook> PerceivedPicture() const;

	/// <summary>Whether the ship at <paramref name="index"/> is drawn as its model: always in the god's view; possessed, only
	/// what the picture holds live, the hull held, and anything close enough to see by eye (VisualRangeKm).</summary>
	bool PerceivesShipModel(int index);

	/// <summary>Where a contact is drawn from where it is now: back along its course by the light's delay (scene units).</summary>
	Core::DVec3 PerceivedLagOffset(const cs::Ref<Core::Ship>& ship, double lagSeconds);

	/// <summary>How far the view's sensors reach a one-unit signature (km), 0 for eyes or the god's view: the frame's caption.</summary>
	double PerceivedReachKm() const { return perceivedReachKm; }

	/// <summary>The show this frame, for the HUD and the console command: whether one is playing, going in, and how far in (s).</summary>
	bool PossessionShowing() const { return possessionShow.Active; }
	bool PossessionEntering() const { return possessionShow.Entering; }
	float PossessionT() const { return possessionShow.T; }

	/// <summary>The screen's faint wash this frame, 0..1 of PossessionTune::WashAlpha (already scaled for Reduce flashes).</summary>
	float PossessionWash() const;

	/// <summary>The breath of the view this frame, degrees off the field of view (narrower going in, wider letting go).</summary>
	float PossessionFovBreath() const;

	/// <summary>The hull model's own rim term (_Highlight) for <paramref name="ship"/> this frame: lit as the energy seeps in.</summary>
	float PossessionHullRim(const cs::Ref<Core::Ship>& ship) const;

	/// <summary>The 3D half of the show, made on the system's root; null before a system is shown.</summary>
	cs::Ref<Views::PossessionFx> possessionFx;

private:
	struct PossessionShowState
	{
		bool Active = false, Entering = true;
		float T = 0, Speed = 1;
		cs::Ref<Core::Ship> Helm;
		cs::Ref<Core::IEmbodiable> Seat;
		/// <summary>The last place worked out (kept if the hull goes, so the show ends where it was).</summary>
		Core::DVec3 Centre, Forward;
		double Radius = 0;
		bool Person = false, Placed = false;
		cs::Ref<Core::Ship> RimShip;
	};
	PossessionShowState possessionShow;
	cs::Ref<Core::Ship> possessedHelm;
	cs::Ref<Core::IEmbodiable> possessedSeat;
	PerceptionKind perceiving = PerceptionKind::God;
	float perceptionShown = 0;
	cs::Ref<Core::ContactBook> perceivedBook;
	float perceptionSweepTimer = 0;
	/// <summary>Where the perceiver is (heliocentric km) and the hull it is in or on (never filtered out of its own view).</summary>
	Core::DVec3 perceiverHelio;
	cs::Ref<Core::Ship> perceiverShip;
	double perceivedReachKm = 0;
	cs::Ref<Transform> possessionRoot;

	/// <summary>Called first in UpdateViews: what is held now against last frame, the show's clock, the perceived view's ease.</summary>
	void UpdatePossession();
	/// <summary>Called last in UpdateViews: the show's hull placed, its 3D half drawn.</summary>
	void UpdatePossessionFx();
	/// <summary>The seat's view-side picture swept (never the simulation's).</summary>
	void SweepPerception();
	/// <summary>Where the show's hull is drawn now: false when it cannot be placed (the last place is kept).</summary>
	bool PlacePossession(PossessionShowState& s);
	/// <summary>A ship's place (heliocentric km) and velocity (km/s) now, as drawn: its battle unit's in a fight, else its schedule's.</summary>
	Core::DVec3 PerceivedHelioOf(int index, Core::DVec3& velocityKmS);
	static PerceptionKind PerceptionOf(const cs::Ref<Core::Ship>& helm, const cs::Ref<Core::IEmbodiable>& seat);
