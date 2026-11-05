#pragma once

// GalaxyMapScreen: the C++ port of Unity.SpaceGod's Runtime/UI/GalaxyMapScreen.cs (US-T31). The Hud owns one and
// calls Opened(), Draw() and DestroyOwnedTextures(), as the C# does.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Systems/Galaxy.h"           // NearbyStar (held by value)
#include "Systems/StrategicCommand.h" // CommandScope, ObjectiveKind (held by value)
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

#include <vector> // US-T47

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class StarSystem;
	class GalaxyMapModel;
	class MapLink;
	class Faction;
	class Ship;
	class PolityReport;
	class StrategicCommand;
	class World;
	class FtlTarget;
	struct Rgb;
	class PolityDirectives; // US-T47
	class ProductionFocus;
	class BuildPlan;
	class ResearchTrack;
	class Proposal;
	class Pact;
	class EnvoyOption;
}

namespace SpaceGod::UI
{
	/// The galaxy map (U.SPC-T103, [ or the navigator's GALAXY MAP): every simulated star system at its real position,
	/// seen from above the ecliptic and tilted to show depth, with the gate network between them. Each system is haloed
	/// in the colour of the polity that holds it; each gate link is drawn as thick as the traffic through it, with that
	/// traffic flowing along it; war fronts, shut gates, sieges and blockades are marked in red, raiders in orange, and
	/// gates being built dashed. All of it is what the player knows (GalaxyMap): the system they are in as
	/// it is, every other as the last news of it had it, and a system never heard from as a bare star.
	///
	/// Click a star to inspect it (the panel on the right), double-click or TRAVEL to fly there through the gates (the
	/// navigator's STAR SYSTEMS trip, UnitySpaceApp.TravelTo). At the helm of a hull with its own FTL drive
	/// (U.SPC-T420) any star or beacon, the plain stars on the charts among them, can be picked to plot an FTL course to
	/// (its ETA, fuel and any well on the way) and the FTL autopilot engaged for it. Drag to turn the map, shift-drag to pan,
	/// the wheel to zoom. The view fills the screen; its panels keep to the middle 21:9 band, so on a 32:9 screen they
	/// sit where the eye is rather than out at the far edges.
	class GalaxyMapScreen final
	{
	public:
		explicit GalaxyMapScreen(UnitySpaceApp* app) : app(app) {}

		/// Opened (again): starts on the system the player is in, framed to fit.
		void Opened();

		void Draw();

		/// The map's own styles are going with the HUD that owns it (U.SPC-T366): DontSave outlives the scene
		/// otherwise, so every game session that opened the map would leave its textures behind.
		void DestroyOwnedTextures();

		// ---------------------------------------------------------------- US-T47: co-op (GalaxyMapScreen.Coop.cpp)

		/// A frame's map action on every peer (the C# track's GalaxyMapScreen.CoopExecute): what the button does, by the same
		/// objects read back from their keys, its toast the asker's. False for a verb the map does not know.
		static bool CoopExecute(UnitySpaceApp* app, const cs::String& verb, const std::vector<cs::String>& a);
		/// F12 (UnitySpaceApp::HailPolity): the hail and its answer at its frame on every peer, the envoy opened on the hailer's.
		static bool CoopSendsHail(UnitySpaceApp* app, const cs::Ref<Core::Faction>& from, const cs::Ref<Core::Faction>& to);
		/// The smoke run's clicks (CoopSmoke): the speaker arrows, Production, an EXPLORE for the whole navy, SEND A GIFT.
		void CoopSmokeSpeak(const cs::Ref<Core::Faction>& f);
		void CoopSmokeProduction();
		void CoopSmokeExplore(const cs::Ref<Core::StarSystem>& target);
		void CoopSmokeGift(const cs::Ref<Core::Faction>& them);

	private:
		static bool CoopSendsText(UnitySpaceApp* app, const cs::String& verb, const cs::String& args);
		/// command->Player = f in co-op: a "speak" entry (true when sent, or already the party's / already asked for).
		bool CoopSpeaks(const cs::Ref<Core::Faction>& f);
		/// The envoy screen, drawn: in co-op it speaks for the party's polity rather than writing its own pick back each frame.
		void CoopAdoptSpeaker(const cs::List<cs::Ref<Core::Faction>>& states);
		/// The directives the COMMAND panel reads: in co-op what the Core holds, without making them (command->Of makes them).
		cs::Ref<Core::PolityDirectives> CoopDirectives(const cs::Ref<Core::StrategicCommand>& command, const cs::Ref<Core::Faction>& f);
		bool CoopSendsProduction(const cs::Ref<Core::ProductionFocus>& p);
		bool CoopSendsBuild(const cs::Ref<Core::BuildPlan>& p);
		bool CoopSendsResearch(const cs::Ref<Core::ResearchTrack>& t);
		bool CoopSendsOrder(Core::ObjectiveKind kind, const cs::Ref<Core::StarSystem>& target, const cs::Ref<Core::World>& from, const cs::Ref<Core::Ship>& charge);
		bool CoopSendsClear(const cs::Ref<Core::World>& from);
		bool CoopSendsAnswer(const cs::Ref<Core::Proposal>& p, bool accept);
		bool CoopSendsBreak(const cs::Ref<Core::Pact>& p);
		bool CoopSendsPropose(const cs::Ref<Core::EnvoyOption>& o, const cs::Ref<Core::Faction>& them);
		bool CoopSendsGift(const cs::Ref<Core::Faction>& them, double amount);
		bool CoopSendsWar(const cs::Ref<Core::Faction>& them);
		static inline cs::Ref<Core::Faction> speakSent;
		static inline long long speakSentAt = -1000;

		UnitySpaceApp* app;
		cs::Ref<Core::GalaxyMapModel> model;
		float builtAt = -10.0f;
		cs::Ref<Core::StarSystem> builtFor;

		cs::Ref<Core::StarSystem> selected, hovered;

		/// A plain star on the charts picked or under the cursor (U.SPC-T420): nobody simulates it, but an FTL drive can go there.
		cs::Nullable<Core::NearbyStar> selectedStar, hoveredStar;

		/// The plain stars drawn, nearest the system the player is in first, gathered with the model.
		cs::List<Core::NearbyStar> chartStars;

		/// The most plain stars drawn: the charts run on far beyond what the map frames.
		static constexpr int MaxChartStars = 300;
		float yaw = 0, tilt = 0.55f, zoom = 1.0f;
		Vector2 pan;
		bool fitted = false;
		float fitScale = 1.0f;
		Core::DVec3 centreLy;

		bool showTerritory = true, showFlows = true, showFronts = true, showPirates = true, showConstruction = true;

		/// The strategy layer's COMMAND panel is up in place of the system detail (U.SPC-T423).
		bool commandMode = false;
		cs::Ref<Core::Faction> commanded;
		cs::Ref<Core::Ship> commandGroup;
		Core::CommandScope commandScope = Core::CommandScope::Group;
		Vector2 groupScroll;
		cs::Ref<Core::PolityReport> report;
		float reportAt = -10.0f;

		/// The DIPLOMACY panel (the envoy screen, U.SPC-T427) is up in place of the system detail; the state treated with.
		bool diplomacyMode = false, warArmed = false;
		cs::Ref<Core::Faction> envoyWith;
		Vector2 diplomacyScroll;
		float diplomacyHeight = 600;

		cs::Ref<GUIStyle> title, text, small, label, labelDim, button, buttonOn;
		cs::Ref<Texture2D> panelTex, buttonTex, buttonOnTex, glow;

		static constexpr Color Accent = Color(0.45f, 0.85f, 1.0f);
		static constexpr Color Dim = Color(0.62f, 0.7f, 0.78f);
		static constexpr Color Unknown = Color(0.5f, 0.54f, 0.6f);
		static constexpr Color FlowColor = Color(1.0f, 0.82f, 0.4f);
		static constexpr Color FrontColor = Color(1.0f, 0.3f, 0.26f);
		static constexpr Color PirateColor = Color(1.0f, 0.55f, 0.15f);
		static constexpr Color BuildColor = Color(0.75f, 0.8f, 0.9f);
		static constexpr Color CourseColor = Color(0.6f, 0.95f, 0.75f);

		/// The widest the panels spread, as width over height: 21:9, as the rest of the HUD.
		static constexpr float MaxPanelAspect = 21.0f / 9.0f;

		/// The middle band the panels keep to: the whole width up to 21:9, the middle 21:9 of anything wider.
		static float BandWidth();
		static float BandLeft();
		static float BandRight();

		// ---------------------------------------------------------------- projection

		/// Frames every system: centred on their middle, scaled so the farthest fits the canvas.
		void Fit(Rect canvas);

		/// A point (light-years) on the canvas: turned by the yaw about the ecliptic pole, tilted, scaled and panned.
		Vector2 Project(Rect canvas, Core::DVec3 ly, float& depth);

		Vector2 Project(Rect canvas, Core::DVec3 ly);

		/// The same point dropped to the ecliptic plane (z = the map's middle): where a star's stalk stands.
		Vector2 Foot(Rect canvas, Core::DVec3 ly);

		/// The plain stars on the charts nearest the system the player is in, up to MaxChartStars.
		void GatherChartStars();

		cs::Nullable<Core::NearbyStar> NearestStar(Rect canvas, Vector2 mouse);

		cs::Ref<Core::StarSystem> Nearest(Rect canvas, Vector2 mouse);

		// ---------------------------------------------------------------- input

		void HandleInput(Rect canvas);

		void Travel(const cs::Ref<Core::StarSystem>& to);

		// ---------------------------------------------------------------- the map

		void DrawMap(Rect canvas, double jd);

		/// Sparks along a link, as many as its traffic, each way in proportion to the crossings that way.
		void FlowDots(Vector2 a, Vector2 b, const cs::Ref<Core::MapLink>& l);

		void Barrier(Vector2 at, Vector2 toward);

		static Color StarColor(const cs::Ref<Core::StarSystem>& s);

		static Color Tint(const Core::Rgb& c, float a);

		static cs::String Age(double days);

		// ---------------------------------------------------------------- panels

		void DrawHeader(Rect r, double jd);

		void DrawLegend(Rect r);

		void DrawDetail(Rect r, double jd);

		// ---------------------------------------------------------------- command (U.SPC-T423)

		/// The states that can be commanded: every polity with a navy and a port somewhere.
		cs::List<cs::Ref<Core::Faction>> CommandableStates();

		/// The strategy layer (U.SPC-T423): one polity at a glance (its systems, people, money, navy and wars), its production,
		/// build and research priorities, every one of its fleet groups with what it is doing and what it was told to do, and
		/// objectives (take, hold, blockade, escort, explore) for the system picked on the map, given to one squadron, every
		/// group in a system, or the whole navy.
		void DrawCommand(Rect r, double jd);

		void Cycle(const cs::List<cs::Ref<Core::Faction>>& states, int step);

		template <class T>
		static cs::Ref<T> Next(const cs::List<cs::Ref<T>>& all, const cs::Ref<T>& current)
		{
			for (int i = 0; i < all.Count(); i++)
				if (all[i] == current) return all[(i + 1) % all.Count()];
			return all.Count() > 0 ? all[0] : nullptr;
		}

		void Give(const cs::Ref<Core::StrategicCommand>& command, Core::ObjectiveKind kind, const cs::Ref<Core::StarSystem>& target,
			const cs::Ref<Core::World>& from, double jd);

		// ---------------------------------------------------------------- diplomacy (U.SPC-T427)

		/// The envoy screen (U.SPC-T427): the state you speak for (the one commanded), the proposals other states have put to
		/// it waiting on your answer, every other state with its mood toward you and how you stand, and for the one picked
		/// its answer to your hail, the pacts between you (each can be broken), every deal your envoy can put with how they
		/// would take it (the same weighing the AI answers by), a gift, and a declaration of war (clicked twice).
		void DrawDiplomacy(Rect r, double jd);

		void CycleSpeaker(const cs::List<cs::Ref<Core::Faction>>& states, int step);

		static cs::String Capital(const cs::String& s);

		/// A plain star on the charts (U.SPC-T420): what is known of it, and an FTL course to it.
		void DrawStarDetail(Rect r, const Core::NearbyStar& star);

		/// FTL to `target` (U.SPC-T420): at the helm of a hull with its own drive, PLOT FTL COURSE works the
		/// course out (shown here once plotted: the run out, the jump, any wells on the way, the ETA and the fuel) and ENGAGE
		/// AUTOPILOT flies it.
		void DrawFtlSection(float& y, float x, float w, const cs::Ref<Core::FtlTarget>& target);

		static cs::String Days(double d);

		static cs::String People(double n);

		static cs::String Tonnes(double t);

		// ---------------------------------------------------------------- drawing helpers

		const cs::Ref<GUIStyle>& Panel();
		cs::Ref<GUIStyle> panelStyle;

		void Heading(float& y, float x, float w, const cs::String& s);

		void Line(float& y, float x, float w, const cs::String& s, Color c);

		void Toggle(float& y, float x, float w, bool& on, const cs::String& name, const cs::String& hint);

		void Swatch(float& y, float x, float w, Color c, const cs::String& s, bool dashed);

		void Label(Vector2 p, const cs::String& s, Color c, bool minor);

		static void Line(Vector2 a, Vector2 b, Color color, float thickness);

		static void Dashed(Vector2 a, Vector2 b, Color color, float thickness, float dash, float gap);

		void Disc(Vector2 p, float r, Color c);

		static void Ring(Vector2 p, float r, Color c, float thickness);

		static void Brackets(Vector2 p, float half, Color c);

		void EnsureStyles();

		/// A white disc fading out to its edge, tinted at draw time.
		static cs::Ref<Texture2D> SoftDisc();
	};
}
