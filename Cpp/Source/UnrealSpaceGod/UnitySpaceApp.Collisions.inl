// UnitySpaceApp.Collisions.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Collisions.cs; bodies in UnitySpaceApp.Collisions.cpp.
//
// <summary>
// Collisions for the piloted hull (U.SPC-T60): every frame it flies, its volume is tested against the
// other hulls in its battle (or, out of one, every ship near it), the debris of wrecks, stations and
// structures in orbit, and the real asteroids of the belt, and a contact is resolved by
// <see cref="Collisions.Resolve"/>: momentum exchanged (a battle unit is knocked off its line; a ship on
// its schedule, a structure, a rock and a drifting debris field hold their tracks) and the energy the
// crumpling took done as damage to the struck side of each hull. A hull destroyed so leaves a wreck.
// </summary>
//
// C#'s Dictionary<object, float> keyed by whatever was struck (a battle unit, a ship, a wreck, a structure, a
// body, or a boxed asteroid number, which compares by value) is keyed here by ContactKey: the object held (as C#'s
// dictionary holds it) or the number.

private:
	/// <summary>Real seconds a contact's toast stays quiet for before the same thing can toast again.</summary>
	static constexpr float ContactToastIntervalS = 1.5f;

	/// <summary>A contact not struck again inside this many suppression windows is long past mattering to it.</summary>
	static constexpr float ContactToastStaleS = ContactToastIntervalS * 20;

	/// C# `object` as a dictionary key: a reference (by identity) or a boxed int (by value).
	struct ContactKey
	{
		std::shared_ptr<const void> Object;
		int Number = 0;

		template <class T> static ContactKey Of(const cs::Ref<T>& o)
		{
			ContactKey k;
			k.Object = o;
			return k;
		}

		static ContactKey Of(int number)
		{
			ContactKey k;
			k.Number = number;
			return k;
		}

		bool operator==(const ContactKey& o) const { return Object == o.Object && Number == o.Number; }
		size_t HashCode() const { return cs::HashCombine(std::hash<const void*>{}(Object.get()), std::hash<int>{}(Number)); }
	};

	cs::Dictionary<ContactKey, float> contactToastAt;
	cs::List<ContactKey> staleContactToasts;

	/// <summary>The belt is scanned a slice a frame, over this many frames, for rocks near the hull.</summary>
	static constexpr int BeltScanFrames = 30;

	/// <summary>A belt asteroid this near the hull (km) is tested every frame until the next scan.</summary>
	static constexpr double BeltCandidateKm = 3000;

	cs::List<int> beltCandidates, beltScanning;
	cs::List<float> beltXyz; // C# null until the first scan: empty here
	int beltCursor = 0;

	/// <summary>Finite-difference step for velocities, days.</summary>
	static constexpr double VelocityStepDays = 1e-5;

	/// <summary>
	/// Tests and resolves the piloted hull's contacts this frame (it has just moved from
	/// <paramref name="posBefore"/>). Returns false if the hull was lost in one, the session already ended.
	/// </summary>
	bool UpdatePilotCollisions(const cs::Ref<Core::Ship>& ship, Core::DVec3 posBefore, float dt);

	/// <summary>
	/// A contact, caught even on a fast pass: the volumes are tested where they end the frame and, if they
	/// are apart there but their bounding spheres crossed on the way (<see cref="Collisions.TimeOfImpact"/>),
	/// at steps from first touch on, so a hull at cruise speed cannot slip through a rock between frames.
	/// </summary>
	static Core::Impact Collide(Core::CollisionVolume& me, Core::CollisionVolume& other, Core::DVec3 meBefore, double dt);

	/// <summary>
	/// The piloted hull's share of an impact: the damage done to its struck side (through its battle if it
	/// is in one, so a kill there is the battle's), a toast, and the hull lost if that was the end of it.
	/// Returns false if it was.
	/// </summary>
	bool Struck(const cs::Ref<Core::Ship>& ship, Core::CollisionVolume& me, const Core::Impact& hit, const ContactKey& what, const cs::String& name);

	/// <summary>A hull that comes down hard on a world (or a big station, or one of the belt's own worlds)
	/// takes the damage of the sink speed it lost there on its underside. Returns false if it was lost.</summary>
	bool SurfaceImpact(const cs::Ref<Core::Ship>& ship, int body, Core::DVec3 outward, double sinkKmS);

	/// <summary>Scans a slice of the belt this frame for rocks within <see cref="BeltCandidateKm"/> of the hull;
	/// a full pass replaces the list tested every frame.</summary>
	void ScanBelt(const cs::Ref<Core::AsteroidField>& field, Core::DVec3 hullHelio, double jd);

	/// <summary>Drops a contact's last-toast time once it is old enough that the suppression window no longer
	/// cares about it, so <see cref="contactToastAt"/> cannot grow without bound over a long session and never
	/// has to be wiped wholesale (U.SPC-T366, the same treatment BattleFx.PruneSeen gives its own
	/// seen-dictionary).</summary>
	void PruneContactToasts(float now);

	/// <summary>A body's own velocity, heliocentric km/s.</summary>
	Core::DVec3 FrameVelocityKmS(int body, double jd);

	static Core::DVec3 VelocityKmS(const cs::Func<Core::DVec3, double>& helioAt, double jd);

	static cs::String Speed(double kmS);
