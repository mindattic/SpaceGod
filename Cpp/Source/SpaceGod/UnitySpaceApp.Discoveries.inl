// UnitySpaceApp.Discoveries.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Discoveries.cpp.
//
// <summary>
// US-T259, the discovery log as the player meets it: the game sweeps what has been found into the Galaxy's log
// (Core::DiscoveryLog, saved) every so often and whenever the player's place or scan changes, says so on a toast for a
// notable find, and the discoveries screen (Shift+O, UI/DiscoveriesScreen) lists it, goes to a find in the system the player
// is in, or plots a course to where it is. The screen is local to this machine, like the Help screen; the log is shared
// state every peer keeps alike (but for the bodies each player scans).
// </summary>

public:
	/// <summary>The discoveries screen (Shift+O): everything found, by kind and the day it was found. Esc closes it.</summary>
	bool ShowDiscoveries = false;

	/// <summary>Each frame: sweeps the simulation's finds into the log when the place, the helm's body or the scanned body
	/// has changed, else every couple of seconds.</summary>
	void UpdateDiscoveries();

	/// <summary>The Galaxy's discovery log, made the first time it is asked for.</summary>
	Core::DiscoveryLog& Discoveries();

	/// <summary>What selecting <paramref name="e"/> would select in the system in view (a body, or a met pilot's hull here);
	/// false when it is not here to select.</summary>
	bool DiscoveryTarget(const Core::DiscoveryEntry& e, Target& target);

	/// <summary>Selects <paramref name="e"/> (and, with <paramref name="focus"/>, puts the camera on it); false when it is not
	/// here to select.</summary>
	bool GoToDiscovery(const Core::DiscoveryEntry& e, bool focus);

	/// <summary>Why no course can be plotted to where <paramref name="e"/> is ("" when one can: another system, at a helm).</summary>
	cs::String DiscoveryCourseRefusal(const Core::DiscoveryEntry& e);

	/// <summary>Plots a course to where <paramref name="e"/> is: over the gates to a running system, by FTL to a star on the
	/// charts. False, with a toast, when it cannot.</summary>
	bool PlotDiscoveryCourse(const Core::DiscoveryEntry& e);

	/// <summary>Draws the discoveries screen over the whole view (the Hud calls it while <see cref="ShowDiscoveries"/>);
	/// closes it on Back or once a button went to a find.</summary>
	void DrawDiscoveries();

	/// <summary>The screen's own textures go with the Hud's (U.SPC-T366).</summary>
	void DestroyDiscoveriesTextures();

private:
	cs::Ref<UI::DiscoveriesScreen> discoveriesScreen;
	int discoveryFrames = 0;
	const Core::StarSystem* discoverySystem = nullptr;
	int discoveryNear = -2, discoveryScanned = -2;
	/// <summary>The galaxy last swept: the first sweep of a game (or of a load) writes in everything already known, with
	/// no toast for that.</summary>
	const void* discoveryGalaxy = nullptr;
