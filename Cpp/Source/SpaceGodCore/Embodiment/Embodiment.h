#pragma once
// Port of Unity.SpaceGod Core/Embodiment/Embodiment.cs (US-T21, see docs/MIGRATION.md).
//
// An interface's properties are pure virtual methods (Name(), Kind(), ...), as the other ported interfaces do.
// C# compares seats with object.Equals / GetHashCode, which each seat overrides ("seats compare by what they
// control"); IEmbodiable carries them as virtual Equals / GetHashCode, and EmbodiableEquals / EmbodiableIndexOf /
// EmbodiableKey (not C# types) give List.Contains / IndexOf and Dictionary keys those semantics.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Faction;

	/// <summary>
	/// What kind of man-made thing an <see cref="IEmbodiable"/> is (U.SPC-T18), for display and grouping only:
	/// how control works is each implementation's own, and nothing branches on this to decide it.
	/// </summary>
	enum class EmbodimentKind : int
	{
		Ship,

		/// <summary>A launched fighter or bomber, one of a carrier's squadrons (U.SPC-T422).</summary>
		Fighter,
		Turret,
		Station,
		Satellite,

		/// <summary>An EVA repair worker out on a hull, one of its damage control teams (US-T135, <see cref="RepairWorker"/>).</summary>
		RepairWorker,

		/// <summary>A mech (US-T83, the Unreal.Soulslike mech, not ported yet): reserved so the picker, the HUD and co-op
		/// already know the kind; it will embody on <see cref="EmbodimentInputProfile.Walker"/>.</summary>
		Mech,

		/// <summary>A power-suited marine (US-T82, boarding, not built yet): reserved as <see cref="Mech"/> is.</summary>
		Marine,
	};
	SPACEGODCORE_API const char* ToString(EmbodimentKind v);

	/// <summary>
	/// Which set of controls a kind takes the keys and mouse on while held (US-T135). The input layer reads the same hands
	/// for every kind (<see cref="EmbodimentInput"/>) and owns them exclusively while anything is held (US-T90); the profile
	/// only says which keys mean what there, for the HUD's role panel and the keys the app adds on top (a cockpit's lock,
	/// a worker's weld). A new kind picks one of these; a kind with controls of its own adds one.
	/// </summary>
	enum class EmbodimentInputProfile : int
	{
		/// <summary>A ship's helm, flown by the pilot loop: stick, throttle, strafe, roll, the helm's abilities.</summary>
		Helm,
		/// <summary>A launched craft's cockpit: the dogfight stick, lock, flares, wing orders.</summary>
		Cockpit,
		/// <summary>A gun mount: the mouse lays it, the trigger fires it.</summary>
		Gunner,
		/// <summary>A whole station: every gun laid on the aim, the second trigger launches its craft.</summary>
		Station,
		/// <summary>A satellite: point it, nudge it on its thrusters.</summary>
		Satellite,
		/// <summary>EVA on a hull (US-T135): W/S A/D crawl the plating where the mouse looks, Shift the suit's jets, left mouse
		/// the welding torch's timing.</summary>
		HullWalker,
		/// <summary>A walker on its feet (US-T83 mech, US-T82 marine): W/S A/D walk, Shift run, mouse look, left/right mouse its
		/// arms. Reserved: nothing uses it until those kinds land; the app's walker hands go to it unchanged.</summary>
		Walker,
	};
	SPACEGODCORE_API const char* ToString(EmbodimentInputProfile v);

	/// <summary>How the camera rides a kind while it is held (US-T135): the app's camera layer places the eye from this, never
	/// from the kind's type. The flight camera itself (how a chase swings and leads, US-T129) is the camera's own.</summary>
	enum class EmbodimentCameraKind : int
	{
		/// <summary>A chase about the thing held, <see cref="EmbodimentCameraRig.ChaseRadii"/> of it out.</summary>
		Chase,
		/// <summary>The eye on a mount, looking down its barrel (a gun).</summary>
		Mount,
		/// <summary>Over the shoulder of something small on a bigger hull (an EVA worker): the eye a little above and behind
		/// <see cref="EmbodimentSubject.PointKm"/>, sized in the host hull's radii so it reads the same at either scale.</summary>
		Shoulder,
	};

	struct EmbodimentCameraRig
	{
		EmbodimentCameraKind Kind = EmbodimentCameraKind::Chase;

		/// <summary>For a chase: how far out, in radii of what is followed.</summary>
		double ChaseRadii = 2.5;

		/// <summary>For a mount or a shoulder: how far off the hull the eye sits, in the host hull's radii.</summary>
		double Rise = 0.04;

		/// <summary>For a shoulder: how far behind the subject the eye sits, in the host hull's radii.</summary>
		double Back = 0;

		EmbodimentCameraRig() = default;
		EmbodimentCameraRig(EmbodimentCameraKind kind, double chaseRadii, double rise = 0.04, double back = 0)
			: Kind(kind), ChaseRadii(chaseRadii), Rise(rise), Back(back) {}
	};

	/// <summary>One key of a role and what it does there, for the role panel.</summary>
	struct EmbodimentAbility
	{
		cs::String Key, Label;

		/// <summary>False when it cannot be used now (greyed on the panel).</summary>
		bool Ready = true;

		EmbodimentAbility() = default;
		EmbodimentAbility(cs::String key, cs::String label, bool ready = true) : Key(std::move(key)), Label(std::move(label)), Ready(ready) {}
	};

	/// <summary>A role's live meter on the panel (US-T135: the EVA worker's weld timing): a bar 0..1 with a marker
	/// sweeping it and a sweet zone to hit, and a progress fill underneath. <see cref="Show"/> false for none.</summary>
	struct EmbodimentMeter
	{
		bool Show = false;
		cs::String Label;
		double Marker = 0, ZoneLo = 0, ZoneHi = 0;
		double Progress = 0;
		/// <summary>The skill read-out (0..1) and a word for the last action ("GOOD WELD", "SPUTTER"), empty for none.</summary>
		double Quality = 0;
		cs::String Verdict;
		bool VerdictGood = false;
	};

	/// <summary>
	/// The HUD's role panel for whatever is held (US-T135): what it is, how it stands, and every key of its role. Built by
	/// the kind itself, so the HUD draws one panel for every kind and a new kind brings its own.
	/// </summary>
	struct EmbodimentRolePanel
	{
		/// <summary>The role, upper case ("FIGHTER", "GUN", "EVA REPAIR").</summary>
		cs::String Title;

		/// <summary>One line of its state (a cockpit's readout, a gun's reload, the worker's job).</summary>
		cs::String Status;

		cs::List<EmbodimentAbility> Abilities;

		EmbodimentMeter Meter;

		/// <summary>The verb the toast uses on taking it ("Flying", "Manning", "Working").</summary>
		cs::String Verb;
	};

	class Battle;
	class BattleUnit;
	class Ship;
	class Subsystem;

	/// <summary>
	/// What an embodied thing physically is in the world (US-T135): the hull it flies or rides (<see cref="Ship"/>, its unit
	/// in <see cref="Battle"/>), the part of it (<see cref="Part"/>: a gun's mount, the subsystem a worker is on), and the
	/// battle-frame point it is at. The camera rig, the HUD and who-embodies-what (<see cref="ISessionState.Held"/>, for a
	/// co-op player's glint on the units others hold) read this, never the kind's type. Any of it may be null.
	/// </summary>
	struct EmbodimentSubject
	{
		cs::Ref<Core::Ship> Ship;
		cs::Ref<Core::Battle> Battle;
		cs::Ref<BattleUnit> Unit;
		cs::Ref<Subsystem> Part;

		/// <summary>Where it is in its battle's frame (km), when <see cref="HasPoint"/>; else the unit's own position stands.</summary>
		bool HasPoint = false;
		DVec3 PointKm;
	};

	/// <summary>
	/// Where an embodied thing is and which way it faces: km from the centre of <see cref="FrameBody"/>
	/// (ecliptic axes), the same frame-body-plus-offset convention the pilot loop and battles use.
	/// </summary>
	struct EmbodimentPose
	{
		int FrameBody = 0;
		DVec3 OffsetKm;

		/// <summary>Unit facing (a hull's nose, a turret's barrel) and roof.</summary>
		DVec3 Forward, Up;

		EmbodimentPose() = default;

		EmbodimentPose(int frameBody, DVec3 offsetKm, DVec3 forward, DVec3 up)
		{
			FrameBody = frameBody;
			OffsetKm = offsetKm;
			Forward = forward;
			Up = up;
		}
	};

	/// <summary>
	/// One frame of the player's control, already in world terms (the Runtime input layer's job), for
	/// whatever is embodied. Each kind reads what it has: a turret its aim and trigger, a hull all of it.
	/// </summary>
	struct EmbodimentInput
	{
		/// <summary>Where the player wants it pointed (unit, ecliptic axes); zero holds the current aim.</summary>
		DVec3 Aim;

		/// <summary>Forward/reverse thrust, strafe and roll, each -1..1, for things that move.</summary>
		double Throttle = 0, StrafeRight = 0, StrafeUp = 0, Roll = 0;

		/// <summary>The main trigger (guns) and the secondary (missiles), held this frame.</summary>
		bool Primary = false, Secondary = false;

		/// <summary>The afterburner, held this frame, for things that move (U.SPC-T424).</summary>
		bool Boost = false;

		/// <summary>A ship's helm (U.SPC-T449, <see cref="ShipHelm"/>): the cruise key held, Ctrl held (fine control, a
		/// hauler's docking mode in a port's approach), and the engine telegraph rung this frame (+1 ahead, -1 astern).</summary>
		bool Cruise = false, Fine = false;
		int TelegraphStep = 0;

		/// <summary>The manoeuvre key held (Alt, US-T87): the hull swings its nose onto the retrograde for a turn-and-burn
		/// (<see cref="PilotFlight.FlipAim"/>), sliding on along its track as it turns. At a capital ship's helm it is the
		/// broadside stance instead (US-T81): the turn keys translate the hull and the helm lays it beam-on to the locked
		/// hull (<see cref="CapitalFlightModel.BeamOn"/>).</summary>
		bool Manoeuvre = false;

		/// <summary>Drift held (R, US-T165, <see cref="Drift"/>): a fighter's dogfight model let go, the hull coasting on its
		/// way while it turns free onto the aim; let go, a boost-out in the new facing. A capital's helm ignores it.</summary>
		bool Drift = false;

		/// <summary>The reactor split asked for this frame (US-T94, <see cref="PowerDistribution.Code"/>), -1 for no change:
		/// whole, not a step, so a command carried twice (co-op, a replay) lands the same. See <see cref="CraftPower"/>.</summary>
		int PowerSplit = -1;

		/// <summary>The self-destruct asked for this frame (US-T97, a <see cref="SelfDestructKey"/>): 0 nothing, 1 arm, 2 abort.
		/// A command, not a toggle, so one carried twice (co-op, a replay) lands the same. See <see cref="SelfDestruct"/>.</summary>
		int SelfDestruct = 0;

		/// <summary>The helm ability ordered this frame (US-T106, a <see cref="HelmAbility"/>), 0 for none: launch a squadron,
		/// recall the wing, an order to it, a shuttle, a Breacher or the pods at the locked hull. A command, landed once.</summary>
		int HelmAbility = 0;

		/// <summary>The sensor and power command at a helm this frame (US-T93, a <see cref="SensorKey"/>), 0 for none: an active
		/// ping, go dark, or power up. A command, landed once. See <see cref="SensorPosture"/>.</summary>
		int Sensor = 0;
	};

	/// <summary>
	/// Anything man-made the player can take charge of (U.SPC-T18, see docs/EMBODIMENT.md): a ship's helm, a
	/// turret on a hull or station, a station, a satellite. The thing's own AI holds it whenever the player
	/// does not; <see cref="TakeCharge"/> and <see cref="HandBack"/> only change who holds it, so the AI picks
	/// up exactly where the player left off.
	/// </summary>
	class IEmbodiable
	{
	public:
		virtual ~IEmbodiable() = default;

		virtual cs::String Name() const = 0;
		virtual EmbodimentKind Kind() const = 0;
		virtual cs::Ref<Core::Faction> Faction() const = 0;

		/// <summary>What it is mounted on (a turret's hull or station), or null for a free hull.</summary>
		virtual cs::Ref<IEmbodiable> Host() const = 0;

		/// <summary>Whether the player can take it right now; when not, <see cref="Blocker"/> says why.</summary>
		virtual bool CanEmbody() const = 0;
		/// (Empty stands for C#'s null: no blocker.)
		virtual cs::String Blocker() const = 0;

		/// <summary>Whether the player holds it now.</summary>
		virtual bool Embodied() const = 0;

		virtual EmbodimentPose PoseAt(double jd) = 0;

		/// <summary>The player takes it: its AI lets go.</summary>
		virtual void TakeCharge() = 0;

		/// <summary>One frame of the player's control, while <see cref="Embodied"/>.</summary>
		virtual void Control(const EmbodimentInput& input, double dt) = 0;

		/// <summary>Its AI takes it back, from wherever the player left it.</summary>
		virtual void HandBack() = 0;

		// ---------------------------------------------------------------- the universal embody contract (US-T135)
		// Enter and leave are TakeCharge / HandBack above. Every kind also says, for itself, which controls it takes, how the
		// camera rides it, what its HUD role panel shows, and what it physically is: the app's switching layer, the HUD and
		// co-op read only these, so a new kind (a mech, US-T83; a marine, US-T82) plugs in without a type check anywhere.

		/// <summary>Which controls it takes while held (it owns them exclusively, US-T90).</summary>
		virtual EmbodimentInputProfile InputProfile() const = 0;

		/// <summary>How the camera rides it.</summary>
		virtual EmbodimentCameraRig CameraRig() const = 0;

		/// <summary>Its HUD role panel: its state and the keys of its role.</summary>
		virtual EmbodimentRolePanel RolePanel() = 0;

		/// <summary>What it is in the world: its hull, its part of it, where it is.</summary>
		virtual EmbodimentSubject Subject() const = 0;

		/// C# object.Equals(object) / GetHashCode(): reference identity unless a seat overrides them.
		virtual bool Equals(const cs::Ref<IEmbodiable>& obj) const { return obj.get() == this; }
		virtual size_t GetHashCode() const { return std::hash<const void*>{}(this); }
	};

	/// C# static object.Equals(a, b): both null, or a.Equals(b).
	inline bool EmbodiableEquals(const cs::Ref<IEmbodiable>& a, const cs::Ref<IEmbodiable>& b)
	{
		if (a == b) return true;
		if (!a || !b) return false;
		return a->Equals(b);
	}

	/// C# List&lt;IEmbodiable&gt;.IndexOf (EqualityComparer.Default, so the seats' Equals).
	inline int EmbodiableIndexOf(const cs::List<cs::Ref<IEmbodiable>>& list, const cs::Ref<IEmbodiable>& item)
	{
		for (int i = 0; i < list.Count(); i++)
			if (EmbodiableEquals(list[i], item)) return i;
		return -1;
	}

	/// A Dictionary&lt;IEmbodiable, ...&gt; key: equal and hashed by the seat's own Equals / GetHashCode, as .NET's default comparer.
	struct EmbodiableKey
	{
		cs::Ref<IEmbodiable> Value;
		EmbodiableKey() = default;
		EmbodiableKey(cs::Ref<IEmbodiable> v) : Value(std::move(v)) {}
		bool operator==(const EmbodiableKey& o) const { return EmbodiableEquals(Value, o.Value); }
		size_t HashCode() const { return Value ? Value->GetHashCode() : 0; }
	};
}
