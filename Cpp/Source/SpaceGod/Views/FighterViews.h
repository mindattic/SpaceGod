#pragma once
// Port of Unity.SpaceGod Runtime/Views/FighterViews.cs (US-T30).

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
	class FlightDeck;
}

namespace SpaceGod::Views
{
	class LineStrip;

	/// <summary>
	/// Every fighter in every battle: its hull, an engine glow kept a few pixels across so a squadron
	/// reads as a cluster of sparks from afar, and a fading trail that traces its runs and break-offs. Craft down on a
	/// flight deck are drawn on it, and every hull's flight decks (U.SPC-T518) are drawn as lit frames round their stern
	/// and bow mouths, with the glide path astern while a craft is on it, once the hull is big enough on screen to show them.
	/// </summary>
	class FighterViews final
	{
		class View final
		{
		public:
			cs::Ref<GameObject> Hull;
			cs::Ref<Transform> Glow;
			cs::Ref<LineStrip> Trail;
			cs::List<Core::DVec3> History;
			float NextSample = 0;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> root;
		cs::Dictionary<cs::Ref<Core::BattleUnit>, cs::Ref<View>> views;

		/// <summary>A flight deck's lights: the frames round its stern and bow mouths and its glide path.</summary>
		class DeckView final
		{
		public:
			cs::Ref<LineStrip> Stern, Bow, Path;
		};

		/// <summary>Screen-space minimum (US-T202): the spark shows from this many pixels of hull, at full size from MarkFullPx,
		/// and fades out as the hull passes MarkFadePx (then its mesh is readable and only its engine halo stays).</summary>
		static constexpr double MarkFloorPx = 0.1, MarkFullPx = 0.5, MarkFadePx = 12.0;

		static constexpr int TrailLength = 24;
		static constexpr float SampleSeconds = 0.12f;

		cs::Dictionary<cs::Ref<Core::FlightDeck>, cs::Ref<DeckView>> deckViews;
		cs::List<Vector3> framePoints = cs::NewArray<Vector3>(5);
		cs::List<Vector3> trailPoints = cs::NewArray<Vector3>(TrailLength);

		// C#'s statics `static Material glowMaterial, shipMaterial; static Mesh quad;`: shared by every FighterViews.
		static cs::Ref<Material>& glowMaterial();
		static cs::Ref<Material>& shipMaterial();
		static cs::Ref<Mesh>& quad();

	public:
		FighterViews(UnitySpaceApp* app, const cs::Ref<Transform>& root);

		void Update();

		/// <summary>The model <paramref name="u"/> is drawn as (one unit long, nose +Z), while it is drawn as a model at all;
		/// null otherwise. What an FX on the craft rides on (its RCS jets, US-T165).</summary>
		cs::Ref<Transform> ModelOf(const cs::Ref<Core::BattleUnit>& u) const;

	private:
		/// <summary>Half the width and height of a deck's mouth, in its hull's lengths, and at least so many km.</summary>
		static constexpr double MouthHalfWidth = 0.014, MouthHalfHeight = 0.007, MouthMinKm = 0.03;

		static constexpr Color DeckOpen = Color(0.55f, 0.95f, 0.6f, 0.9f), DeckClosed = Color(1.0f, 0.35f, 0.3f, 0.6f);

		/// <summary>
		/// <paramref name="carrier"/>'s flight decks (U.SPC-T518): a lit frame round each mouth, green while the deck is open and
		/// red once its hangar is shot away, and the glide path from the approach gate to the stern mouth while a craft is on
		/// it. Drawn only while the hull is big enough on screen (<see cref="FlightDecks.Lod"/>).
		/// </summary>
		void DrawDecks(const cs::Ref<Core::Battle>& b, const cs::Ref<Core::BattleUnit>& carrier, double ppu, cs::HashSet<cs::Ref<Core::FlightDeck>>& liveDecks);

		/// <summary>The lit frame round a deck's mouth <paramref name="along"/> km down its axis.</summary>
		void Frame(const cs::Ref<Core::Battle>& b, const cs::Ref<Core::FlightDeck>& d, double along, double w, double h, const cs::Ref<LineStrip>& strip, Color color);

		cs::Ref<View> Create(const cs::Ref<Core::BattleUnit>& u);
	};
}
