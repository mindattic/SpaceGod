#pragma once

// Icons: the C++ port of Unity.SpaceGod's Runtime/UI/Icons.cs (a static class). Each C# static property
// (Icons.White, Icons.Prograde ...) is a static function returning the lazily made texture.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	enum class ShipClass : int;
}

namespace SpaceGod::UI
{
	/// Procedurally drawn white icon sprites (tinted at draw time) that stand in for anything too
	/// small or too far away to see: each ship class and body kind has a distinct silhouette.
	class Icons final
	{
	public:
		static const cs::Ref<Texture2D>& White();

		/// The piloting HUD's prograde marker: a ring with a centre dot and three ticks (U.SPC-T59).
		static const cs::Ref<Texture2D>& Prograde();

		/// The retrograde marker: the same ring and ticks, crossed through instead of dotted.
		static const cs::Ref<Texture2D>& Retrograde();

		/// A sensor contact not yet resolved to a hull class (U.SPC-T66): a small cross in a broken square.
		static const cs::Ref<Texture2D>& Blip();

		/// A contact the scanners have lost, marked where it was last seen: a question mark.
		static const cs::Ref<Texture2D>& Lost();

		/// An off-screen target's edge arrow (US-T128): a notched arrowhead pointing up, turned at draw time.
		static const cs::Ref<Texture2D>& EdgeArrow();

		static cs::Ref<Texture2D> ForBody(const cs::Ref<Core::BodyDefinition>& body);
		static cs::Ref<Texture2D> ForShip(Core::ShipClass c);

		/// On-screen icon size in pixels.
		static float PixelSize(Core::ShipClass c);
		static float PixelSize(const cs::Ref<Core::BodyDefinition>& body);

	private:
		static constexpr int Size = 64;
	};
}
