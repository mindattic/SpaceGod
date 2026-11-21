#pragma once
// US-T432: where a hull's engines actually are -- the nozzles and bells its model draws -- for the flames and glows.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Rendering
{
	/// <summary>One engine nozzle: the middle of its mouth, the way its exhaust leaves and its radius, in some root's frame.</summary>
	struct EnginePort
	{
		Vector3 Position;
		Vector3 Exhaust = Vector3::back();
		float Radius = 0.02f;
		/// <summary>Which of the hull's Engine hardpoints the nozzle is the bell of (in layout order), or -1 for a nozzle of
		/// the hull itself.</summary>
		int Engine = -1;
	};

	/// <summary>
	/// Engine ports as the model is built (US-T432): every kit, and the procedural hulls, mark each nozzle they draw with an
	/// empty child named <see cref="Name"/> under the section it is part of (so it rides with that section when the hull
	/// breaks), at the nozzle's mouth, turned so its +Z is the exhaust and scaled to its radius. The views read them back in
	/// their root's frame (<see cref="Of"/>) and put the flames and glows on them: a hull's own nozzles (the pack's glowing
	/// exhausts, tools/usc_ports.py) and the bells bolted on its Engine hardpoints alike. A model with none marked (the
	/// Kenney craft) keeps the views' own placement.
	/// </summary>
	class EnginePorts
	{
	public:
		static constexpr const char* Name = "EnginePort";

		/// <summary>Marks a nozzle under <paramref name="parent"/> (its frame): mouth at <paramref name="at"/>, exhaust
		/// leaving along <paramref name="exhaust"/>, <paramref name="radius"/> across; <paramref name="engine"/> is the Engine
		/// hardpoint it is the bell of, or -1.</summary>
		static void Mark(const cs::Ref<Transform>& parent, Vector3 at, Vector3 exhaust, float radius, int engine = -1);

		/// <summary>Every port marked under <paramref name="root"/>, in its frame (through every transform between).</summary>
		static cs::List<EnginePort> Of(const cs::Ref<Transform>& root);

		/// <summary>The port marked under <paramref name="root"/> for Engine hardpoint <paramref name="engine"/>, with the
		/// section transform it was marked under, in that section's frame; false when there is none.</summary>
		static bool ForEngine(const cs::Ref<Transform>& root, int engine, cs::Ref<Transform>& section, EnginePort& port);
	};
}
