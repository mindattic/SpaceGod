#pragma once

// CaptureEvents: the C++ port of SpaceGod.Unity.CSharp's Runtime/CaptureEvents.cs (US-T72, docs/CAPTURE.md there).
// The game's events for MindAttic.Capture (Capture/MindAtticCapture.h): each one is raised for the capture.json rules
// and written to the log as one `[capture] event name k=v ...` line (also to the session's .game.log, since a Shipping
// game writes no log). Only an observer: nothing here changes the game, and all of it is a no-op unless capture is
// running (only -capture on the command line starts it). Polled events compare state once a frame, after the frame
// (Tick, from the actor after the app's Update: Unity's LateUpdate).

#include "Cs/Cs.h"

#include <cstdint>
#include <initializer_list>

namespace SpaceGod
{
	class UnitySpaceApp;
	namespace Input { class InputState; }

	namespace CaptureEvents
	{
		/// One field of an event: its key and an integer or a string value (the catalogue has no other kind).
		struct Field
		{
			const char* Key;
			bool IsNumber;
			std::int64_t Number;
			cs::String Text;

			Field(const char* key, const cs::String& text) : Key(key), IsNumber(false), Number(0), Text(text) {}
			Field(const char* key, const char* text) : Key(key), IsNumber(false), Number(0), Text(text ? text : "") {}
			Field(const char* key, int value) : Key(key), IsNumber(true), Number(value) {}
			Field(const char* key, long long value) : Key(key), IsNumber(true), Number(value) {}
			Field(const char* key, unsigned long long value) : Key(key), IsNumber(true), Number(static_cast<std::int64_t>(value)) {}
		};

		/// Whether capture is running: false in a plain launch.
		bool Running();

		/// Raises <paramref name="name"/> with its fields in catalogue order.
		void Raise(const char* name, std::initializer_list<Field> fields);

		// ---------------------------------------------------------------- direct events

		void Trade(const cs::String& side, const cs::String& good, int crates, const cs::String& market);
		void Hail(const cs::String& from, const cs::String& to);
		void GateDeparture(const cs::String& ship, const cs::String& gate, const cs::String& system, const cs::String& to);
		void GameSaved(const cs::String& slot);
		void GameLoaded(const cs::String& slot);
		void CoopJoined(const cs::String& player, int seat);
		void CoopLeft(const cs::String& player, int seat);
		void CoopResync(long long sync, long long frame, const cs::String& reason);

		/// Alt+F9 / Alt+F10 / Alt+F11, read by InputState::Poll: run locally, never sent to co-op peers.
		void Hotkeys(const Input::InputState& input);

		/// Hooks the galaxy's own events (star discoveries, every world's log) and starts the polling afresh: called once
		/// the game is up and again after every rebuild (a load or a co-op sync).
		void Attach(UnitySpaceApp* host);

		/// C++ only: the app is going (the actor's EndPlay); the handlers left on its galaxy see no app and do nothing.
		void Detach(UnitySpaceApp* host);

		/// Compares this frame's state with the last one's (docs/CAPTURE.md section 3).
		void Tick(UnitySpaceApp* host);
	}
}
