// US-T147: the showcase tour (see Tour/ShowcaseTour.h): its registry, the runner, the strip on screen, the contact sheet,
// the sg.Tour console command and its dev menu entries.

#include "Tour/ShowcaseTour.h"

#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "Input/InputDevice.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Systems/Galaxy.h"
#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"

#include <algorithm>
#include <memory>

namespace SpaceGod::Tour
{
	using Input::InputDevice;
	using Input::Key;

	namespace
	{
		UWorld* GameWorld()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
				if (c.WorldType == EWorldType::Game || c.WorldType == EWorldType::PIE) return c.World();
			return nullptr;
		}

		UnitySpaceApp* RunningApp()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
			{
				if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
				auto* actor = AUnitySpaceAppActor::Find(c.World());
				if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
			}
			return nullptr;
		}

		cs::String Lower(const cs::String& s) { return cs::Strings::ToLowerInvariant(s); }

		cs::String CommandName(const cs::String& line)
		{
			auto space = line.find(' ');
			return space == cs::String::npos ? line : line.substr(0, space);
		}

		bool CommandExists(const cs::String& line)
		{
			return IConsoleManager::Get().FindConsoleObject(UTF8_TO_TCHAR(CommandName(line).c_str())) != nullptr;
		}

		FString F(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	}

	// ---------------------------------------------------------------- stops and the registry

	TourStop TourStop::Make(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption)
	{
		TourStop s;
		s.Chapter = chapter;
		s.Id = id;
		s.Title = title;
		s.Caption = caption;
		return s;
	}

	TourStop TourStop::Console(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption, const cs::String& command)
	{
		TourStop s = Make(chapter, id, title, caption);
		s.Setup = [command](UnitySpaceApp&) -> cs::String
		{
			if (!CommandExists(command)) return cs::Cat("not in this build yet (", CommandName(command), ")");
			if (GEngine) GEngine->Exec(GameWorld(), UTF8_TO_TCHAR(command.c_str()));
			return cs::String();
		};
		return s;
	}

	TourStop TourStop::Placeholder(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption, const cs::String& ticket)
	{
		TourStop s = Make(chapter, id, title, caption);
		s.Pending = ticket;
		s.System = cs::String();
		s.Scale = -1;
		s.Seconds = 4;
		return s;
	}

	std::vector<TourRegistry::Chapter>& TourRegistry::Mutable()
	{
		static std::vector<Chapter> chapters;
		return chapters;
	}

	TourRegistry::Chapter& TourRegistry::Find(const cs::String& name)
	{
		auto& all = Mutable();
		for (auto& c : all)
			if (c.Name == name) return c;
		Chapter c;
		c.Name = name;
		all.push_back(c);
		return all.back();
	}

	void TourRegistry::DeclareChapter(const cs::String& name, int order, const cs::String& blurb)
	{
		auto& c = Find(name);
		c.Order = order;
		c.Blurb = blurb;
		std::stable_sort(Mutable().begin(), Mutable().end(), [](const Chapter& a, const Chapter& b) { return a.Order < b.Order; });
	}

	void TourRegistry::Add(TourStop stop)
	{
		// A stop already there under this id (a placeholder for a feature now landed) gives way, wherever it was.
		bool removed = false;
		for (auto& c : Mutable())
		{
			for (auto it = c.Stops.begin(); it != c.Stops.end(); ++it)
				if (it->Id == stop.Id)
				{
					if (c.Name == stop.Chapter && it->Order == stop.Order)
					{
						*it = std::move(stop);
						return;
					}
					c.Stops.erase(it);
					removed = true;
					break;
				}
			if (removed) break;
		}
		auto& stops = Find(stop.Chapter).Stops;
		auto at = std::upper_bound(stops.begin(), stops.end(), stop.Order, [](int order, const TourStop& s) { return order < s.Order; });
		stops.insert(at, std::move(stop));
	}

	std::vector<void (*)()>& TourRegistry::Pending()
	{
		static std::vector<void (*)()> pending;
		return pending;
	}

	void TourRegistry::Defer(void (*registerStops)()) { Pending().push_back(registerStops); }

	const std::vector<TourRegistry::Chapter>& TourRegistry::Chapters()
	{
		while (!Pending().empty())
		{
			auto next = Pending().front();
			Pending().erase(Pending().begin());
			next();
		}
		// Placeholders were registered with the rest: a real stop under the same id, whenever it came, has replaced them.
		return Mutable();
	}

	std::vector<std::pair<int, const TourStop*>> TourRegistry::Flat()
	{
		std::vector<std::pair<int, const TourStop*>> out;
		const auto& chapters = Chapters();
		for (int c = 0; c < static_cast<int>(chapters.size()); c++)
			for (const auto& s : chapters[c].Stops) out.emplace_back(c, &s);
		return out;
	}

	// ---------------------------------------------------------------- the runner

	namespace
	{
		enum class Phase
		{
			Prepare,
			Ready,
			Show,
			Shoot,
			Shot,
			OtherScale,
		};

		/// What became of a stop in an auto run, for the contact sheet.
		struct Result
		{
			cs::String Id, Title, Caption, Chapter;
			cs::String Status; // ok, late (setup never said it landed), skipped (why), pending (its ticket)
			cs::String Note;
			std::vector<FString> Files;
		};

		struct State
		{
			bool running = false;
			std::vector<std::pair<int, const TourStop*>> stops;
			int index = 0;
			Phase phase = Phase::Prepare;
			double phaseT = 0; // real seconds in the phase
			double showT = 0;  // seconds shown (paused time does not count)
			bool paused = false;
			bool otherScale = false; // interactive: every stop again at the other scale
			bool onOther = false;    // this pass is the other scale's
			bool setupRan = false;
			bool late = false;
			cs::String skipped;     // why the stop could not be set up
			cs::String waiting;     // what the strip says the tour is waiting for
			cs::Ref<Core::StarSystem> home;
			bool autoShots = false, quit = false;
			FString shotsDir, pendingShot;
			std::vector<Result> results;
			double startedAt = 0;
		};

		State& S()
		{
			static State s;
			return s;
		}

		const TourStop* Current()
		{
			auto& s = S();
			return s.index >= 0 && s.index < static_cast<int>(s.stops.size()) ? s.stops[s.index].second : nullptr;
		}

		void Enter(Phase p)
		{
			S().phase = p;
			S().phaseT = 0;
		}

		void GoTo(int index)
		{
			auto& s = S();
			s.index = index;
			s.showT = 0;
			s.onOther = false;
			s.setupRan = false;
			s.late = false;
			s.skipped.clear();
			s.waiting.clear();
			s.paused = false;
			Enter(Phase::Prepare);
		}

		Result& ResultFor(const TourStop& stop)
		{
			auto& s = S();
			for (auto& r : s.results)
				if (r.Id == stop.Id) return r;
			Result r;
			r.Id = stop.Id;
			r.Title = stop.Title;
			r.Caption = stop.Caption;
			r.Chapter = stop.Chapter;
			s.results.push_back(r);
			return s.results.back();
		}

		cs::Ref<Core::StarSystem> SystemFor(UnitySpaceApp& app, const TourStop& stop)
		{
			if (stop.System.empty()) return nullptr;
			if (stop.System == TourStop::Home) return S().home;
			auto found = app.Galaxy->Find(stop.System);
			return found != nullptr ? found : app.Galaxy->Realize(stop.System, app.Clock->Jd);
		}

		/// Everything a stop needs before its own setup: out of any seat and panel, in its system, at its scale, the clock on.
		/// True once there (<see cref="State.waiting"/> says what it is waiting for meanwhile).
		bool Prepare(UnitySpaceApp& app, const TourStop& stop, double t)
		{
			auto& s = S();
			if (!stop.Pending.empty()) return true;
			if (t == 0)
			{
				if (app.CommsOpen()) app.CloseComms();
				if (!stop.KeepSeat && app.AtControls())
				{
					cs::String said;
					UI::CheatMenu::RunPath(app, "Helm/Release to command", said);
				}
			}
			if (app.InFtlCruise() && !stop.System.empty())
			{
				s.waiting = "between the stars: waiting for the drop-out";
				return false;
			}
			auto system = SystemFor(app, stop);
			if (system != nullptr && app.ActiveSystem != system)
			{
				s.waiting = cs::Cat("on the way to ", system->Name);
				if (!app.Travel->Active) app.TravelTo(system);
				return false;
			}
			if (app.Travel->Active)
			{
				s.waiting = "arriving";
				return false;
			}
			if (stop.Scale >= 0 && app.ScaleGoal != static_cast<double>(stop.Scale)) app.ToggleScale();
			if (app.ScaleAnimating())
			{
				s.waiting = "changing scale";
				return false;
			}
			if (app.Clock->Paused()) app.Clock->SetPaused(false);
			s.waiting.clear();
			return true;
		}

		void Shoot(const TourStop& stop)
		{
			auto& s = S();
			FString name = FString::Printf(TEXT("%02d-%s%s.png"), s.index + 1, *F(stop.Id), s.onOther ? TEXT("-other-scale") : TEXT(""));
			s.pendingShot = FPaths::Combine(s.shotsDir, name);
			IFileManager::Get().Delete(*s.pendingShot, false, true, true);
			FScreenshotRequest::RequestScreenshot(s.pendingShot, true, false);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Tour: shot %s"), *s.pendingShot);
		}

		void WriteContactSheet();

		void Finish(UnitySpaceApp* app)
		{
			auto& s = S();
			if (!s.running) return;
			s.running = false;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Tour: done after %.0f s"), FPlatformTime::Seconds() - s.startedAt);
			if (s.autoShots)
			{
				WriteContactSheet();
				if (s.quit) FPlatformMisc::RequestExit(false);
			}
			else if (app != nullptr) app->ShowToast("Showcase tour over");
		}

		/// The next stop, or the end.
		void Advance(UnitySpaceApp& app)
		{
			auto& s = S();
			if (s.index + 1 >= static_cast<int>(s.stops.size()))
			{
				Finish(&app);
				return;
			}
			GoTo(s.index + 1);
		}

		/// The first stop of chapter <paramref name="chapter"/> (by its index), or -1.
		int FirstOf(int chapter)
		{
			const auto& stops = S().stops;
			for (int i = 0; i < static_cast<int>(stops.size()); i++)
				if (stops[i].first == chapter) return i;
			return -1;
		}

		int ChapterCount()
		{
			return static_cast<int>(TourRegistry::Chapters().size());
		}

		void Keys(UnitySpaceApp& app)
		{
			auto& s = S();
			const InputDevice& kb = InputDevice::current();
			if (!kb.present) return;
			auto pressed = [&](Key k) { return kb[k].wasPressedThisFrame; };
			int n = static_cast<int>(s.stops.size());
			int chapter = s.stops[s.index].first;
			if (pressed(Key::Escape))
			{
				Finish(&app);
				return;
			}
			if (pressed(Key::RightArrow) || pressed(Key::N))
			{
				Advance(app);
				return;
			}
			if (pressed(Key::LeftArrow) || pressed(Key::B))
			{
				GoTo(std::max(0, s.index - 1));
				return;
			}
			if (pressed(Key::S))
			{
				for (int c = chapter + 1; c < ChapterCount(); c++)
				{
					int at = FirstOf(c);
					if (at >= 0)
					{
						GoTo(at);
						return;
					}
				}
				Finish(&app);
				return;
			}
			if (pressed(Key::Space)) s.paused = !s.paused;
			if (pressed(Key::O))
			{
				s.otherScale = !s.otherScale;
				app.ShowToast(s.otherScale ? "Tour: every stop again at the other scale" : "Tour: each stop at its own scale only");
			}
			static const Key digits[] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9 };
			for (int d = 0; d < 9; d++)
				if (pressed(digits[d]))
				{
					int at = FirstOf(d);
					if (at >= 0 && at < n) GoTo(at);
					return;
				}
		}
	}

	void ShowcaseTour::Start(UnitySpaceApp& app, int first, bool autoShots, const cs::String& shotsDir, bool quit)
	{
		auto& s = S();
		s = State();
		s.stops = TourRegistry::Flat();
		if (s.stops.empty()) return;
		s.running = true;
		s.home = app.ActiveSystem;
		s.autoShots = autoShots;
		s.quit = quit;
		s.startedAt = FPlatformTime::Seconds();
		if (autoShots)
		{
			s.shotsDir = FPaths::ConvertRelativePathToFull(F(shotsDir.empty() ? cs::String(TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tour")))) : shotsDir));
			IFileManager::Get().MakeDirectory(*s.shotsDir, true);
		}
		GoTo(std::max(0, std::min(first, static_cast<int>(s.stops.size()) - 1)));
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Tour: %d stops in %d chapters%s"), static_cast<int>(s.stops.size()), ChapterCount(),
			autoShots ? *FString::Printf(TEXT(", shots into %s"), *s.shotsDir) : TEXT(""));
	}

	int ShowcaseTour::ChapterStart(const cs::String& chapter)
	{
		const auto& chapters = TourRegistry::Chapters();
		auto flat = TourRegistry::Flat();
		for (int c = 0; c < static_cast<int>(chapters.size()); c++)
			if (Lower(chapters[c].Name).rfind(Lower(chapter), 0) == 0)
				for (int i = 0; i < static_cast<int>(flat.size()); i++)
					if (flat[i].first == c) return i;
		return -1;
	}

	void ShowcaseTour::Stop(UnitySpaceApp& app) { Finish(&app); }

	void ShowcaseTour::Abandon() { Finish(nullptr); }

	bool ShowcaseTour::Running() { return S().running; }

	bool ShowcaseTour::Update(UnitySpaceApp& app, bool keysFree)
	{
		auto& s = S();
		if (!s.running) return false;
		const TourStop* stop = Current();
		if (stop == nullptr)
		{
			Finish(&app);
			return false;
		}
		if (keysFree && !s.autoShots)
		{
			int before = s.index;
			Phase phaseBefore = s.phase;
			Keys(app);
			if (!s.running) return true;
			if (s.index != before || s.phase != phaseBefore) return true;
		}
		float dt = Mathf::Min(Time::unscaledDeltaTime, 0.1f);
		double t = s.phaseT;
		s.phaseT += dt;
		Result* result = s.autoShots ? &ResultFor(*stop) : nullptr;

		switch (s.phase)
		{
			case Phase::Prepare:
			{
				if (!Prepare(app, *stop, t))
				{
					if (t > 150)
					{
						s.skipped = cs::Cat("never got there (", s.waiting, ")");
						Enter(Phase::Show);
					}
					break;
				}
				if (!stop->Pending.empty())
				{
					if (result != nullptr)
					{
						result->Status = "pending";
						result->Note = stop->Pending;
						Advance(app);
						break;
					}
					Enter(Phase::Show);
					break;
				}
				if (!s.setupRan)
				{
					s.setupRan = true;
					try
					{
						s.skipped = stop->Setup ? stop->Setup(app) : cs::String();
					}
					catch (const std::exception& e)
					{
						s.skipped = e.what();
					}
					if (!s.skipped.empty()) UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: %s: %s"), *F(stop->Id), *F(s.skipped));
				}
				Enter(s.skipped.empty() ? Phase::Ready : Phase::Show);
				break;
			}
			case Phase::Ready:
			{
				bool ready = true;
				try
				{
					ready = !stop->Ready || stop->Ready(app, t);
				}
				catch (const std::exception& e)
				{
					s.skipped = e.what();
					Enter(Phase::Show);
					break;
				}
				if (!ready && t < stop->ReadyTimeout)
				{
					s.waiting = "setting up";
					break;
				}
				if (!ready)
				{
					s.late = true;
					UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: %s: its setup never said it had landed (%.0f s); shown as it is"), *F(stop->Id), stop->ReadyTimeout);
				}
				s.waiting.clear();
				s.showT = 0;
				Enter(Phase::Show);
				break;
			}
			case Phase::Show:
			{
				if (!s.skipped.empty() || !stop->Pending.empty())
				{
					// Nothing to show here: the card says why, a moment, then on (at once in an auto run).
					if (result != nullptr)
					{
						result->Status = "skipped";
						result->Note = s.skipped;
						Advance(app);
						break;
					}
					if (!s.paused) s.showT += dt;
					if (s.showT >= 4) Advance(app);
					break;
				}
				if (stop->Camera)
				{
					try { stop->Camera(app, s.showT); }
					catch (const std::exception& e) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: %s camera: %s"), *F(stop->Id), UTF8_TO_TCHAR(e.what())); }
				}
				if (!s.paused) s.showT += dt;
				if (s.autoShots && s.showT >= std::max(1.0, stop->Seconds - 0.4))
				{
					Shoot(*stop);
					Enter(Phase::Shot);
					break;
				}
				if (!s.autoShots && s.showT >= stop->Seconds)
				{
					if (!s.onOther && s.otherScale)
					{
						s.onOther = true;
						app.ToggleScale();
						Enter(Phase::OtherScale);
					}
					else Advance(app);
				}
				break;
			}
			case Phase::Shot:
			{
				// The camera keeps doing its work while the shot is written at the end of the frame.
				if (stop->Camera) stop->Camera(app, s.showT);
				bool written = IFileManager::Get().FileExists(*s.pendingShot);
				if (!written && t < 5) break;
				if (result != nullptr)
				{
					if (written) result->Files.push_back(s.pendingShot);
					if (result->Status.empty() || result->Status == "ok") result->Status = s.late ? "late" : written ? "ok" : "no-shot";
				}
				if (!s.onOther && stop->BothScales)
				{
					s.onOther = true;
					app.ToggleScale();
					Enter(Phase::OtherScale);
				}
				else Advance(app);
				break;
			}
			case Phase::OtherScale:
			{
				// The other scale: once it has settled, the stop again from its camera's start.
				if (stop->Camera) stop->Camera(app, 0);
				if (app.ScaleAnimating() || t < 1.5) break;
				s.showT = 0;
				Enter(Phase::Show);
				break;
			}
			default: break;
		}
		return s.running;
	}

	// ---------------------------------------------------------------- the strip

	namespace
	{
		struct Styles
		{
			bool ready = false;
			cs::Ref<Texture2D> panelTex, barTex, cardTex;
			cs::Ref<GUIStyle> panel, chapter, title, caption, keys, card, cardTitle;
		};

		Styles& St()
		{
			static Styles s;
			return s;
		}

		void EnsureStyles()
		{
			auto& st = St();
			if (st.ready) return;
			st.ready = true;
			st.panelTex = UI::Hud::Solid(Color(0.03f, 0.05f, 0.08f, 0.86f), Color(0.45f, 0.85f, 1.0f, 0.6f));
			st.barTex = UI::Hud::Solid(Color(0.45f, 0.85f, 1.0f, 0.9f), Color::clear());
			st.cardTex = UI::Hud::Solid(Color(0.03f, 0.05f, 0.08f, 0.94f), Color(1.0f, 0.75f, 0.35f, 0.8f));
			st.panel = cs::New<GUIStyle>();
			st.panel->normal.background = st.panelTex;
			st.panel->border = RectOffset(2, 2, 2, 2);
			st.chapter = cs::New<GUIStyle>();
			st.chapter->fontSize = 11;
			st.chapter->fontStyle = FontStyle::Bold;
			st.chapter->normal.textColor = UI::Hud::Accent;
			st.chapter->clipping = TextClipping::Clip;
			st.title = cs::New<GUIStyle>();
			st.title->fontSize = 16;
			st.title->fontStyle = FontStyle::Bold;
			st.title->normal.textColor = Color::white();
			st.title->clipping = TextClipping::Clip;
			st.caption = cs::New<GUIStyle>();
			st.caption->fontSize = 13;
			st.caption->normal.textColor = Color::white();
			st.caption->wordWrap = true;
			st.caption->clipping = TextClipping::Clip;
			st.keys = cs::New<GUIStyle>();
			st.keys->fontSize = 11;
			st.keys->normal.textColor = UI::Hud::Dim;
			st.keys->clipping = TextClipping::Clip;
			st.card = cs::New<GUIStyle>();
			st.card->normal.background = st.cardTex;
			st.card->border = RectOffset(2, 2, 2, 2);
			st.cardTitle = cs::New<GUIStyle>(*st.title);
			st.cardTitle->alignment = TextAnchor::MiddleCenter;
			st.cardTitle->fontSize = 20;
		}
	}

	void ShowcaseTour::Draw(UnitySpaceApp& app)
	{
		(void)app;
		auto& s = S();
		if (!s.running) return;
		const TourStop* stop = Current();
		if (stop == nullptr) return;
		EnsureStyles();
		auto& st = St();
		const auto& chapters = TourRegistry::Chapters();
		int chapter = s.stops[s.index].first;

		float w = std::min(static_cast<float>(Screen::width) - 32, 980.0f);
		float h = 92;
		float x = (Screen::width - w) * 0.5f;
		float y = Screen::height - h - 40;
		GUI::Box(Rect(x, y, w, h), GUIContent::none(), st.panel);
		// How far through the stop it is, along the top edge.
		if (s.phase == Phase::Show && stop->Seconds > 0)
			GUI::DrawTexture(Rect(x + 2, y + 2, (w - 4) * Mathf::Clamp01(static_cast<float>(s.showT / stop->Seconds)), 3), st.barTex);

		cs::String where = cs::Cat("SHOWCASE TOUR  ·  CHAPTER ", chapter + 1, " OF ", chapters.size(), ": ", cs::Strings::ToUpperInvariant(chapters[chapter].Name),
			"  ·  STOP ", s.index + 1, " OF ", s.stops.size());
		if (s.onOther) where = cs::Cat(where, "  ·  AT THE OTHER SCALE");
		if (s.paused) where = cs::Cat(where, "  ·  PAUSED");
		if (s.autoShots) where = cs::Cat(where, "  ·  AUTO");
		GUI::Label(Rect(x + 16, y + 9, w - 32, 16), where, st.chapter);
		GUI::Label(Rect(x + 16, y + 25, w - 32, 22), stop->Title, st.title);
		cs::String caption = stop->Caption;
		if (!s.skipped.empty()) caption = cs::Cat("Not shown here: ", s.skipped);
		else if (!stop->Pending.empty()) caption = cs::Cat("Coming with ", stop->Pending, ": ", stop->Caption);
		else if (!s.waiting.empty()) caption = cs::Cat(stop->Caption, "   (", s.waiting, "...)");
		GUI::Label(Rect(x + 16, y + 48, w - 32, 22), caption, st.caption);
		GUI::Label(Rect(x + 16, y + h - 20, w - 32, 16),
			cs::Cat("Right / N next   ·   Left / B previous   ·   S skip chapter   ·   Space ", s.paused ? "resume" : "pause",
				"   ·   1-", std::min(9, static_cast<int>(chapters.size())), " chapter   ·   O other scale: ", s.otherScale ? "ON" : "OFF", "   ·   Esc end"),
			st.keys);

		// A stop still to come (or one that cannot be shown here) is a card in the middle of the screen.
		if (!stop->Pending.empty() || !s.skipped.empty())
		{
			auto r = Rect((Screen::width - 560) * 0.5f, Screen::height * 0.32f, 560, 120);
			GUI::Box(r, GUIContent::none(), st.card);
			GUI::Label(Rect(r.x, r.y + 24, r.width, 30), !stop->Pending.empty() ? cs::Cat("COMING SOON  ·  ", stop->Pending) : cs::String("NOT HERE"), st.cardTitle);
			GUI::Label(Rect(r.x + 24, r.y + 62, r.width - 48, 50), !stop->Pending.empty() ? stop->Title : s.skipped, st.caption);
		}
	}

	void ShowcaseTour::DestroyOwnedTextures()
	{
		auto& st = St();
		st = Styles();
	}

	// ---------------------------------------------------------------- the contact sheet

	namespace
	{
		/// Every shot of the auto run in a grid (a thumbnail each, a coloured bar under it: green shown, amber late, red
		/// skipped or not shot, blue still to come), as contact-sheet.png; the same with titles, captions and notes as
		/// contact-sheet.html; and tour-report.txt, a line a stop.
		void WriteContactSheet()
		{
			auto& s = S();
			IImageWrapperModule& images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			constexpr int Cols = 6, ThumbW = 320, ThumbH = 180, Bar = 8, Gap = 6;
			int tiles = 0;
			for (const auto& r : s.results) tiles += std::max<int>(1, static_cast<int>(r.Files.size()));
			int rows = (tiles + Cols - 1) / Cols;
			int sheetW = Cols * (ThumbW + Gap) + Gap, sheetH = rows * (ThumbH + Bar + Gap) + Gap;
			TArray<uint8> sheet;
			sheet.SetNumZeroed(sheetW * sheetH * 4);
			for (int i = 0; i < sheetW * sheetH; i++)
			{
				sheet[i * 4 + 0] = 24;
				sheet[i * 4 + 1] = 16;
				sheet[i * 4 + 2] = 12;
				sheet[i * 4 + 3] = 255;
			}
			auto fill = [&](int x0, int y0, int w, int h, uint8 b, uint8 g, uint8 r)
			{
				for (int y = y0; y < y0 + h && y < sheetH; y++)
					for (int x = x0; x < x0 + w && x < sheetW; x++)
					{
						uint8* p = &sheet[(y * sheetW + x) * 4];
						p[0] = b;
						p[1] = g;
						p[2] = r;
						p[3] = 255;
					}
			};
			int tile = 0;
			FString html = TEXT("<!doctype html><meta charset=utf-8><title>SpaceGod showcase tour</title><style>body{background:#0b1016;color:#dfe8ef;font:13px system-ui;margin:16px}"
				".g{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px}.t{background:#121a22;padding:8px;border-left:4px solid #4c8}"
				".late{border-color:#e9a33a}.skipped,.no-shot{border-color:#e55}.pending{border-color:#59f}img{width:100%}h2{font-size:15px;margin:4px 0}p{margin:2px 0;color:#9ab}</style>"
				"<h1>SpaceGod showcase tour</h1><div class=g>");
			FString report;
			for (const auto& r : s.results)
			{
				uint8 cb = 80, cg = 200, cr = 80;
				if (r.Status == "late") { cb = 40; cg = 165; cr = 235; }
				else if (r.Status == "skipped" || r.Status == "no-shot") { cb = 60; cg = 60; cr = 230; }
				else if (r.Status == "pending") { cb = 240; cg = 150; cr = 80; }
				report += FString::Printf(TEXT("%-8s %-28s %s%s%s\n"), *F(r.Status), *F(r.Id), *F(r.Title), r.Note.empty() ? TEXT("") : TEXT("  --  "), *F(r.Note));
				html += FString::Printf(TEXT("<div class='t %s'><h2>%s</h2><p>%s &middot; %s</p><p>%s</p>"), *F(r.Status), *F(r.Title), *F(r.Chapter), *F(r.Status), *F(r.Caption));
				if (!r.Note.empty()) html += FString::Printf(TEXT("<p><b>%s</b></p>"), *F(r.Note));
				std::vector<FString> files = r.Files;
				if (files.empty()) files.push_back(FString());
				for (const auto& file : files)
				{
					int tx = Gap + (tile % Cols) * (ThumbW + Gap), ty = Gap + (tile / Cols) * (ThumbH + Bar + Gap);
					tile++;
					fill(tx, ty + ThumbH, ThumbW, Bar, cb, cg, cr);
					if (file.IsEmpty()) continue;
					html += FString::Printf(TEXT("<img src='%s'>"), *FPaths::GetCleanFilename(file));
					TArray<uint8> png;
					if (!FFileHelper::LoadFileToArray(png, *file)) continue;
					TSharedPtr<IImageWrapper> in = images.CreateImageWrapper(EImageFormat::PNG);
					TArray64<uint8> raw;
					if (!in.IsValid() || !in->SetCompressed(png.GetData(), png.Num()) || !in->GetRaw(ERGBFormat::BGRA, 8, raw)) continue;
					int w = static_cast<int>(in->GetWidth()), h = static_cast<int>(in->GetHeight());
					if (w <= 0 || h <= 0) continue;
					// A box filter down to the thumbnail, letterboxed to keep the aspect.
					float scale = std::min(ThumbW / static_cast<float>(w), ThumbH / static_cast<float>(h));
					int dw = std::max(1, static_cast<int>(w * scale)), dh = std::max(1, static_cast<int>(h * scale));
					int ox = tx + (ThumbW - dw) / 2, oy = ty + (ThumbH - dh) / 2;
					for (int y = 0; y < dh; y++)
						for (int x = 0; x < dw; x++)
						{
							int sx0 = x * w / dw, sx1 = std::max(sx0 + 1, (x + 1) * w / dw);
							int sy0 = y * h / dh, sy1 = std::max(sy0 + 1, (y + 1) * h / dh);
							uint32 acc[3] = { 0, 0, 0 };
							int n = 0;
							for (int yy = sy0; yy < sy1; yy += 2)
								for (int xx = sx0; xx < sx1; xx += 2)
								{
									const uint8* p = &raw[(yy * w + xx) * 4];
									acc[0] += p[0];
									acc[1] += p[1];
									acc[2] += p[2];
									n++;
								}
							uint8* d = &sheet[((oy + y) * sheetW + (ox + x)) * 4];
							d[0] = static_cast<uint8>(acc[0] / n);
							d[1] = static_cast<uint8>(acc[1] / n);
							d[2] = static_cast<uint8>(acc[2] / n);
							d[3] = 255;
						}
				}
				html += TEXT("</div>");
			}
			html += TEXT("</div>");
			TSharedPtr<IImageWrapper> out = images.CreateImageWrapper(EImageFormat::PNG);
			FString sheetPath = FPaths::Combine(s.shotsDir, TEXT("contact-sheet.png"));
			if (out.IsValid() && out->SetRaw(sheet.GetData(), sheet.Num(), sheetW, sheetH, ERGBFormat::BGRA, 8))
			{
				const TArray64<uint8>& png = out->GetCompressed();
				FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(png.GetData(), png.Num()), *sheetPath);
			}
			FFileHelper::SaveStringToFile(html, *FPaths::Combine(s.shotsDir, TEXT("contact-sheet.html")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FFileHelper::SaveStringToFile(report, *FPaths::Combine(s.shotsDir, TEXT("tour-report.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Tour: contact sheet %s\n%s"), *sheetPath, *report);
		}
	}

	// ---------------------------------------------------------------- sg.Tour and the dev menu

	namespace
	{
		/// sg.Tour [chapter|stop number] | sg.Tour auto [shots <dir>] [quit] | sg.Tour stop. From the main menu (or -ExecCmds) it
		/// starts a new game first and waits for it, as the feature commands do.
		void StartCommand(const TArray<FString>& args)
		{
			bool autoShots = false, quit = false, stop = false;
			FString shots, from;
			for (int i = 0; i < args.Num(); i++)
			{
				const FString& a = args[i];
				if (a.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) autoShots = true;
				else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) { shots = args[++i]; autoShots = true; }
				else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
				else if (a.Equals(TEXT("stop"), ESearchCase::IgnoreCase) || a.Equals(TEXT("end"), ESearchCase::IgnoreCase)) stop = true;
				else from = from.IsEmpty() ? a : from + TEXT(" ") + a;
			}
			if (stop)
			{
				if (auto* app = RunningApp()) ShowcaseTour::Stop(*app);
				return;
			}
			struct Wait { double started = FPlatformTime::Seconds(); double appAt = -1; bool askedNewGame = false; };
			auto w = std::make_shared<Wait>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([w, autoShots, quit, shots, from](float)
			{
				double now = FPlatformTime::Seconds();
				auto* app = RunningApp();
				if (app == nullptr)
				{
					if (!w->askedNewGame && now - w->started > 2.0)
					{
						w->askedNewGame = true;
						UE_LOG(LogSpaceGod, Display, TEXT("sg.Tour: no game running; starting a new one"));
						GameLaunch::NewGame();
					}
					if (now - w->started > 180.0)
					{
						UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: no game came up"));
						if (quit) FPlatformMisc::RequestExit(false);
						return false;
					}
					return true;
				}
				// A few seconds for a scene only just up to lay itself out.
				if (w->appAt < 0) w->appAt = now;
				if (now - w->appAt < (w->askedNewGame || autoShots ? 6.0 : 0.0)) return true;
				int first = 0;
				if (!from.IsEmpty())
				{
					first = from.IsNumeric() ? FCString::Atoi(*from) - 1 : ShowcaseTour::ChapterStart(TCHAR_TO_UTF8(*from));
					if (first < 0)
					{
						UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: no chapter '%s'"), *from);
						first = 0;
					}
				}
				ShowcaseTour::Start(*app, first, autoShots, TCHAR_TO_UTF8(*shots), quit);
				return false;
			}));
		}

		FAutoConsoleCommand GTourCommand(
			TEXT("sg.Tour"),
			TEXT("US-T147: the showcase tour through every feature: sg.Tour [chapter|stop number]; sg.Tour stop; "
				"sg.Tour auto [shots <dir>] [quit] plays every stop unattended, screenshots each and writes a contact sheet."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { StartCommand(args); }));

		void RegisterTourCheats()
		{
			using UI::CheatEntry;
			using UI::CheatRegistry;
			CheatRegistry::DeclareCategory("Showcase", 5, "the guided tour through every feature, chapter by chapter (US-T147)");
			CheatRegistry::Add("Showcase", CheatEntry::Action("Start the showcase tour", "every feature in turn: Right next, Left back, S skip, Esc end", [](UnitySpaceApp& a) { ShowcaseTour::Start(a); })
				.WithKeys("sg.Tour"));
			CheatRegistry::Add("Showcase", CheatEntry::Menu("Start at a chapter", "jump straight to one part of the tour", [](UnitySpaceApp&)
			{
				std::vector<CheatEntry> out;
				const auto& chapters = TourRegistry::Chapters();
				for (int c = 0; c < static_cast<int>(chapters.size()); c++)
				{
					cs::String name = chapters[c].Name;
					out.push_back(CheatEntry::Action(name, cs::Cat(chapters[c].Stops.size(), " stops  ·  ", chapters[c].Blurb), [name](UnitySpaceApp& a)
					{
						ShowcaseTour::Start(a, std::max(0, ShowcaseTour::ChapterStart(name)));
					}));
				}
				return out;
			}));
			CheatRegistry::Add("Showcase", CheatEntry::Action("End the tour", "leave everything as the stop left it", [](UnitySpaceApp& a) { ShowcaseTour::Stop(a); })
				.WithKeys("Esc").WithBlocker([](UnitySpaceApp&) { return ShowcaseTour::Running() ? cs::String() : cs::String("no tour running"); }));
		}

		UI::CheatRegistration GTourCheats(&RegisterTourCheats);
	}
}
