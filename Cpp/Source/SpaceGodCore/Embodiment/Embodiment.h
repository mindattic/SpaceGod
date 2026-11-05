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
	};
	SPACEGODCORE_API const char* ToString(EmbodimentKind v);

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
