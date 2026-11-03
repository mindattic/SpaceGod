#pragma once
// Port of Unity.SpaceGod Runtime/Views/BattleFx.cs (US-T30).
//
// C#: [SettingsSource]. Attribute discovery becomes explicit registration (docs/MIGRATION.md): the engine side must
// register this class's settings with `SettingsRegistry::AddDeclarationSource(touch)`, where touch calls
// `Views::BattleFx::EffectsBudget()` (its one setting, in C# field order).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class War;
	class RangeSetting;
	struct CombatFx;
	enum class FxKind : int;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Draws every battle's weapons fire as one dynamic mesh of camera-facing quads: laser beams and
	/// tracers, point-defence streams, missiles with their trails, impacts, shield flashes and
	/// explosions. Effects are timed in real seconds (so they read at any time warp) and kept a few
	/// pixels wide at any distance, so a battle is visible from across a planetary system.
	///
	/// All of the actual hit/miss/travel-time decisions live in Core (<see cref="Battle"/>) -- this
	/// class only draws what already happened: a beam sweeps onto its target and leaves a scorch, a
	/// kinetic round (<see cref="Battle.KineticRound"/>) is a travelling tracer like a missile rather
	/// than a single instant line, and every direct-fire shot gets a brief muzzle flash and recoil kick
	/// at the gun itself.
	/// </summary>
	class BattleFx final
	{
		enum class Shape : int
		{
			Radial = 0,
			Beam = 1,
			Ring = 2,

			/// <summary>A shield bubble segment: a hex lattice with a ripple from the impact; the flash's age rides
			/// in the shape slot's fraction (3 + age), see the Fx shader.</summary>
			Bubble = 3,
		};

		struct Effect
		{
			cs::Ref<Core::Battle> Battle;
			Core::FxKind Kind{};
			Core::DVec3 A, B;
			double Size = 0;
			::SpaceGod::Color Color;
			float Born = 0, Life = 0;
			cs::Ref<Core::BattleUnit> Unit;

			/// <summary>A shield hit's impact direction in the struck hull's own frame (right, up, forward), so its
			/// bubble stays on the arc that took it as the hull turns, and the generator covering that arc.</summary>
			Core::DVec3 Local;
			int Sub = 0;
		};

		UnitySpaceApp* app;
		cs::Ref<Mesh> mesh;
		cs::List<Effect> effects;
		cs::Dictionary<cs::Ref<Core::Battle>, int> seen;
		cs::List<cs::Ref<Core::Battle>> staleSeen;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;

	public:
		/// <summary>The render budget for weapons fire on screen at once (U.SPC-T431): past it the oldest effects go first.</summary>
		static const cs::Ref<Core::RangeSetting>& EffectsBudget();

	private:
		static int MaxEffects();
		static constexpr int MaxNewPerBattlePerFrame = 160;

	public:
		BattleFx(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

	private:
		void Collect(const cs::Ref<Core::Battle>& b, float now);

		/// <summary>Drops a battle's seen-count once it is no longer live or kept in <see cref="War.Ended"/>,
		/// so the dictionary cannot grow without bound and never has to be wiped wholesale.</summary>
		void PruneSeen(const cs::Ref<Core::War>& war);

		void Spawn(const cs::Ref<Core::Battle>& b, const Core::CombatFx& fx, float now);

		void Draw(const Effect& e, float now);

		/// <summary>The shield shell's radius in ship lengths: between the inner and outer edge of ShipView's ring.</summary>
		static constexpr double BubbleRadius = 0.62;

		/// <summary>Half-angle of the patch of shell a hit lights, degrees.</summary>
		static constexpr float BubbleHalfAngleDeg = 38.0f;

		static constexpr int BubbleGrid = 7;

		/// <summary>
		/// A shield hit (U.SPC-T40): a segment of the shield bubble flashes around the hull at the impact, a hex
		/// lattice with a ripple running out from the hit, in the colour of the generator whose arc took it at
		/// that arc's live charge, so a hard-pressed arc flashes dim and translucent and a fresh one bright. An arc
		/// that has collapsed (or whose generator is shot away) shows nothing: its hits go to the hull and draw as
		/// impacts. A hull too small on screen for a patch to read gets the old ring flash, in the same colour.
		/// </summary>
		/// <summary>A tachyon lance's shot (U.SPC-T487): the full line at once, a hot core inside a wide violet glow that
		/// narrows as it fades, a charge flare at the emitter and, on a hit, a burst where it struck.</summary>
		void DrawLance(const Effect& e, float age, float fade);

		void DrawShieldHit(const Effect& e, float t, float fade);

		void DrawMissiles(const cs::Ref<Core::Battle>& b);

		/// <summary>
		/// A railgun slug or autocannon round actually crossing the range: drawn as a moving tracer with
		/// a short trail, exactly like a missile, rather than the single appear-and-fade line a direct-fire
		/// hit/miss event still gets for its own instant muzzle flash (see <see cref="Draw"/>). A round that
		/// missed keeps flying and fading here same as a round that's still closing on a hit.
		/// </summary>
		void DrawRounds(const cs::Ref<Core::Battle>& b);

		/// <summary>Mines lying in wait: a dim dot, blinking fast once armed (U.SPC-T271).</summary>
		void DrawMines(const cs::Ref<Core::Battle>& b);

		static Color SideColour(const cs::Ref<Core::Battle>& b, int side, Color fallback);

		Vector3 Point(const cs::Ref<Core::Battle>& b, Core::DVec3 local);

		float PixelSize(Vector3 p, float pixels);

		void Sprite(Vector3 c, float radius, Color color, Shape shape);

		void Ribbon(Vector3 a, Vector3 b, float pixels, Color color);

		void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color, Shape shape);

		void AddCorners(Color color, Shape shape);

		void Indices(int i);
	};
}
