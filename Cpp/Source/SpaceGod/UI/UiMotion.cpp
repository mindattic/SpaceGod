#include "UI/UiMotion.h"

#include <string>
#include <unordered_map>
#include <utility>

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "ActiveProfile.h"
#include "MindAtticHeadless.h"
#include "PlayerOptions.h" // US-T252
#include "Shared/Profiles/ProfileStore.h"
#include "UnityCompat/GuiHost.h"

namespace SpaceGod::UI::UiMotion
{
	namespace
	{
		/// A run that is driven and photographed (the smoke, UI shots, views, fidelity shots, audits, co-op smoke, the
		/// race; a capture or the demo tour; a headless run) never moves, so every shot is of a settled frame.
		bool ScriptedRun()
		{
			if (FMindAtticHeadless::IsActive()) return true;
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false))
				if (token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase)
					|| token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
					return true;
			return false;
		}

		bool ReadReduceMotion()
		{
			try
			{
				auto profile = ActiveProfile::Current();
				auto settings = profile != nullptr ? profile->Settings() : nullptr;
				// US-T252: or Settings > Accessibility > Reduce flashes and shake.
				return PlayerOptions::Calm() || (settings != nullptr && settings->Common != nullptr && settings->Common->ReduceMotion());
			}
			catch (...)
			{
				return false;
			}
		}

		struct PanelClock
		{
			double start = 0;
			uint64 lastFrame = 0;
			bool seen = false;
		};
		std::unordered_map<std::string, PanelClock>& Panels()
		{
			static std::unordered_map<std::string, PanelClock> panels;
			return panels;
		}

		// ---------------------------------------------------------------- the scene fade

		enum class Phase
		{
			None,
			/// Fading to black; at black the load runs.
			Out,
			/// Black until the next scene arrives (or a while passes with none).
			Black,
			/// Fading back in (its clock starts the first frame it is drawn, after the load's hitch).
			In,
		};
		Phase phase = Phase::None;
		double phaseStart = 0;
		bool inClockStarted = false;
		int blackFrames = 0;
		std::function<void()> pendingLoad;
		bool overlayInstalled = false;
		int overlayOwner = 0;

		/// Black frames drawn with no scene arriving before the fade gives up and comes back in (a load that failed or
		/// never travelled must not leave the screen dark).
		constexpr int MaxBlackFrames = 240;

		void RunPending()
		{
			auto load = std::move(pendingLoad);
			pendingLoad = nullptr;
			if (load) load();
		}

		void DrawOverlay()
		{
			bool enabled = Enabled();
			GUI::hoverSeconds = enabled ? HoverSeconds : 0.0f;
			if (phase == Phase::None) return;
			if (!enabled)
			{
				// Reduced motion turned on mid-fade: the load goes now, and nothing is drawn.
				phase = Phase::None;
				RunPending();
				return;
			}
			bool repaint = Event::current().type == EventType::Repaint;
			double now = Now();
			float alpha = 0;
			switch (phase)
			{
				case Phase::Out:
				{
					float t = static_cast<float>((now - phaseStart) / SceneSeconds);
					alpha = Mathf::Clamp01(t);
					if (t >= 1 && repaint)
					{
						phase = Phase::Black;
						blackFrames = 0;
						RunPending();
					}
					break;
				}
				case Phase::Black:
					alpha = 1;
					if (repaint && ++blackFrames > MaxBlackFrames)
					{
						phase = Phase::In;
						inClockStarted = false;
					}
					break;
				case Phase::In:
				{
					if (!inClockStarted)
					{
						inClockStarted = true;
						phaseStart = now;
					}
					float t = static_cast<float>((now - phaseStart) / SceneSeconds);
					alpha = 1 - EaseOut(t);
					if (t >= 1) phase = Phase::None;
					break;
				}
				default:
					break;
			}
			if (!repaint || alpha <= 0) return;
			auto old = GUI::color;
			GUI::color = Color(0, 0, 0, alpha);
			GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), Texture2D::whiteTexture());
			GUI::color = old;
		}

		void EnsureOverlay()
		{
			if (overlayInstalled || ScriptedRun()) return;
			overlayInstalled = true;
			// Last of every handler, so the fade covers the whole UI (the app's, the menu's, the model viewer's).
			GuiHost::Register(&overlayOwner, [] { DrawOverlay(); }, 1000000);
		}
	}

	bool Enabled()
	{
		static int scripted = -1;
		if (scripted < 0) scripted = ScriptedRun() ? 1 : 0;
		if (scripted == 1) return false;
		// The profile's setting, read once a frame.
		static uint64 readFrame = ~0ull;
		static bool reduce = false;
		if (readFrame != GFrameCounter)
		{
			readFrame = GFrameCounter;
			reduce = ReadReduceMotion();
		}
		return !reduce;
	}

	double Now() { return FPlatformTime::Seconds(); }

	float EaseOut(float t)
	{
		t = Mathf::Clamp01(t);
		float u = 1 - t;
		return 1 - u * u * u;
	}

	float Appear(const char* id, float seconds)
	{
		if (!Enabled() || id == nullptr) return 1;
		uint64 frame = GFrameCounter;
		double now = Now();
		PanelClock& c = Panels()[id];
		// Not drawn last frame or the one before: it has just (re)appeared.
		if (!c.seen || frame > c.lastFrame + 2) c.start = now;
		c.seen = true;
		c.lastFrame = frame;
		return EaseOut(static_cast<float>((now - c.start) / Mathf::Max(0.001f, seconds)));
	}

	float Enter(float ageSeconds, float seconds)
	{
		if (!Enabled()) return 1;
		return EaseOut(ageSeconds / Mathf::Max(0.001f, seconds));
	}

	Scope::Scope(const char* id, float slide) { Apply(Appear(id), slide); }

	Scope::Scope(float t, float slide) { Apply(t, slide); }

	void Scope::Apply(float t, float slide)
	{
		oldFade = GUI::fade;
		oldMatrix = GUI::matrix;
		if (t >= 1) return;
		t = Mathf::Clamp01(t);
		GUI::fade = oldFade * t;
		float dy = (1 - t) * slide;
		if (dy != 0) GUI::matrix = GuiMatrix::Translate(Vector2(0, dy)) * GUI::matrix;
	}

	Scope::~Scope()
	{
		GUI::fade = oldFade;
		GUI::matrix = oldMatrix;
	}

	void FadeOutThen(std::function<void()> load)
	{
		if (!load) return;
		if (!overlayInstalled || !Enabled())
		{
			load();
			return;
		}
		if (phase == Phase::Out || phase == Phase::Black) return; // one trip at a time
		phase = Phase::Out;
		phaseStart = Now();
		pendingLoad = std::move(load);
	}

	void FadeFromBlack()
	{
		if (!overlayInstalled || !Enabled()) return;
		if (phase == Phase::Out) return; // a trip is under way: its own fade comes back in
		phase = Phase::In;
		inClockStarted = false;
	}

	void SceneArrived()
	{
		EnsureOverlay();
		if (phase == Phase::Black)
		{
			phase = Phase::In;
			inClockStarted = false;
		}
	}
}
