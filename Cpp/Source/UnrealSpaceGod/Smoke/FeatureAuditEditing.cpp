#include "Smoke/FeatureAuditEditing.h"

#include "ModelViewer.h"
#include "UnityCompat/GuiHost.h"
#include "UnityCompat/UnityGui.h"
#include "UnityCompat/UnityObjects.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityScene.h"

namespace SpaceGod::Smoke::FeatureAudit
{
	using namespace SpaceGod::UnityCompat;

	namespace
	{
		// ---------------------------------------------------------------- text editing (US-T52)

		/// The probe's field: drawn first in every pass (its own OnGUI handler), so it takes its events before the HUD.
		struct Field
		{
			cs::String text;
			Rect rect = Rect(400, 300, 300, 24);
		};
		Field field;
		ScriptedGuiInput gin;
		float guiTime = 0;

		void GuiStep()
		{
			GuiHost::ScriptedFrame(gin, guiTime);
			gin.Clear();
		}

		void Key(KeyCode code, char32_t ch = 0, int mods = 0)
		{
			gin.modifiers = static_cast<EventModifiers>(mods);
			gin.keysDown.push_back({ code, ch });
			GuiStep();
			gin.modifiers = EventModifiers::None;
		}

		constexpr int Shift = static_cast<int>(EventModifiers::Shift), Ctrl = static_cast<int>(EventModifiers::Control);

		/// A press and release of the left button at x (GUI pixels, the field's middle row); `soon`: within a double
		/// click's time of the last press (Unity's clickCount counts on), else a second later.
		void Click(float x, bool soon = false, int mods = 0)
		{
			guiTime += soon ? 0.1f : 1.0f;
			gin.modifiers = static_cast<EventModifiers>(mods);
			gin.mouse = Vector2(x, field.rect.y + field.rect.height * 0.5f);
			GuiStep(); // the move
			gin.pressed[0] = gin.held[0] = true;
			GuiStep();
			gin.held[0] = false;
			gin.released[0] = true;
			GuiStep();
			gin.modifiers = EventModifiers::None;
		}

		cs::String State()
		{
			auto s = GUI::FocusedTextEditor();
			return cs::Cat("'", field.text, "' cursor ", s.cursor, " select ", s.select);
		}

		bool Is(const cs::String& text, int cursor, int select)
		{
			auto s = GUI::FocusedTextEditor();
			return s.id != 0 && field.text == text && s.cursor == cursor && s.select == select;
		}
	}

	void TextEditProbes(const EditingReport& result)
	{
		static int owner;
		cs::String clipboard = GUIUtility::systemCopyBuffer();
		guiTime = Time::unscaledTime + 1000;
		field.text = "alpha beta";
		GuiHost::Register(&owner, [] { field.text = GUI::TextField(field.rect, field.text, 64); }, -1000);
		float left = field.rect.x + 1, right = field.rect.xMax() - 2, middle = field.rect.x + field.rect.width * 0.5f;

		// The first click into a field focuses it and selects its text (TextEditor.OnFocus, as the C# track's).
		Click(middle);
		result("text.click-focus", Is("alpha beta", 10, 0), cs::Cat("click: ", State()));

		// The caret: Home, typing at it, End, Left, Ctrl+Left/Right by word, Up/Down to the ends.
		cs::String steps;
		bool ok = true;
		auto step = [&](const char* what, const cs::String& text, int cursor, int select)
		{
			bool here = Is(text, cursor, select);
			ok = ok && here;
			steps += cs::Cat(steps.empty() ? "" : ", ", what, " ", GUI::FocusedTextEditor().cursor, here ? "" : cs::Cat(" (", State(), ")"));
		};
		Key(KeyCode::Home);
		step("Home", "alpha beta", 0, 0);
		Key(KeyCode::X, U'X');
		step("'X'", "Xalpha beta", 1, 1);
		Key(KeyCode::End);
		step("End", "Xalpha beta", 11, 11);
		Key(KeyCode::LeftArrow);
		step("Left", "Xalpha beta", 10, 10);
		Key(KeyCode::LeftArrow, 0, Ctrl);
		step("Ctrl+Left", "Xalpha beta", 7, 7);
		Key(KeyCode::LeftArrow, 0, Ctrl);
		step("Ctrl+Left", "Xalpha beta", 0, 0);
		Key(KeyCode::RightArrow, 0, Ctrl);
		step("Ctrl+Right", "Xalpha beta", 6, 6);
		Key(KeyCode::DownArrow);
		step("Down", "Xalpha beta", 11, 11);
		Key(KeyCode::UpArrow);
		step("Up", "Xalpha beta", 0, 0);
		result("text.caret-keys", ok, cs::Cat("'Xalpha beta': ", steps));

		// Shift extends the selection; an arrow without it collapses it to that side.
		steps.clear();
		ok = true;
		Key(KeyCode::End, 0, Shift);
		step("Shift+End", "Xalpha beta", 11, 0);
		Key(KeyCode::LeftArrow);
		step("Left", "Xalpha beta", 0, 0);
		Key(KeyCode::End);
		Key(KeyCode::LeftArrow, 0, Shift | Ctrl);
		step("End, Shift+Ctrl+Left", "Xalpha beta", 7, 11);
		result("text.shift-select", ok, steps);

		// Ctrl+C / A / V / X through the system clipboard; typing over a selection replaces it.
		steps.clear();
		ok = true;
		Key(KeyCode::C, 0, Ctrl);
		cs::String copied = GUIUtility::systemCopyBuffer();
		ok = copied == "beta";
		steps = cs::Cat("Ctrl+C '", copied, "'");
		Key(KeyCode::A, 0, Ctrl);
		step(", Ctrl+A", "Xalpha beta", 11, 0);
		Key(KeyCode::V, 0, Ctrl);
		step(", Ctrl+V", "beta", 4, 4);
		Key(KeyCode::LeftArrow, 0, Shift);
		Key(KeyCode::LeftArrow, 0, Shift);
		step(", Shift+Left x2", "beta", 2, 4);
		Key(KeyCode::X, 0, Ctrl);
		cs::String cut = GUIUtility::systemCopyBuffer();
		ok = ok && cut == "ta";
		step(cs::Cat(", Ctrl+X '", cut, "'").c_str(), "be", 2, 2);
		Key(KeyCode::A, 0, Ctrl);
		Key(KeyCode::Z, U'z');
		step(", Ctrl+A 'z'", "z", 1, 1);
		result("text.clipboard", ok, steps);

		// Backspace / Delete, and with Ctrl a word.
		steps.clear();
		ok = true;
		field.text = "alpha beta";
		GuiStep();
		Key(KeyCode::Home);
		Key(KeyCode::Delete);
		step("Home Delete", "lpha beta", 0, 0);
		Key(KeyCode::End);
		Key(KeyCode::Backspace);
		step("End Backspace", "lpha bet", 8, 8);
		Key(KeyCode::Backspace, 0, Ctrl);
		step("Ctrl+Backspace", "lpha ", 5, 5);
		Key(KeyCode::Home);
		Key(KeyCode::Delete, 0, Ctrl);
		step("Home Ctrl+Delete", " ", 0, 0);
		result("text.delete", ok, steps);

		// The mouse: a click places the caret, a drag selects, a double click a word, Shift+click extends, a triple
		// click the whole line.
		steps.clear();
		ok = true;
		field.text = "alpha beta gamma";
		GuiStep();
		Click(right);
		step("click right", "alpha beta gamma", 16, 16);
		guiTime += 1.0f;
		gin.mouse = Vector2(left, field.rect.y + 12);
		GuiStep();
		gin.pressed[0] = gin.held[0] = true;
		GuiStep();
		gin.mouse = Vector2(right, field.rect.y + 12);
		GuiStep();
		gin.held[0] = false;
		gin.released[0] = true;
		GuiStep();
		step("drag left->right", "alpha beta gamma", 16, 0);
		Click(field.rect.x + 8);
		Click(field.rect.x + 8, true);
		step("double click 'alpha'", "alpha beta gamma", 5, 0);
		Click(right, false, Shift);
		step("Shift+click right", "alpha beta gamma", 16, 0);
		Click(middle);
		Click(middle, true);
		Click(middle, true);
		step("triple click", "alpha beta gamma", 16, 0);
		result("text.mouse", ok, steps);

		GuiHost::Unregister(&owner);
		GUIUtility::keyboardControl = 0;
		GUIUtility::SetSystemCopyBuffer(clipboard);
	}

	// ---------------------------------------------------------------- the model viewer's camera (US-T53)

	void ModelViewerProbes(const EditingReport& result, const std::function<void(int)>& frames)
	{
		Camera* main = Camera::main();
		if (!ModelViewer::OpenForCheck(0))
		{
			result("viewer.screen-camera", false, "no model viewer, or no models under Resources/Ships");
			return;
		}
		frames(3);
		Camera* cam = ModelViewer::CheckCamera();
		Camera* screen = UnityScene::ScreenCamera();
		const auto open = UnityScene::Last();
		int layerBit = 1 << ModelViewer::CheckLayer();
		result("viewer.screen-camera", cam && screen == cam && cam->depth == 1000 && cam->cullingMask == layerBit && !main->enabled,
			cs::Cat("F9 on '", ModelViewer::CheckModelName(), "': the screen camera is the viewer's: ", cam && screen == cam ? "True" : "False",
				" (depth ", cam ? cs::Str(cam->depth) : cs::String("-"), ", mask ", cam ? cam->cullingMask : 0, " = layer ", ModelViewer::CheckLayer(),
				"); the game's enabled: ", main->enabled ? "True" : "False"));
		bool masked = open.Visible > 0 && open.VisibleLayers == layerBit && open.Masked > 0;
		result("viewer.culling-mask", masked, cs::Cat(open.Visible, " drawn (the backdrop and the model; layers mask ", open.VisibleLayers, "), ",
			open.Masked, " of the game's left out", masked ? cs::String() : cs::Cat("; the model: ", ModelViewer::CheckDescribe())));
		result("viewer.backdrop", open.Backdrop, cs::Cat("SolidColor (0.05, 0.06, 0.09) backdrop drawn: ", open.Backdrop ? "True" : "False"));
		Light* key = UnityScene::KeyLightOf(cam);
		result("viewer.key-light", key != nullptr && (key->cullingMask & ~layerBit) == 0,
			cs::Cat("its own key light under its camera: ", key ? "True" : "False", key ? cs::Cat(" (intensity ", cs::Str(key->intensity), ", mask ",
				key->cullingMask, "), the game's camera sees it: ", UnityScene::KeyLightOf(main) ? "True" : "False") : cs::String()));

		ModelViewer::CloseForCheck();
		frames(3);
		const auto closed = UnityScene::Last();
		result("viewer.close", UnityScene::ScreenCamera() == main && main->enabled && closed.Masked == 0 && !closed.Backdrop,
			cs::Cat("closed: the screen camera is the game's: ", UnityScene::ScreenCamera() == main ? "True" : "False", ", ", closed.Masked,
				" left out, backdrop ", closed.Backdrop ? "True" : "False"));
	}
}
