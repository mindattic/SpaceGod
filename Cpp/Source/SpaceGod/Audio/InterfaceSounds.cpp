#include "Audio/InterfaceSounds.h"
#include "Audio/GameAudio.h"
#include "Audio/SynthSound.h"
#include "PlayerOptions.h"
#include "UnitySpaceApp.h"
#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "UI/PauseMenu.h"
#include "UnityCompat/UnityGui.h"

#include "Comms/Comms.h"
#include "Economy/Contracts.h"
#include "Economy/Holdings.h"
#include "Law/Law.h"
#include "Ships.h"
#include "World.h"

#include "HAL/PlatformTime.h"

#include <bit>
#include <cmath>
#include <unordered_map>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	// Declared in UnitySpaceApp.Main.inl (US-T244).
	int UnitySpaceApp::OpenPanels() const
	{
		int bits = OpenScreens();
		auto add = [&bits](int bit, bool on) { if (on) bits |= 1 << bit; };
		add(14, ShowSystemMap);
		add(15, ShowLogs);
		add(16, ShowDiscoveries);
		add(17, ShowBoarding);
		add(18, commsOpen);
		add(19, hud != nullptr && hud->DrawerOpen());
		add(20, pauseMenu != nullptr && pauseMenu->IsOpen);
		add(21, cheatMenu != nullptr && cheatMenu->IsOpen);
		add(22, ShowEncyclopedia); // US-T271
		return bits;
	}
}

namespace SpaceGod::Audio::InterfaceSounds
{
	namespace
	{
		using namespace Synth;

		// ---------------------------------------------------------------- the sounds (synthesized, US-T240)

		const SoundClip& Hover()
		{
			static const SoundClip clip = [] { auto b = Buffer(0.02f); Tone(b, 0, 0.018f, 3200, 3000, 0.5f, Wave::Sine, 0.001f); return Finish(b, "UiHover", 0.3f); }();
			return clip;
		}

		const SoundClip& Toggle()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.07f);
				Tone(b, 0, 0.025f, 1500, 1500, 0.5f, Wave::Triangle, 0.0005f);
				Tone(b, 0.035f, 0.03f, 2000, 2000, 0.5f, Wave::Triangle, 0.0005f);
				return Finish(b, "UiToggle", 0.5f);
			}();
			return clip;
		}

		const SoundClip& PanelOpen()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.16f);
				Tone(b, 0, 0.15f, 320, 960, 0.5f, Wave::Triangle, 0.02f);
				Noise(b, 0, 0.12f, 0.15f, 0.35f, 0.03f, 11);
				return Finish(b, "PanelOpen", 0.45f);
			}();
			return clip;
		}

		const SoundClip& PanelClose()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.13f);
				Tone(b, 0, 0.12f, 900, 300, 0.5f, Wave::Triangle, 0.004f);
				Noise(b, 0, 0.08f, 0.12f, 0.3f, 0.004f, 12);
				return Finish(b, "PanelClose", 0.4f);
			}();
			return clip;
		}

		const SoundClip& Toast()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.35f);
				Tone(b, 0, 0.18f, 660, 660, 0.5f);
				Tone(b, 0.09f, 0.26f, 990, 990, 0.45f);
				return Finish(b, "Toast", 0.45f);
			}();
			return clip;
		}

		const SoundClip& News()
		{
			static const SoundClip clip = [] { auto b = Buffer(0.09f); Tone(b, 0, 0.08f, 1250, 1180, 0.5f, Wave::Sine, 0.003f); return Finish(b, "News", 0.3f); }();
			return clip;
		}

		const SoundClip& Hail()
		{
			static const SoundClip clip = []
			{
				// Three rising rings: someone is calling.
				auto b = Buffer(0.75f);
				for (int i = 0; i < 3; i++)
				{
					Tone(b, i * 0.22f, 0.16f, 1200.0f + i * 150.0f, 1250.0f + i * 150.0f, 0.5f, Wave::Square, 0.004f);
					Tone(b, i * 0.22f, 0.16f, 2400.0f + i * 300.0f, 2400.0f + i * 300.0f, 0.15f, Wave::Sine, 0.004f);
				}
				return Finish(b, "Hail", 0.5f);
			}();
			return clip;
		}

		const SoundClip& CommsLine()
		{
			static const SoundClip clip = []
			{
				// A burst of carrier noise and two data chirps: a line coming in over the link.
				auto b = Buffer(0.14f);
				Noise(b, 0, 0.05f, 0.25f, 0.8f, 0.001f, 21);
				Tone(b, 0.03f, 0.035f, 1800, 2500, 0.45f, Wave::Square, 0.001f);
				Tone(b, 0.08f, 0.04f, 2200, 1700, 0.4f, Wave::Square, 0.001f);
				return Finish(b, "CommsLine", 0.35f);
			}();
			return clip;
		}

		const SoundClip& ContractAccepted()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.3f);
				Tone(b, 0, 0.14f, 523.3f, 523.3f, 0.5f, Wave::Triangle);
				Tone(b, 0.1f, 0.2f, 659.3f, 659.3f, 0.5f, Wave::Triangle);
				return Finish(b, "ContractAccepted", 0.5f);
			}();
			return clip;
		}

		const SoundClip& ContractCompleted()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.7f);
				const float notes[] = { 523.3f, 659.3f, 784.0f, 1046.5f };
				for (int i = 0; i < 4; i++) Tone(b, i * 0.08f, i == 3 ? 0.4f : 0.16f, notes[i], notes[i], 0.5f, Wave::Triangle);
				Tone(b, 0.24f, 0.45f, 2093.0f, 2093.0f, 0.12f);
				return Finish(b, "ContractCompleted", 0.55f);
			}();
			return clip;
		}

		const SoundClip& ContractFailed()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.6f);
				Tone(b, 0, 0.2f, 440, 440, 0.5f, Wave::Saw, 0.006f);
				Tone(b, 0.16f, 0.2f, 349.2f, 349.2f, 0.5f, Wave::Saw, 0.006f);
				Tone(b, 0.32f, 0.28f, 293.7f, 277.2f, 0.5f, Wave::Saw, 0.006f);
				return Finish(b, "ContractFailed", 0.45f);
			}();
			return clip;
		}

		const SoundClip& CreditsGained()
		{
			static const SoundClip clip = []
			{
				// A coin: two bright rings, the second higher.
				auto b = Buffer(0.4f);
				Tone(b, 0, 0.12f, 1318.5f, 1318.5f, 0.45f);
				Tone(b, 0.06f, 0.32f, 1760.0f, 1760.0f, 0.5f);
				Tone(b, 0.06f, 0.2f, 3520.0f, 3520.0f, 0.1f);
				return Finish(b, "CreditsGained", 0.4f);
			}();
			return clip;
		}

		const SoundClip& CreditsSpent()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.25f);
				Tone(b, 0, 0.1f, 987.8f, 987.8f, 0.45f);
				Tone(b, 0.06f, 0.18f, 740.0f, 740.0f, 0.45f);
				return Finish(b, "CreditsSpent", 0.35f);
			}();
			return clip;
		}

		void Play(const SoundClip& clip, float volume = 1.0f) { GameAudio::Play2D(clip, AudioBus::Interface, volume); }

		// ---------------------------------------------------------------- the GUI's buttons and toggles

		double lastHoverAt = 0;

		void OnControl(UnityCompat::GUI::ControlSound sound)
		{
			if (!GameAudio::Enabled()) return;
			switch (sound)
			{
			case UnityCompat::GUI::ControlSound::Hover:
			{
				// Sweeping across a list of buttons ticks at most every 40 ms.
				const double now = FPlatformTime::Seconds();
				if (now - lastHoverAt < 0.04) return;
				lastHoverAt = now;
				Play(Hover(), 0.35f);
				break;
			}
			case UnityCompat::GUI::ControlSound::Toggle: Play(Toggle(), 0.7f); break;
			default: Play(StarterSounds::Click(), 0.8f); break;
			}
		}

		/// Installed before anything draws: the hook is a constant-initialized pointer, so this static's order is safe.
		const bool installed = (UnityCompat::GUI::controlSound = &OnControl, true);

		// ---------------------------------------------------------------- what Poll compares frame to frame

		struct Seen
		{
			bool primed = false;
			const UnitySpaceApp* app = nullptr;
			const Core::World* world = nullptr;
			int panels = 0;
			cs::String toast;
			float toastAge = 99.0f;
			int newsCount = 0;
			cs::String newsLast;
			const PatrolHail* hail = nullptr;
			int commsCount = 0;
			cs::String commsLast;
			std::unordered_map<const Contract*, ContractState> contracts;
			const Ship* purseShip = nullptr;
			const Faction* purseFaction = nullptr;
			double purse = 0;
			double creditsAt = 0;
		} seen;

		/// A save finished since the last Poll (its toast is the save's, not a plain toast).
		bool savedSincePoll = false;

		bool ShownInTicker(WorldEventKind k)
		{
			auto mode = PlayerOptions::NoticeMode();
			if (mode == PlayerOptions::NoticeLevel::Quiet) return false;
			return mode == PlayerOptions::NoticeLevel::Everything || k == WorldEventKind::Battle || k == WorldEventKind::Capture ||
				k == WorldEventKind::Diplomacy;
		}

		/// The player's purse: the treasury of the piloted ship's house (the player's own holdings), else none.
		bool Purse(UnitySpaceApp* app, const Ship*& ship, const Faction*& faction, double& credits)
		{
			const auto& world = app->World();
			ship = world->PilotedShip.get();
			faction = ship != nullptr ? ship->Faction.get() : nullptr;
			if (faction == nullptr || world->Holdings == nullptr || world->Holdings->House.get() != faction) return false;
			credits = faction->Treasury;
			return true;
		}
	}

	void SaveDone()
	{
		savedSincePoll = true;
		Play(StarterSounds::Chime(), 0.6f);
	}

	void Poll(UnitySpaceApp* app)
	{
		const bool saved = savedSincePoll;
		savedSincePoll = false;
		if (app == nullptr || app->Galaxy == nullptr || app->ActiveSystem == nullptr || !GameAudio::Enabled())
		{
			seen.primed = false;
			return;
		}
		const auto& world = app->World();
		const auto& events = world->Events;
		const auto& comms = app->CommsLog();
		const PatrolHail* hail = world->Law != nullptr ? world->Law->Hail.get() : nullptr;
		const int panels = app->OpenPanels();
		const Ship* purseShip = nullptr;
		const Faction* purseFaction = nullptr;
		double purse = 0;
		const bool hasPurse = Purse(app, purseShip, purseFaction, purse);

		std::unordered_map<const Contract*, ContractState> contracts;
		if (world->Contracts != nullptr)
			for (const auto& c : world->Contracts->All)
				if (c != nullptr && c->ByPlayer) contracts[c.get()] = c->State;

		// A new world (a load, a new game, a co-op sync, a system change) starts afresh: nothing heard from the jump.
		if (!seen.primed || seen.app != app || seen.world != world.get())
		{
			seen.primed = true;
			seen.app = app;
			seen.world = world.get();
			seen.panels = panels;
			seen.toast = app->Toast;
			seen.toastAge = app->ToastAge;
			seen.newsCount = events.Count();
			seen.newsLast = events.Count() > 0 ? events[events.Count() - 1].Text : cs::String();
			seen.hail = hail;
			seen.commsCount = comms.Count();
			seen.commsLast = comms.Count() > 0 ? comms[comms.Count() - 1].Text : cs::String();
			seen.contracts = std::move(contracts);
			seen.purseShip = purseShip;
			seen.purseFaction = purseFaction;
			seen.purse = purse;
			return;
		}

		// Panels: one more open (or a different one in its place) opens, fewer closes.
		if (panels != seen.panels)
		{
			const int was = std::popcount(static_cast<unsigned>(seen.panels)), now = std::popcount(static_cast<unsigned>(panels));
			if (now >= was) Play(PanelOpen(), 0.6f);
			else Play(PanelClose(), 0.55f);
			seen.panels = panels;
		}

		bool specific = saved; // a sound that says more than "a toast"

		// An incoming hail: a patrol calling (a cargo scan, an interdiction) or a beaten ship striking its colours.
		const bool newToast = app->Toast != seen.toast || app->ToastAge < seen.toastAge;
		const bool hailToast = newToast && app->Toast.rfind("INCOMING HAIL", 0) == 0;
		if ((hail != nullptr && hail != seen.hail) || hailToast)
		{
			Play(Hail(), 0.8f);
			specific = true;
		}
		seen.hail = hail;

		// The player's contracts: taken (new and Taken), completed, failed or abandoned.
		{
			int taken = 0, done = 0, failed = 0;
			for (const auto& [c, state] : contracts)
			{
				auto it = seen.contracts.find(c);
				const ContractState before = it != seen.contracts.end() ? it->second : ContractState::Open;
				if (state == before) continue;
				if (state == ContractState::Taken) taken++;
				else if (state == ContractState::Completed) done++;
				else if (state == ContractState::Failed || state == ContractState::Abandoned) failed++;
			}
			if (done > 0) Play(ContractCompleted(), 0.75f);
			else if (failed > 0) Play(ContractFailed(), 0.7f);
			else if (taken > 0) Play(ContractAccepted(), 0.7f);
			if (done + failed + taken > 0) specific = true;
			seen.contracts = std::move(contracts);
		}

		// Credits: the house purse up or down (a sale, a payout, a purchase), at most a few times a second.
		if (hasPurse && purseShip == seen.purseShip && purseFaction == seen.purseFaction)
		{
			const double delta = purse - seen.purse;
			const double nowS = FPlatformTime::Seconds();
			if (std::abs(delta) >= 1.0 && nowS - seen.creditsAt >= 0.35)
			{
				if (!specific) Play(delta > 0 ? CreditsGained() : CreditsSpent(), 0.55f);
				seen.creditsAt = nowS;
				seen.purse = purse;
				specific = true;
			}
		}
		else
		{
			seen.purse = purse;
		}
		seen.purseShip = purseShip;
		seen.purseFaction = purseFaction;

		// A comms line from the other party (not the player's own words, not the link's system lines).
		if (comms.Count() > 0 && (comms.Count() != seen.commsCount || comms[comms.Count() - 1].Text != seen.commsLast))
		{
			const auto& line = comms[comms.Count() - 1];
			if (!line.FromSpeaker && line.Who != "comms" && !specific)
			{
				Play(CommsLine(), 0.65f);
				specific = true;
			}
		}
		seen.commsCount = comms.Count();
		seen.commsLast = comms.Count() > 0 ? comms[comms.Count() - 1].Text : cs::String();

		// A toast (when nothing above already spoke for it).
		if (newToast && !app->Toast.empty() && !specific)
		{
			Play(Toast(), 0.6f);
			specific = true;
		}
		seen.toast = app->Toast;
		seen.toastAge = app->ToastAge;

		// A news report the ticker shows.
		if (events.Count() > 0 && (events.Count() != seen.newsCount || events[events.Count() - 1].Text != seen.newsLast))
		{
			if (!specific && ShownInTicker(events[events.Count() - 1].Kind)) Play(News(), 0.45f);
		}
		seen.newsCount = events.Count();
		seen.newsLast = events.Count() > 0 ? events[events.Count() - 1].Text : cs::String();
	}
}
