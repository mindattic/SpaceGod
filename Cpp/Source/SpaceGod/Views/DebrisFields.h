#pragma once
// Port of Unity.SpaceGod Runtime/Views/DebrisFields.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Wreck;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The drifting debris cloud round every lasting <see cref="Wreck"/>: dark tumbling shards that spread
	/// slowly from the hull (<see cref="Wreck.DebrisOffsetKm"/>) and shrink away as the wreck breaks up
	/// (<see cref="Wreck.Fade"/>). Drawn at the battle scale, where the fight that made it was drawn.
	/// </summary>
	class DebrisFields final
	{
		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::Dictionary<cs::Ref<Core::Wreck>, cs::List<cs::Ref<Transform>>> fields;
		cs::List<cs::Ref<Core::Wreck>> gone;
		MaterialPropertyBlock block;
		static constexpr const char* ColorId = "_Tint"; // Shader.PropertyToID("_Tint")

		// C#'s static `static Mesh shard;`: shared by every DebrisFields.
		static cs::Ref<Mesh>& shard();

	public:
		DebrisFields(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

	private:
		cs::List<cs::Ref<Transform>> Create(const cs::Ref<Core::Wreck>& w);
	};
}
