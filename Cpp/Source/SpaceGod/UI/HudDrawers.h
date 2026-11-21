#pragma once

// HudDrawers (US-T438): the HUD's drawers, a registry and a window manager drawn with the IMGUI layer (UnityCompat/
// UnityGui.h). docs/HUD.md has the player's view of it.
//
// A drawer is a panel's content and how it starts out (DrawerSpec: id, title, default edge and size, a group to start in,
// whether it starts pulled out or pinned, when it can show at all, an alert hook, and the draw callback that fills the
// content rect it is handed). Drawers live in windows (DrawerWindow): a window holds one drawer, or several docked to each
// other (a group), shown as tabs (one at a time) or stacked (each with its own small header, the height split between
// them). A window is:
//   - shown (pulled out) or closed back to its drawers' edge tabs;
//   - open or collapsed (its title bar only);
//   - pinned or not: a pinned window stays on screen in the minimal HUD (US-T439), the rest show only in HUD mode;
//   - docked to a screen edge (its order along it and its size) or floating (its rect), always kept on screen.
// The title bar moves it: a click folds it (collapse), a drag carries it; let go over another window's title bar it joins
// that window's group, near a screen edge it docks there (stacked with what is docked there), elsewhere it floats. A
// group's tab dragged out of its strip (or a stacked member's header) tears that drawer off into a window of its own. The
// title bar's buttons pin it, switch a group between tabs and a stack, and close it. With the HUD mode on, every edge shows
// the closed drawers whose home is there as pull-out tabs: a click pulls one out, a drag tears it off floating. A drawer's
// alert (DrawerAlert) makes its tab glow, and shows the tab, with the alert's line, in the minimal HUD too.
//
// Every frame goes through the HUD's Panel()/RegisterUi (the Frame's callbacks), so the world under a drawer takes no clicks
// and world labels under it are hidden. The layout math is HudDock.h's, unit-checked. The whole layout saves to a JSON object
// (Save/Load) for presets.
//
// Converting a panel (US-T440): give it an id and a DrawerSpec whose `draw` takes the content Rect and draws what the panel
// drew inside its own rect (no Panel() call, no HudLayout::Take: the manager has placed and framed it), `available` when it
// only shows sometimes (at the controls, in a fight), `alert` for anything urgent; Register it in Hud::RegisterDrawers. Open,
// Close, Toggle and Visible drive it from keys; RectOf / PublishedRect say where it is drawn (the tutorial's outline).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"
#include "UI/HudDock.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace SpaceGod::Shared::Json
{
	class JsonObject;
}

namespace SpaceGod::UI
{
	/// What a drawer raises: nothing (0), a notice (1: its tab glows the accent) or something urgent (2: it pulses red).
	struct DrawerAlert
	{
		int level = 0;
		/// One line, shown on its tab in the minimal HUD.
		cs::String text;
	};

	/// A drawer: its content and how it starts out.
	struct DrawerSpec
	{
		/// Stable: layouts are saved by it.
		cs::String id;
		cs::String title;
		/// Where it docks by default, and its size there (w x h; a 0 along the edge fills the edge's room).
		HudDock::Edge edge = HudDock::Edge::Right;
		Vector2 size = Vector2(260, 200);
		/// Pulled out to begin with (else closed to its edge tab); pinned to begin with.
		bool shown = true;
		bool pinned = false;
		/// Drawers naming the same group start out in one window, as tabs, in the order registered.
		cs::String group;
		/// Whether it can show now (null: always). A window none of whose drawers can is not drawn, nor are their tabs.
		std::function<bool()> available;
		/// What it raises now (null: never).
		std::function<DrawerAlert()> alert;
		/// Draws the content into the rect handed (GUI units, screen space). Called once per GUI pass while it shows.
		std::function<void(Rect)> draw;
	};

	/// A window: one drawer, or a group of them docked to each other.
	struct DrawerWindow
	{
		std::vector<cs::String> members;
		/// The tab shown (tabbed); stacked groups show every member.
		int active = 0;
		bool tabbed = true;
		bool shown = true, collapsed = false, pinned = false;
		bool docked = true;
		HudDock::Edge edge = HudDock::Edge::Right;
		int order = 0;
		/// Docked: w x h (0 along the edge: fill).
		Vector2 size = Vector2(260, 200);
		/// Floating: where.
		Rect floating = Rect(200, 200, 320, 240);

		// ---- not saved
		/// Opened from the minimal HUD by a key or its tab: shown there until closed, or the HUD mode changes.
		bool peek = false;
		/// How far pulled out, 0..1 (eased).
		float slide = 0;
		/// Where it was laid out this pass (the whole frame), and its home edge.
		Rect rect;
		/// Drawn this pass (laid out and visible).
		bool drawn = false;
	};

	class HudDrawers final
	{
	public:
		/// The HUD's styles a window is drawn with.
		struct Styles
		{
			cs::Ref<GUIStyle> panel, title, small, button, buttonActive;
		};

		/// What a pass draws with: the mode, the screen, each edge's room (where docked windows stack and the edge tabs lie),
		/// the HUD's Panel()/RegisterUi, and its styles.
		struct Frame
		{
			bool hudMode = true;
			float screenW = 1920, screenH = 1080;
			HudDock::Box bands[HudDock::EdgeCount];
			std::function<Rect(Rect)> panel;
			std::function<void(Rect)> registerUi;
			Styles styles;
		};

		static constexpr float TitleH = 24, SectionH = 18, TabH = 22, Pad = 4;

		// ---------------------------------------------------------------- the registry

		/// Adds a drawer (its default window too, unless its group's window is made already; after a layout was loaded or
		/// reset, a new drawer gets its default window at once).
		void Register(DrawerSpec spec);
		bool Has(const cs::String& id) const;
		const DrawerSpec* Spec(const cs::String& id) const;
		const std::vector<DrawerSpec>& Specs() const { return specs; }

		/// Every drawer back where its spec starts it.
		void ResetLayout();

		// ---------------------------------------------------------------- driving it

		/// Pulls the drawer's window out with it the tab shown and opened (not collapsed). <paramref name="peek"/>: shown in
		/// the minimal HUD too (a key opened it).
		void Open(const cs::String& id, bool peek = false);
		/// Closes the drawer's window back to its tabs.
		void Close(const cs::String& id);
		/// Open unless it is visible now, then closed. True when it is open after.
		bool Toggle(const cs::String& id, bool peek = false);
		/// Whether it is drawn now with its content showing (its window drawn, not collapsed, it the tab shown).
		bool Visible(const cs::String& id) const;
		/// Whether its window is pulled out (whatever the mode).
		bool Shown(const cs::String& id) const;

		void SetPinned(const cs::String& id, bool pinned);
		bool Pinned(const cs::String& id) const;
		void SetCollapsed(const cs::String& id, bool collapsed);
		/// Its window floating at <paramref name="r"/> (kept on screen when drawn).
		void Float(const cs::String& id, Rect r);
		/// Its window docked on <paramref name="edge"/> at <paramref name="order"/> among the windows there.
		void Dock(const cs::String& id, HudDock::Edge edge, int order = 1000);
		/// <paramref name="id"/>'s window joins <paramref name="into"/>'s (as its last tab, shown).
		void Join(const cs::String& id, const cs::String& into);
		/// Tears the drawer off its group into a window of its own, floating at <paramref name="r"/>.
		void Detach(const cs::String& id, Rect r);
		/// Its window's group shown as tabs (else stacked).
		void SetTabbed(const cs::String& id, bool tabbed);

		/// The drawer's window grown (dir 1) or shrunk (-1) a step across its edge (a floating one in height). False when it
		/// was already as small as it goes and dir is -1 (the caller may close it).
		bool Grow(const cs::String& id, int dir);

		/// The index of the window holding the drawer (-1: none).
		int WindowOf(const cs::String& id) const;
		const std::vector<DrawerWindow>& Windows() const { return windows; }

		/// Where the drawer's window was drawn this pass (empty when it is not).
		Rect RectOf(const cs::String& id) const;
		/// The same, for code with no Hud at hand (the tutorial's outline): as the last Repaint drew it.
		static Rect PublishedRect(const cs::String& id);

		/// A window is being dragged (or a press on a title or tab may become a drag).
		bool Dragging() const { return drag.kind != DragKind::None; }

		// ---------------------------------------------------------------- per frame

		/// Once a frame: the pull-out eases, and the minimal HUD's peeks dropped when the mode changes.
		void Tick(float dt, bool hudMode);

		/// The box the windows docked on <paramref name="edge"/> and drawn this frame take together (empty: none), for the HUD
		/// to keep its other panels clear of them. Lays out as Draw will (call before drawing the panels).
		HudDock::Box EdgeExtent(HudDock::Edge edge, const Frame& f);

		/// Lays out and draws every window and edge tab, and handles the drags. Once per GUI pass, over the HUD's panels.
		void Draw(const Frame& f);

		// ---------------------------------------------------------------- saving

		/// The layout (every window and what it holds) as a JSON object.
		cs::Ref<Shared::Json::JsonObject> Save() const;
		/// A layout Save wrote back in; drawers it does not name keep their default windows, names it has that are not
		/// registered are dropped. False (the layout unchanged) when it is no layout at all.
		bool Load(const cs::Ref<Shared::Json::JsonObject>& layout);

	private:
		enum class DragKind { None, Pending, Move, Resize };
		enum class DragFrom { Title, Tab, Section, EdgeTab, Grip };
		struct Drag
		{
			DragKind kind = DragKind::None;
			DragFrom from = DragFrom::Title;
			int window = -1;
			cs::String member;
			Vector2 down, grab;
			Rect start;
		};

		/// The control id the drags hold (GUIUtility::hotControl): fixed, so a drag survives panels coming and going under it.
		static constexpr int DragControl = 2000000000 + 438;
		static constexpr float EdgeReach = 28, DragStart = 5, MinW = 160, MinH = 60;

		std::vector<DrawerSpec> specs;
		std::vector<DrawerWindow> windows;
		bool laidOut = false;
		bool lastHudMode = true;
		Drag drag;
		/// The snap the drag would make if let go now (drawn as a preview).
		HudDock::Snap preview;
		bool previewing = false;
		/// This pass's press: the topmost window under it (-1: none), the only one whose title takes it.
		int topHit = -1;
		Frame last;
		bool haveLast = false;

		static inline std::unordered_map<cs::String, Rect> published;

		int SpecIndex(const cs::String& id) const;
		bool Available(const cs::String& id) const;
		DrawerAlert AlertOf(const cs::String& id) const;
		/// The window's members that can show now.
		std::vector<int> AvailableMembers(const DrawerWindow& w) const;
		/// Shown, and in this mode (pinned, peeked or the HUD mode on), and one of its drawers can show.
		bool VisibleNow(const DrawerWindow& w, bool hudMode) const;
		HudDock::Edge HomeEdge(const DrawerWindow& w, const Frame& f) const;
		DrawerWindow DefaultWindow(const DrawerSpec& s) const;
		void Renumber(HudDock::Edge edge);
		void Activate(DrawerWindow& w, const cs::String& id);
		/// A new window holding only <paramref name="id"/>, torn off its own, floating at r; its index.
		int TearOff(const cs::String& id, Rect r);
		void RemoveEmpty();

		void Layout(const Frame& f);
		void DrawWindow(int index, const Frame& f);
		void DrawEdgeTabs(const Frame& f);
		void HandleDrag(const Frame& f);
		void Drop(const Frame& f);
		void DrawPreview(const Frame& f);
		std::vector<HudDock::Box> TitleStrips(int except) const;
	};
}
