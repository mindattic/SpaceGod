#pragma once

// US-T44: the helm's input stream in co-op, the C# track's CSharp/.../Runtime/Coop/CoopInput.cs (US-T43) in C++. The pilot
// loop reads one frame of the original's InputState; in co-op the frame it reads is the helm holder's, carried in the
// lockstep frames, so every peer flies the same hull on the same numbers. C# collects InputState's public bool / int /
// float / Vector2 / Vector3 fields by reflection, sorted by name (ordinal); C++ lists the same fields in the same order
// (CoopInput.cpp), so a frame's text is the C# track's.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"

#include <functional>
#include <vector>

namespace SpaceGod::Input { class InputState; }

namespace SpaceGod::Coop
{
	/// One field's value: which of bool / int / float / Vector2 / Vector3, and the numbers.
	struct PilotValue
	{
		bool B = false;
		int I = 0;
		float F[3] = { 0, 0, 0 };
		bool operator==(const PilotValue& o) const { return B == o.B && I == o.I && F[0] == o.F[0] && F[1] == o.F[1] && F[2] == o.F[2]; }
	};

	/// One frame of the helm holder's hands, as the pilot loop reads them: every carried field of InputState, plus what of
	/// the holder's own machine the loop reads besides the keys: its look sensitivity, whether a click panel had the
	/// mouse, what it had selected and hovered, and its camera's up frame.
	class PilotFrame
	{
	public:
		std::vector<PilotValue> Values;
		float Sensitivity = 0;
		bool PanelOpen = false;
		cs::String Selected = "n", Hovered = "n";
		Quaternion UpFrame = Quaternion::identity();

		/// A frame of nothing pressed (what the helm flies on until its holder's first message).
		static PilotFrame Idle();
		/// This machine's hands this frame (lookY: its invert-look, applied to the vertical delta).
		static PilotFrame Of(const Input::InputState& input, const std::function<float(float)>& lookY);
		/// Puts this frame into `input` as this machine's poll would have, its invert-look turned back.
		void ApplyTo(Input::InputState& input, const std::function<float(float)>& lookY) const;
		/// A copy of every carried field of `input` as it stands (to put back after the loop).
		static std::vector<PilotValue> Snapshot(const Input::InputState& input);
		static void Restore(Input::InputState& input, const std::vector<PilotValue>& snapshot);
		/// What a host frame flies on from the holder's messages since the last one (see CoopInput.cs).
		static PilotFrame Compose(const PilotFrame* last, const std::vector<PilotFrame>& since);
		/// The same frame with its edges, deltas and picks dropped.
		PilotFrame HeldOnly() const;
		bool AnyEdge() const;

		cs::String Write() const;
		static PilotFrame Read(const cs::String& text);
	};
}
