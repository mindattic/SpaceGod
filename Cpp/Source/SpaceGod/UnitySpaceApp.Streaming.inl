// UnitySpaceApp.Streaming.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Streaming.cs; bodies in UnitySpaceApp.Streaming.cpp.
//
// <summary>
// Seamless streaming (U.SPC-T414). A system is entered with none of its ships' models built: each is built when
// <see cref="Streaming"/> says it is wanted (big enough on screen, or about to be at the view's present speed and
// heading), a few each frame within the loading budget, and let go of again once it has dwindled well out of
// sight; until then it is drawn as its icon, as any far-off ship is. Body maps and ship models load in the
// background (<see cref="AssetStreamer"/>), and the systems beyond the gates the view is closing on are warmed
// ahead of arrival. What is still loading (<see cref="StreamingPending"/>) holds a jump gate's tunnel open, or the
// helm's own flare through a ring (<see cref="HelmGateGlow"/>), so the far side is never seen half built.
// </summary>

private:
	cs::List<Core::StreamItem> streamItems;
	cs::List<int> streamLoad, streamUnload;
	/// C#: System.Diagnostics.Stopwatch.StartNew() -- started when the app is made.
	std::chrono::steady_clock::time_point streamClockStart = std::chrono::steady_clock::now();
	cs::HashSet<cs::String> streamKeep;
	cs::List<cs::String> activePaths;
	cs::Func<bool, int> buildShipView;
	cs::Func<double> streamNow;
	cs::Ref<Core::StarSystem> streamSystem;
	Core::DVec3 streamLastCamera, streamVelocity;
	float warmTimer = 0, releaseInS = 0;
	bool releasePending = false;

	/// <summary>The most ship views let go of in one frame.</summary>
	static constexpr int MaxUnloadsPerFrame = 24;

	/// <summary>How often (seconds) the gates ahead are looked at for systems to warm.</summary>
	static constexpr float WarmEvery = 0.5f;

	/// <summary>How long after entering a system the maps of the one left behind are let go of.</summary>
	static constexpr float ReleaseAfterS = 12.0f;

	/// <summary>How near (in gate ring diameters) a gate must be for its far system to be warmed whatever the view's speed.</summary>
	static constexpr double WarmGateDiameters = 400;

public:
	/// <summary>
	/// What is still loading that the view needs now: ship models big enough on screen to be drawn in full but not
	/// built yet, and the active system's maps still on their way. A gate transit holds its cover while this is above 0.
	/// </summary>
	int StreamingPending = 0;

	/// <summary>Where the view will be (scene units) when the system shows: set while a gate's tunnel hides the system,
	/// to the far gate it comes out of, so what is about to be seen is loaded first; null otherwise.</summary>
	cs::Nullable<Core::DVec3> StreamFocus;

	/// <summary>The way the view will look from <see cref="StreamFocus"/>.</summary>
	Core::DVec3 StreamFocusHeading;

private:
	/// <summary>The helm's own flight through a gate: its flare-and-fold covering the far side's loading.</summary>
	const cs::Ref<Core::GateCover> helmCover = cs::New<Core::GateCover>();

public:
	/// <summary>The glow over the screen of the helm's flight through a gate, 0..1.</summary>
	float HelmGateGlow() const { return helmCover->Active ? static_cast<float>(Core::GateCover::HelmGlow(helmCover->T())) : 0.0f; }

	/// <summary>How hard space is folded shut in the helm's flight through a gate, 0..1 (the view narrows with it).</summary>
	float HelmGateFold() const { return helmCover->Active ? static_cast<float>(Core::GateCover::HelmFold(helmCover->T())) : 0.0f; }

private:
	/// <summary>How far the view narrows (degrees) at the height of the helm's fold through a gate.</summary>
	static constexpr float HelmFoldDeg = 22.0f;

public:
	/// <summary>Whether <paramref name="ship"/> is drawn as its 3D model right now: big enough on screen (see
	/// <see cref="ShipLod"/>) and its model built. The HUD draws its icon whenever it is not.</summary>
	bool ShipModelShown(const cs::Ref<Core::Ship>& ship, double px) const;

private:
	/// <summary>Starts the helm's cover for a jump just made: a flash, a fold of space and a glow held while the far
	/// side loads.</summary>
	void BeginHelmGateCover();

	/// <summary>Called on entering a system: what it is drawn with is noted (its maps are part of what a gate waits
	/// for), and the system left behind will be let go of in a while.</summary>
	void OnSystemStreamed(const cs::Ref<Core::StarSystem>& system);

	void UpdateStreaming(float dt);

	/// <summary>Builds ship <paramref name="i"/>'s view if its model is in memory (false, and nothing built, if not).</summary>
	bool BuildStreamedShipView(int i);

	/// <summary>Lets go of ship <paramref name="i"/>'s view (its own meshes and materials with it); drawn as its icon
	/// until it is wanted again.</summary>
	void DropShipView(int i);

	/// <summary>Warms the systems the view is heading for: the destination of a trip under way, and the far side of
	/// any gate it is near or closing on. Some while after a system is entered, the maps of systems neither drawn
	/// nor warmed are let go of.</summary>
	void UpdateWarming(float dt, Core::DVec3 camera);

	void Warm(const cs::Ref<Core::StarSystem>& system);
