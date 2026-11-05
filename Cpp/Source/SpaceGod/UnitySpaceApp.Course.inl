// UnitySpaceApp.Course.inl: included INSIDE the UnitySpaceApp class body (UnitySpaceApp.h). Port of Runtime/UnitySpaceApp.Course.cs.
//
/// A plotted course over the gates (U.SPC-T516): a trip to a system several gates away is a list of real legs
/// (<see cref="TravelCourse"/>), each flown across the system the hull is in to that system's gate for the next link
/// (by the hyperlanes where they run to it) and gone through. At the helm the pilot flies it by hand (the HUD names the
/// next gate and how far it is) or hands it to the crew (the autopilot, <see cref="EngageCourseAutopilot"/>); the god view's
/// followed ship is given the course the same way and the view goes with it, system to system. Nothing jumps a hull to the
/// far system.
//
// A C# null string is the empty string here (EndCourse's toast, CourseLine's result).

public:
	/// <summary>The course being flown, or null.</summary>
	cs::Ref<Core::TravelCourse> Course;

private:
	/// <summary>The hull the course was plotted for: another hull's helm, or a hull lost, ends it.</summary>
	cs::Ref<Core::Ship> courseShip;

public:
	/// <summary>Whether the crew flies the course (the autopilot, or a followed ship sent on it) rather than the pilot.</summary>
	bool CourseByCrew = false;

private:
	/// <summary>Whether the view goes with the crew's hull from system to system.</summary>
	bool courseFollow = false;

public:
	/// <summary>Plots the quickest open way over the gates to <paramref name="to"/> for the piloted hull.</summary>
	void PlotCourse(const cs::Ref<Core::StarSystem>& to);

	/// <summary>
	/// The autopilot: hands the helm to the crew, who fly the plotted course gate by gate (by lane across each system where
	/// one runs to the gate) and take every gate; the view follows the hull. False, with a toast, if the crew cannot take it.
	/// </summary>
	bool EngageCourseAutopilot();

private:
	/// <summary>Sends <paramref name="ship"/> (standing in the active system) on a course to <paramref name="to"/> under its crew.</summary>
	bool StartCrewCourse(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::StarSystem>& to, bool follow);

	/// <summary>The god view's trip to <paramref name="to"/> when a ship is followed: the ship is sent, and the view goes with it.</summary>
	bool TrySendFollowedShip(const cs::Ref<Core::StarSystem>& to);

	void FollowCourseShip(const cs::Ref<Core::Ship>& ship);

	/// <summary>The system whose fleet holds <paramref name="ship"/> now, or null (inside a gate).</summary>
	cs::Ref<Core::StarSystem> SystemHolding(const cs::Ref<Core::Ship>& ship);

	void EndCourse(const cs::String& toast);

	/// <summary>Each frame the crew flies a course: keeps it level with the hull's voyage, takes the view into each system the hull comes out in, and ends it on arrival.</summary>
	void UpdateCourseShip();

	/// <summary>The hull has gone through <paramref name="gate"/>: on to the next leg, replotted if it was some other gate, ended on arrival.</summary>
	void AdvanceCourse(const cs::Ref<Core::JumpGate>& gate, const cs::Ref<Core::Ship>& ship);

public:
	/// <summary>The HUD's line for the course being flown, or null: the way by its systems, the next gate and how far off it is.</summary>
	cs::String CourseLine();

	/// <summary>Whether the HUD offers the autopilot: a course plotted at the helm, not yet handed to the crew.</summary>
	bool CourseAutopilotOffered();
