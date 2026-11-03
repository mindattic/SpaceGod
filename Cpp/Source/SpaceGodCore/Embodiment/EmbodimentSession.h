#pragma once
// Port of Unity.SpaceGod Core/Embodiment/EmbodimentSession.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// The one thing the player is embodying, if any (U.SPC-T18). Switching seats is always a hand-back
	/// followed by a take-charge, and a thing that can no longer be embodied (shot away, captured) is handed
	/// back by itself. Works only through <see cref="IEmbodiable"/>: a new kind of controllable thing plugs in
	/// without touching this.
	/// </summary>
	class SPACEGODCORE_API EmbodimentSession final
	{
	public:
		/// <summary>What the player holds now, or null when only watching.</summary>
		cs::Ref<IEmbodiable> Current;

		bool Active() const { return Current != nullptr; }

		/// <summary>Why the session last ended by itself (the thing held was shot down, the fight ended: its
		/// <see cref="IEmbodiable.Blocker"/>), or null if the player let go or switched (U.SPC-T422).</summary>
		/// (Empty stands for C#'s null.)
		cs::String EndedBecause;

		/// <summary>
		/// Hands back whatever is held and takes <paramref name="next"/>. Returns false (holding nothing new,
		/// and with the old thing still handed back) if <paramref name="next"/> cannot be embodied right now.
		/// </summary>
		bool Switch(cs::Ref<IEmbodiable> next); // by value: next may be Current itself, which Release clears

		/// <summary>Hands what is held back to its AI.</summary>
		void Release();

		/// <summary>
		/// Moves up from a mounted thing to what it is mounted on (a turret to its hull): true if there was
		/// a host to take.
		/// </summary>
		bool MoveToHost();

		/// <summary>
		/// One frame: the player's input goes to what is held, unless it can no longer be embodied, when it is
		/// handed back and the session ends. Returns false once nothing is held.
		/// </summary>
		bool Step(const EmbodimentInput& input, double dt);
	};
}
