#pragma once

// SystemMapScreen (US-T258): the tactical map of the star system the player is in. The Hud owns one and calls Opened(),
// Draw() and DestroyOwnedTextures(), as it does the galaxy map.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Target.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class JumpGate;
	class StarSystem;
	struct LaneHop;
}

namespace SpaceGod::UI
{
	/// The system map (Shift+[, or the drawer's SYSTEM MAP): the star system the player is in, top-down over its ecliptic,
	/// in true km, so it reads the same at the realistic and the game scale. Every body on its orbit (moons come apart from
	/// their planet as the map zooms in), the settlements on them, the stations in orbit, the gas works, the hyperlanes with
	/// their ramps and the spurs to the jump gates, the gates themselves, the debris belts, ice fields, nebulae and
	/// graveyards, and the ships about as far as the sensors of the unit held make them out (the god's view sees them all,
	/// US-T93). Hovering anything gives its name, how far it is and Prose's short line for it where there is one
	/// (LoreShort); a click selects it as the navigator does. At a helm the panel on the right cruises there (the cruise
	/// drive, US-T127) or plots the lane route to it (the lanes to ride, the off-ramps' exits picked on the way), and a
	/// gate's COURSE plots the way over the gates to where it leads. Plots go through the co-op frames as the course does.
	/// Drag (or middle-drag) pans, the wheel zooms about the cursor; FIT frames the whole system, HERE the unit held.
	class SystemMapScreen final
	{
	public:
		explicit SystemMapScreen(UnitySpaceApp* app) : app(app) {}

		/// Opened (again): the whole system framed, the selection kept if it is still here.
		void Opened();

		void Draw();

		/// The map's own textures go with the HUD that owns it (U.SPC-T366).
		void DestroyOwnedTextures();

	private:
		enum class ItemKind : int { Star, Body, Site, Station, Gas, Gate, Ramp, Ship, Belt, IceField, Nebula, Graveyard, You };

		/// One thing on the map: where it is (km from the system's root), how it is drawn, what it stands for.
		struct Item
		{
			ItemKind Kind = ItemKind::Body;
			cs::String Key; // stable across rebuilds: the selection is kept by it
			cs::String Name, KindLabel, Details;
			Core::DVec3 Km;
			/// For an area (a belt, field, nebula or graveyard): its reach, km; a belt's inner edge too, and its host.
			double ReachKm = 0, InnerKm = 0;
			Core::DVec3 HostKm;
			Color Tint = Color::white();
			float Size = 3;
			/// What a click selects and a cruise is bound for (none for a gate, a ramp or an area).
			Target What = Target::None();
			/// The body it is at or nearest (-1: none): its parent's orbit and the lane route are worked out from it.
			int Body = -1;
			cs::Ref<Core::JumpGate> Gate;
			/// The Prose key for its short line, and what is shown when Prose has none.
			cs::String LoreKey, About;
			/// What it orbits, if anything is drawn for it: the orbit is drawn about it, and an item on top of its parent at
			/// this zoom (a moon by its planet seen whole-system) is drawn small, unlabelled and not picked.
			bool HasParent = false;
			Core::DVec3 ParentKm;
		};

		bool Crowded(Rect canvas, const Item& it) const;

		UnitySpaceApp* app;

		std::vector<Item> items;
		float builtAt = -10.0f;
		const Core::StarSystem* builtFor = nullptr;
		cs::String selectedKey, hoveredKey;

		/// The view: the km point at the canvas centre and km per pixel.
		Core::DVec3 centreKm;
		double kmPerPx = 1e6;
		bool fitted = false;
		bool dragging = false;

		bool showOrbits = true, showLanes = true, showContacts = true, showFields = true, showLabels = true;

		cs::Ref<GUIStyle> panel, title, text, small, label, labelDim, button, buttonOn, tip;
		cs::Ref<Texture2D> panelTex, buttonTex, buttonOnTex, glow, tipTex;

		void Build(double jd);
		void Fit(Rect canvas);
		void CentreOnYou(Rect canvas);
		void HandleInput(Rect canvas);
		const Item* Find(const cs::String& key) const;
		const Item* Nearest(Rect canvas, Vector2 at) const;

		Vector2 Project(Rect canvas, Core::DVec3 km) const;
		void DrawMap(Rect canvas, double jd);
		void DrawLanes(Rect canvas, double jd);
		void DrawRoute(Rect canvas, double jd);
		void DrawTip(Rect canvas, const Item& it);
		void DrawHeader(Rect r);
		void DrawLegend(Rect r);
		void DrawDetail(Rect r, double jd);

		/// The lane route to an item (null: none, or none needed) and its hours of riding; worked out, not plotted.
		cs::Ref<cs::List<Core::LaneHop>> RouteTo(const Item& it, double jd, double& rideHours);

		cs::String RangeOf(const Item& it);
		void Line(Vector2 a, Vector2 b, Color c, float thickness);
		void Circle(Rect canvas, Vector2 p, float r, Color c, float thickness);
		void Disc(Vector2 p, float r, Color c);
		void Label(Vector2 p, const cs::String& s, Color c, bool minor);
		void Toggle(float& y, float x, float w, bool& on, const cs::String& name);
		void Text(float& y, float x, float w, const cs::String& s, Color c);
		void EnsureStyles();
		static cs::Ref<Texture2D> SoftDisc();
	};
}
