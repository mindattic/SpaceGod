#pragma once
// Port of Unity.SpaceGod Runtime/Views/ShipView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

#include <tuple>

namespace SpaceGod::Core
{
	class Ship;
	class ShipCombat;
	class Subsystem;
	class CapturedRock;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The 3D model of one ship. Warships carry their hardpoints as separate parts: turrets traverse
	/// and elevate onto whatever they are shooting, a hardpoint that is shot away leaves a blackened
	/// stub and a fire, engines glow while they work, the hull shows more breaches as its hit points
	/// fall, and a destroyed ship breaks into bow and stern that tumble apart and burn out.
	/// </summary>
	class ShipView final
	{
	public:
		cs::Ref<Core::Ship> Ship;
		cs::Ref<GameObject> Root;

		/// <summary>The combat state the hardpoints were built from: a yard refit (U.SPC-T65) gives the ship a new one, and the view is rebuilt.</summary>
		cs::Ref<Core::ShipCombat> BuiltFor;

	private:
		cs::List<cs::Ref<Renderer>> renderers;
		cs::Ref<MeshRenderer> plume;
		cs::Ref<ParticleSystem> exhaust;
		bool exhaustPlaying = false;
		MaterialPropertyBlock block;
		cs::Ref<Transform> stern, bow;

		class Part;
		class Glow;
		cs::List<cs::Ref<Part>> parts;
		cs::List<cs::Ref<Glow>> glows;
		cs::List<cs::Ref<Glow>> breaches;
		bool visible = true;

		/// <summary>A truss freighter's hull (U.SPC-T482), whose mesh is swapped as its load changes the pods clamped on
		/// it, and what that mesh shows now; null for any other hull.</summary>
		cs::Ref<MeshFilter> truss;
		std::tuple<int, int, int> trussShown;
		/// <summary>A kit-built truss freighter's containers (US-T110), front pair first: the first as many as its load
		/// fills are shown.</summary>
		cs::List<cs::Ref<Renderer>> cargo;

		/// <summary>What an ore tug has latched on (U.SPC-T498), drawn ahead of its push plate, and the load it shows now; null until it first carries one.</summary>
		cs::Ref<MeshRenderer> towRenderer;
		cs::Ref<MeshFilter> towFilter;
		cs::Ref<Core::CapturedRock> towShown;

		/// <summary>Whether the full model is drawn (U.SPC-T415): false draws the hull alone, without hardpoints, scars, glows, shields or exhaust.</summary>
		bool detailed = true;
		float wreckSpin = 0;

		// Shield fields: one disc per shield emitter over the patch of hull it covers (U.SPC-T501), built once
		// (geometry is fixed) and recoloured each frame from that emitter's own field. Built for every ship, not
		// just warships, so a freighter's shields show too.
		cs::List<cs::Ref<Core::Subsystem>> shieldGenerators;
		cs::Ref<MeshRenderer> shieldRingRenderer;
		cs::Ref<Mesh> shieldRingMesh;
		cs::List<int> shieldWedgeStart, shieldWedgeCount;
		cs::List<Color> shieldVertexColors;

		static constexpr const char* EngineGlowId = "_EngineGlow";
		static constexpr const char* HighlightId = "_Highlight";
		static constexpr const char* ColorId = "_Tint";

		/// <summary>Breaches open as the hull falls below these fractions.</summary>
		static const cs::List<float>& BreachAt();

	public:
		ShipView(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent);

		/// <summary>
		/// A shield generator's colour at <paramref name="charge"/> (0 to 1 of its cap): dim and translucent when
		/// nearly drained, bright when full. Its patch's disc and the bubble a hit raises on that patch
		/// (<see cref="BattleFx"/>, U.SPC-T40) share it, so a bubble reads as the patch that took the hit.
		/// </summary>
		static Color ShieldColor(float charge)
		{
			return Color::Lerp(Color(0.1f, 0.25f, 0.5f, 0.22f), Color(0.4f, 0.75f, 1.0f, 1.0f), Mathf::Clamp01(charge));
		}

		// ---------------------------------------------------------------- per frame

		void Apply(Vector3 relative, Quaternion rotation, float length, bool burning, bool highlighted);

		/// <summary>
		/// The middle of the hull as drawn (the box round its hull meshes: sections, an imported craft's parts, a
		/// truss and its load; not the drive plume, exhaust or shield FX), in the root's own frame, where the ship
		/// is one unit long. A hull is not built symmetric about its root (a carrier's island, a freighter's pods),
		/// so the focus camera orbits this point rather than the root (US-T78).
		/// </summary>
		Vector3 HullCentre() const;

		/// <summary>The box round the hull as drawn (see <see cref="HullCentre"/>), in the root's own frame where the ship is
		/// one unit long: where its RCS thrusters sit (US-T165, <see cref="RcsJets"/>). Empty before the hull is placed.</summary>
		Bounds HullBox() const;

		/// <summary>The full model is on screen this frame (visible and detailed): what an FX on the hull rides on.</summary>
		bool Shown() const { return visible && detailed; }

		/// <summary>The hull's own renderers, the ones HullCentre measures.</summary>
		const cs::List<cs::Ref<Renderer>>& HullRenderers() const { return renderers; }

		/// <summary>
		/// Shows the hull's combat state: turrets on their targets (or at rest), lost hardpoints as
		/// scars and fires, engines that still work glowing, breaches by hull damage.
		/// </summary>
		void ApplyCombat(const cs::Ref<Core::ShipCombat>& combat, bool fighting, bool thrusting, float now, float dt);

		/// <summary>
		/// A destroyed ship: bow and stern drift apart and tumble, the hull blackens, fires burn at
		/// the break and on the wreck, then the pieces burn out and vanish.
		/// </summary>
		/// <returns>False once the wreck has burnt out.</returns>
		bool Wreck(Vector3 relative, Quaternion rotation, float length, float age, float now);

		/// <summary>How long a destroyed ship burns (see <see cref="Wreck"/>) before it lingers as a cold hulk (see <see cref="BurntOut"/>), real seconds.</summary>
		static constexpr float WreckBurnS = 28.0f;

		/// <summary>
		/// A destroyed ship once its fires are out, for as long as its wreck lasts: a cold hulk tumbling like a
		/// derelict, but still broken in two where it died, with its guns and masts gone and their scars left.
		/// </summary>
		void BurntOut(Vector3 relative, Quaternion rotation, float length, float now);

		/// <summary>A boarded hulk left adrift: dark, cold, slowly tumbling, never burning out.</summary>
		void Derelict(Vector3 relative, Quaternion rotation, float length, float now);

		/// <summary>
		/// Full model or simplified (U.SPC-T415, <see cref="RenderLod"/>): a ship too small on screen for its hardpoints to
		/// read draws its hull alone, its turrets, scars, fires, breaches, glows, shield ring and exhaust put away, and
		/// stops aiming and billboarding them. Back to full detail, every hardpoint is shown as it now stands.
		/// </summary>
		void SetDetail(bool full);

		void SetVisible(bool value);

	private:
		static void EnsureStatics();
		static cs::Ref<Material> GlowMaterial(const cs::String& name, Color c, float intensity, float falloff);
		static cs::Ref<ParticleSystem> BuildExhaust(const cs::Ref<Transform>& parent);
		void BuildShieldRing();
		void UpdateShieldRing(bool fighting);
		cs::Ref<Transform> SectionFor(float z) const;
		cs::Ref<Part> BuildPart(const cs::Ref<Core::Subsystem>& sub);
		cs::Ref<Glow> NewGlow(const cs::Ref<Transform>& parent, Vector3 localPos, float size, const cs::Ref<Material>& material, const cs::Ref<Part>& owner);
		void UpdateTow();
		void SetExhaust(bool want);
		void Aim(Part& p, const cs::Ref<Core::Subsystem>& sub, bool fighting, Quaternion inverseShip, float dt);
		void Billboard(float now);
		void PlaceBrokenHalves(float age);
		void HardStopExhaust();
		void SetBlock();
	};
}
