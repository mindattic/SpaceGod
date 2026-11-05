#pragma once

// US-T44: the headless two-instance co-op proof on the C++ track (tools/play.ps1 -Track Cpp -Mode coop-smoke), the C#
// track's CSharp/.../Runtime/Coop/CoopSmoke.cs (US-T39) step for step and line for line.
//
// -SpaceGodCoopSmoke=host|client: a listen server and a client on localhost (-nullrhi). The client joins (the host's save
// comes over and both load it), then plays a few things as a player would, by the keys and the picker: '.' (time warp
// up: a request the host grants for everyone), '3' (select Earth: every peer sees it), and a station's seat through the
// picker (a take command the host's frame grants, through SeatAuthority on every peer), and (US-T43) keeps it through a
// deliberate resync, flies a freighter's helm, trades at Earth, takes a fighter's cockpit in a fight and uses its keys,
// takes the party to another star and has the host load its quicksave. Both log a digest of the shared state every
// CoopPeer::DigestEvery frames; the host compares them and logs each match or desync. After ClientFrames replayed
// frames the client says bye and both quit.

#include "Cs/Cs.h"

namespace SpaceGod { class UnitySpaceApp; }
namespace SpaceGod::Smoke { struct ScriptedDevice; }

namespace SpaceGod::Coop
{
	class CoopPeer;
	struct FrameRecord;

	namespace CoopSmoke
	{
		constexpr int ClientFrames = 3000; // US-T43's longer run

		/// "host", "client", or empty (no smoke).
		const cs::String& Role();
		bool Active();

		/// The client's scripted keyboard and mouse (the link writes it into the input device before each frame), or null.
		Smoke::ScriptedDevice* Script();
		void UseScript();

		void HostFrame(CoopPeer& peer, UnitySpaceApp* app, const FrameRecord& rec);
		/// Each engine tick on the client, before its frames: the countdown to quitting and the timeout.
		void ClientTick(CoopPeer& peer);
		/// After each replayed host frame on the client: the scripted player.
		void ClientFrame(CoopPeer& peer, UnitySpaceApp& app);
	}
}
