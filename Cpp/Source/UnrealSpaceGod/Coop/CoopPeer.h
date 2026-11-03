#pragma once

// US-T44: cooperative multiplayer for the C++ track, the C# track's CSharp/.../Runtime/Coop/CoopPeer.cs (US-T39) in C++
// (docs/MIGRATION.md "Co-op"): the original's own Session layer (Core/Session) run in lockstep across a listen server
// and its clients, with the same messages, frame records, command lines, digests and resync flow as the C# track.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Coop/CoopWire.h"
#include "Coop/CoopInput.h"

#include <deque>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace SpaceGod { class UnitySpaceApp; }
namespace SpaceGod::Core { class LocalSession; struct PlayerId; }

namespace SpaceGod::Coop
{
	/// One end of a player's connection: the host's link to a client, or a client's link to the host.
	class ICoopLink
	{
	public:
		virtual ~ICoopLink() = default;
		/// Reliable and ordered (a reliable RPC on the player's link actor).
		virtual void Send(const cs::String& message) = 0;
		/// Best effort (an unreliable RPC): presence, which the next one replaces.
		virtual void SendUnreliable(const cs::String& message) = 0;
	};

	/// What every peer knows of a player: their name, what they last selected, where they are looking.
	struct CoopPlayer
	{
		int Seat = 0;
		cs::String Name;
		/// The selection's key (UnitySpaceApp::CoopTargetKey): "n" nothing, "b 3" body 3, "s 12" ship 12.
		cs::String Selection = "n";
		/// Where they are: a body of the active system and an offset from it in true km (-1: not known yet).
		int Body = -1;
		Core::DVec3 OffsetKm;
		bool Left = false;
	};

	/// The co-op session of this process: host-authoritative lockstep over the original's Session layer. See CoopPeer.cs
	/// for the design; everything here is that file's, member for member.
	class CoopPeer
	{
	public:
		static constexpr int MaxPlayers = 4;
		static constexpr int DigestEvery = 60;
		static constexpr int PresenceEvery = 10;
		static constexpr int ChunkWindow = 4;
		static constexpr int MaxResyncs = 3;
		/// Frames a client may fall behind (its pause menu open, a slow machine) before it asks to be resynced instead.
		static constexpr int MaxBacklog = 900;

		/// This process's co-op session, or null: single-player, the original's path unchanged.
		static CoopPeer* Current() { return current.get(); }
		static bool Active() { return current != nullptr; }

		/// A client goes straight into the game (the host's save replaces it), as does the smoke run's host.
		static bool SkipsMenu();

		bool IsHost() const { return isHost; }
		bool IsClient() const { return !isHost; }

		/// The seat this machine plays (0 the host; a client's once the host has welcomed it).
		int LocalSeat() const { return localSeat; }
		const cs::String& LocalName() const { return localName; }

		/// Every player by seat (null for a free seat).
		std::shared_ptr<CoopPlayer> Players[MaxPlayers];

		/// True while a peer applies a synced save itself (its own Activate and load hooks stay quiet).
		bool Applying() const { return applying; }

		/// The host reloads the save it sends (default), so every peer starts from the same loaded bytes.
		bool HostReloads = true;

		static void Log(const cs::String& message);
		static void Warn(const cs::String& message);

		static CoopPeer* StartHost(const cs::String& name);
		static CoopPeer* StartClient(ICoopLink* host, const cs::String& name);
		static void Stop();

		/// The name a player goes by: -CoopName=..., else the fallback, else the machine's user.
		static cs::String NameFromCommandLine(const cs::String& fallback);

		/// The session the app holds its seats through: the original's single-player LocalSession, or in co-op the same
		/// LocalSession with every seat of the game and this machine's player local.
		static cs::Ref<Core::LocalSession> NewSession();

		cs::String NameOf(int seat) const;

		// ---- commands, from either end

		/// A command from this machine's player into the stream: a host queues it for its next frame; a client sends it
		/// to the host, which stamps it into a frame every peer applies. Entries: "s <CommandCodec line>[ @seat key]".
		void Submit(const cs::String& entry);
		/// A change of the shared clock asked for at this machine ("warp 5", "pause 1").
		void RequestClock(const cs::String& request);
		// ---- US-T43: requests, the helm's hands, sync state

		/// The host's next frame carries `entry` (a shared setting changed here, an app entry).
		void QueueEntry(const cs::String& entry);
		/// A client asks the host to set a shared setting (CoopSettings).
		void RequestSetting(const cs::String& key, const cs::String& value);
		/// A client asks the host to take the party to another star.
		void RequestTravel(const cs::String& system);
		/// A client asks the host to load one of the host's save slots (every peer then loads the host's save).
		void RequestLoad(const cs::String& slot);
		/// A client asks for a resync now (the smoke run's deliberate one; a slow client's is "resync <frames>").
		void RequestResync(const cs::String& why);

		/// The engine tick a client is in (several host frames can replay in one): edges are sent once a tick.
		static inline long long EngineTick = 0;
		/// Whether a second Update in one engine tick sees the same edges again (the player controller: yes; the smoke
		/// run's scripted device clears them after every Update, so no).
		static inline bool EdgesPerTick = true;
		/// Whether the edges this Update read are new ones (false for a repeat within the tick).
		static bool FreshEdges(long long& lastTick);

		/// The helm holder (a client) sends its hands for the frame it just replayed.
		void SendPilot(PilotFrame frame);
		int PilotSent = 0;
		/// The host's frame for a client's helm: what came from it since the last frame (PilotFrame::Compose).
		PilotFrame HostPilotFor(int seat);
		/// Forgets a seat's hands (the helm changed hands): the next holder starts from idle.
		void ForgetPilot(int seat);

		/// The state a sync's save does not hold, sent with it (who flies the helm, each player's seat, the shared settings).
		cs::String PendingSyncState;
		/// The sync state that came with the client's current sync (haveClientSyncState false until it has).
		cs::String ClientSyncState;
		bool HaveClientSyncState = false;
		int Retakes = 0;

		/// A sync for a load (every view starts again, as a load does); else a resync, which leaves each view as it was.
		void RequestSync(const cs::String& reason, bool load);

		/// This machine's player selected something: everyone sees it (applied with the frames, as commands are).
		void Selected(const cs::String& key);
		/// Applies a frame's roster entries ("a", "j", "l"); session entries are the app's.
		void ApplyRosterEntry(const cs::String& entry);

		// ---- host

		long long Frame() const;
		int RemoteCount() const { return static_cast<int>(remotes.size()); }
		int DigestMatches = 0;
		int Desyncs = 0;

		/// The record of the frame the host's Update is running, once its clock has been reached (null outside).
		FrameRecord* Open() { return open.get(); }
		bool InFrame() const { return isHost ? open != nullptr : ClientRecord != nullptr; }

		/// At the app's clock step: opens this frame's record, with every command queued since the last one.
		FrameRecord* HostOpenFrame();
		/// The clients' clock requests since the last frame, for the host's clock (it is the authority over time).
		std::vector<std::pair<int, cs::String>> TakeClockRequests();
		/// After each Update on the host: the frame goes out, digests and presence, a pending sync, the save's chunks.
		void HostEndUpdate(UnitySpaceApp* app);
		/// A save for every client at the next frame boundary (a join, a load on the host, a desync).
		void RequestSync(const cs::String& reason);
		/// A client's link actor reached the host (its player controller logged in).
		void HostLogin(ICoopLink* link);
		/// A client logged out: its seat goes back to its AI on every peer, and it leaves the roster.
		void HostLogout(ICoopLink* link);
		/// A message from a client.
		void HostReceive(ICoopLink* link, const cs::String& msg);
		bool AnyBye() const;

		// ---- client

		long long FramesSinceSync = 0;
		/// The client has the host's world loaded and replays its frames.
		bool Running() const { return running; }
		bool Welcomed() const { return welcomed; }
		bool Refused() const { return refused; }
		/// The frame the client's Update is replaying (null outside one).
		const FrameRecord* ClientRecord = nullptr;
		/// Set by the app's clock step when it replayed ClientRecord (an Update that stopped at the pause menu did not,
		/// and the frame waits).
		bool Consumed = false;
		int Backlog() const { return static_cast<int>(queue.size()); }
		int ClockCorrections = 0;

		/// The app is up: asks the host for a seat.
		void ClientHello();
		const FrameRecord* NextFrame() const { return queue.empty() ? nullptr : &queue.front(); }
		/// The client finished replaying the front frame (and its post steps): digest, presence, the smoke run.
		void ClientFrameDone(UnitySpaceApp* app);
		void SayBye();
		/// A message from the host.
		void ClientReceive(const cs::String& msg);
		/// Once every chunk of a sync is in: the save is loaded, and the frames after it replay.
		bool ClientTryLoad(UnitySpaceApp* app);

	private:
		CoopPeer(bool host, const cs::String& name);

		static std::unique_ptr<CoopPeer> current;

		bool isHost = false;
		int localSeat = 0;
		cs::String localName;
		bool applying = false;

		// host
		enum class RemoteState { Joining, Welcomed, Syncing, Running };
		struct Remote
		{
			ICoopLink* Link = nullptr;
			int Seat = -1;
			RemoteState State = RemoteState::Joining;
			int SyncId = 0;
			int NextChunk = 0;
			int Acked = -1;
			int Resyncs = 0;
			bool Bye = false;
		};

		long long frameNo = 0;
		std::unique_ptr<FrameRecord> open;
		std::vector<cs::String> pending;
		std::vector<std::pair<int, cs::String>> clockRequests;
		std::vector<Remote> remotes;
		std::map<long long, cs::String> digests;
		bool syncWanted = false;
		cs::String syncReason;
		int syncId = 0;
		std::vector<cs::String> syncChunks;
		bool haveSyncChunks = false;
		long long syncFrame = -1;
		int presenceTick = 0;
		bool syncLoad = false;
		long long pilotSentTick = -1;
		std::map<int, std::vector<PilotFrame>> pilotIn;
		std::map<int, PilotFrame> pilotLast;

		Remote* FindRemote(ICoopLink* link);
		void HostPresence(UnitySpaceApp* app);
		void TryBeginSync(UnitySpaceApp* app);
		void PumpChunks();
		void Hello(Remote& r, cs::String name);
		void Command(Remote& r, const cs::String& entry);
		void Digest(Remote& r, const cs::String& rest);

		// client
		ICoopLink* hostLink = nullptr;
		std::deque<FrameRecord> queue;
		std::vector<cs::String> chunks;
		bool haveChunks = false;
		int chunksGot = 0, clientSyncId = 0;
		long long clientSyncFrame = -1, lastFrame = 0;
		bool running = false, welcomed = false, refused = false;

		void SendDigest(UnitySpaceApp* app, long long frame);
	};
}
