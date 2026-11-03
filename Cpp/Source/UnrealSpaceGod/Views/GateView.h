#pragma once
// Port of Unity.SpaceGod Runtime/Views/GateView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

#include <tuple>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class JumpGate;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Draws the active system's jump gates (see <see cref="Unity.SpaceGod.Core.JumpGate"/>), one for each
	/// link of the gate network: the same docking-ring mesh a world's docking rings use
	/// (<see cref="StructureViews"/>), reused rather than modelling a new structure, fixed at each
	/// gate's position with its axis along the line to the system it leads to so a ship flies straight
	/// through. The one a piloted ship can activate brightens (see <c>UnitySpaceApp.UpdateGateActivation</c>);
	/// one under blockade glows red.
	/// </summary>
	class GateView final
	{
		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::List<std::tuple<cs::Ref<Core::JumpGate>, cs::Ref<GameObject>, cs::Ref<MeshRenderer>>> views; // (gate, root, renderer)
		MaterialPropertyBlock block;
		static constexpr const char* EngineGlowId = "_EngineGlow"; // Shader.PropertyToID("_EngineGlow")
		static constexpr const char* EngineColorId = "_EngineColor"; // Shader.PropertyToID("_EngineColor")
		static constexpr const char* ColorId = "_Tint"; // Shader.PropertyToID("_Tint")
		static constexpr const char* HighlightId = "_Highlight"; // Shader.PropertyToID("_Highlight")

	public:
		/// <summary>Drawn this many km across (fiction, the same scale family as a docking ring).</summary>
		static constexpr double DiameterKm = 25;

	private:
		static constexpr Color IdleColor = Color(0.6f, 0.45f, 1.0f);
		static constexpr Color ReadyColor = Color(0.55f, 1.0f, 0.6f);
		static constexpr Color ShutColor = Color(1.0f, 0.3f, 0.25f);
		static constexpr Color DownColor = Color(0.35f, 0.32f, 0.3f);

	public:
		GateView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

	private:
		/// <summary>A view for every gate of the active system not yet drawn: those it started with, and any finished since (U.SPC-T97).</summary>
		void AddNew();

	public:
		void Update(bool visible, const cs::Ref<Core::JumpGate>& readyToActivate);
	};
}
