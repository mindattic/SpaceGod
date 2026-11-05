// UnitySpaceApp.SkyTravel.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// US-T145: the sky's layers and the travel between them; bodies in UnitySpaceApp.SkyTravel.cpp.
//
// <summary>
// Each system sits inside its own sky; between the stars (the jump drive's crossing and its void stops) the sky is the
// Milky Way, band and bulge in their true directions, with every star the drive can jump to drawn as a point where it
// really is. The swap from one to the other is only ever made under the jump's tunnel at full strength: the streaks build
// as the drive spools, the sky smears into the tunnel, the skies change, and it unwinds; dropping out plays it the other
// way, with the arrival flash. In the painted-volume mode (Views::SkyLayers) the system's painting is a cloud about the
// size of its outer rim, so flying straight out past the rim is flying out through it into the open Milky Way sky.
// Presentation only: the simulation is read, never changed.
// </summary>

public:
	/// <summary>The sky's layers (the Milky Way, the painted volume, the jump stars and the tunnel).</summary>
	cs::Ref<Views::SkyLayers> SkyLayersView;

	/// <summary>The jump's tunnel this frame, 0..1.</summary>
	float SkyWarp() const { return skyWarp; }

	/// <summary>The hero system of this galaxy: the one drawn with the hand-painted hero sky (the first violet-storm system
	/// beyond Sol, else the first beyond Sol).</summary>
	cs::String SkyHeroSystem();

	/// <summary>The outer rim of the system in view, scene units at the scale drawn now (0 with none).</summary>
	double SkyRimScene();

	/// <summary>
	/// sg.EdgeRun's hold on the hull at the helm: placed at <paramref name="rimFraction"/> of the outer rim from the star
	/// along <paramref name="outward"/> (ecliptic, unit), still, its nose out (or back in with <paramref name="facingIn"/>).
	/// False if nothing is piloted.
	/// </summary>
	bool SkyEdgePlace(double rimFraction, Core::DVec3 outward, bool facingIn);

	/// <summary>Takes the helm of a hull in this system (one with a jump drive first, if <paramref name="wantDrive"/>). False if none.</summary>
	bool SkyTakeHelm(bool wantDrive);

	/// <summary>Lets go of the helm (back to the command view), so a trip to another system can be made by the camera.</summary>
	void SkyLeaveHelm();

	/// <summary>sg.WarpTunnel's preview: the tunnel held at <paramref name="warp"/> (negative: back to the game's own) round the
	/// camera's view, the sky between the stars (the Milky Way) with <paramref name="interstellar"/>.</summary>
	void SkyPreview(float warp, bool interstellar);

private:
	void BeginSkyTravel();
	void UpdateSkyTravel(float dt);
	void EndSkyTravel();
	void RefreshJumpStars();

	float skyWarp = 0, skyLastSmear = -1;
	UnityCompat::Vector3 skyWarpAxis = UnityCompat::Vector3(0, 0, 1);
	bool skyWasCrossing = false, skyWasStop = false;
	float skyPreviewWarp = -1;
	bool skyPreviewInterstellar = false;
	cs::String skyHero;
	const void* skyHeroGalaxy = nullptr;
	cs::String skyGradeKey;
	cs::Ref<Core::StarSystem> skyJumpFor;
	int skyJumpChart = -1;
	float skyJumpAge = 0;
