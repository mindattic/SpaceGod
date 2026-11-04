// US-T44: CoopPeer.cs (US-T39) in C++; see CoopPeer.h and docs/MIGRATION.md "Co-op". Every message, log line and rule is
// the C# track's, so the two tracks' co-op behaves (and logs) the same.

#include "Coop/CoopPeer.h"

#include "Coop/CoopSmoke.h"
#include "Coop/CoopSettings.h"
#include "Bootstrap.h"
#include "UnitySpaceApp.h"
#include "UnrealSpaceGodLog.h"

#include "Session/CommandCodec.h"
#include "Session/Commands.h"
#include "Session/LocalSession.h"
#include "Session/PlayerId.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"
#include "Templates/UniquePtr.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <algorithm>
#include <cctype>
#include <exception>

using namespace SpaceGod::Core;

namespace SpaceGod::Coop
{
	std::unique_ptr<CoopPeer> CoopPeer::current;

	namespace
	{
		FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

		cs::String Trimmed(cs::String s)
		{
			std::replace(s.begin(), s.end(), '\n', ' ');
			size_t a = s.find_first_not_of(" \t\r");
			if (a == cs::String::npos) return cs::String();
			size_t b = s.find_last_not_of(" \t\r");
			return s.substr(a, b - a + 1);
		}

		bool Blank(const cs::String& s) { return s.find_first_not_of(" \t\r\n") == cs::String::npos; }

		cs::String Join(const std::vector<cs::String>& f, size_t from, char sep = ' ')
		{
			cs::String s;
			for (size_t i = from; i < f.size(); i++)
			{
				if (i > from) s += sep;
				s += f[i];
			}
			return s;
		}

		long long Ms(double since) { return static_cast<long long>((FPlatformTime::Seconds() - since) * 1000.0); }
	}

	CoopPeer::CoopPeer(bool host, const cs::String& name)
	{
		isHost = host;
		localName = Blank(name) ? cs::String(host ? "Host" : "Player") : Trimmed(name);
		if (host)
		{
			auto me = std::make_shared<CoopPlayer>();
			me->Seat = 0;
			me->Name = localName;
			Players[0] = me;
		}
	}

	bool CoopPeer::SkipsMenu() { return (current && current->IsClient()) || CoopSmoke::Active(); }

	namespace
	{
		// US-T64: -SpaceGodCoopLog=<file> writes every co-op line to a file of its own as well, in the log's own line format
		// ([time][frame]LogSpaceGod: <verbosity>: SpaceGodCoop: ...). A Shipping build logs nothing (UE_LOG is compiled out),
		// so this is how tools/play.ps1 -Mode coop-smoke -PackageDir reads a packaged host's and client's lines.
		void ToCoopLog(const TCHAR* verbosity, const FString& message)
		{
			static FCriticalSection lock;
			static TUniquePtr<FArchive> file = []
			{
				FString path;
				if (!FParse::Value(FCommandLine::Get(), TEXT("-SpaceGodCoopLog="), path) || path.IsEmpty()) return TUniquePtr<FArchive>();
				return TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*path, FILEWRITE_AllowRead));
			}();
			if (!file) return;
			FString line = FString::Printf(TEXT("[%s][%3d]LogSpaceGod: %s: SpaceGodCoop: %s\r\n"), *FDateTime::Now().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")),
				static_cast<int>(GFrameCounter % 1000), verbosity, *message);
			FTCHARToUTF8 utf8(*line);
			FScopeLock scope(&lock);
			file->Serialize(const_cast<ANSICHAR*>(utf8.Get()), utf8.Length());
			file->Flush();
		}
	}

	void CoopPeer::Log(const cs::String& message)
	{
		FString text = ToF(message);
		UE_LOG(LogSpaceGod, Display, TEXT("SpaceGodCoop: %s"), *text);
		ToCoopLog(TEXT("Display"), text);
	}

	void CoopPeer::Warn(const cs::String& message)
	{
		FString text = ToF(message);
		UE_LOG(LogSpaceGod, Warning, TEXT("SpaceGodCoop: %s"), *text);
		ToCoopLog(TEXT("Warning"), text);
	}

	// The C# peers prime Core/Persistence's statics snapshot at start (CoopPeer.PrimeSaveStatics): its walk took in what
	// weak-table caches held at the first save. The C++ walk never takes those in (US-T41, StaticTables.cpp), so every
	// process names the same objects whenever it first saves: there is nothing to prime here.

	CoopPeer* CoopPeer::StartHost(const cs::String& name)
	{
		CoopSettings::Hook(); // US-T43
		current.reset(new CoopPeer(true, name));
		current->HostReloads = !Bootstrap::HasCommandLineFlag("-CoopNoHostReload");
		Log(cs::Cat("hosting as ", current->Players[0]->Name, " (player 1), up to ", MaxPlayers, " players; host reloads its sync saves: ",
			current->HostReloads ? "True" : "False"));
		return current.get();
	}

	CoopPeer* CoopPeer::StartClient(ICoopLink* host, const cs::String& name)
	{
		CoopSettings::Hook(); // US-T43
		current.reset(new CoopPeer(false, name));
		current->hostLink = host;
		return current.get();
	}

	void CoopPeer::Stop() { current.reset(); }

	cs::String CoopPeer::NameFromCommandLine(const cs::String& fallback)
	{
		FString v;
		if (FParse::Value(FCommandLine::Get(), TEXT("-CoopName="), v)) return cs::String(TCHAR_TO_UTF8(*v));
		if (!fallback.empty()) return fallback;
		return cs::String(TCHAR_TO_UTF8(FPlatformProcess::UserName()));
	}

	cs::Ref<LocalSession> CoopPeer::NewSession()
	{
		return current == nullptr ? LocalSession::Create(/*seed:*/ 0) : LocalSession::Create(/*seed:*/ 0, /*players:*/ MaxPlayers, /*local:*/ current->localSeat);
	}

	cs::String CoopPeer::NameOf(int seat) const
	{
		return seat >= 0 && seat < MaxPlayers && Players[seat] != nullptr ? Players[seat]->Name : cs::Cat("player ", seat + 1);
	}

	// ================================================================ commands, from either end

	void CoopPeer::Submit(const cs::String& entry)
	{
		if (isHost) pending.push_back(entry);
		else if (hostLink) hostLink->Send(cs::Cat("cmd ", entry));
	}

	void CoopPeer::RequestClock(const cs::String& request)
	{
		if (IsClient() && hostLink) hostLink->Send(request);
	}

	// ---------------------------------------------------------------- US-T43: requests, the helm's hands, sync state

	void CoopPeer::QueueEntry(const cs::String& entry)
	{
		if (isHost) pending.push_back(entry);
	}

	void CoopPeer::RequestSetting(const cs::String& key, const cs::String& value)
	{
		if (IsClient() && hostLink) hostLink->Send(cs::Cat("setreq ", key, " ", value));
	}

	void CoopPeer::RequestTravel(const cs::String& system)
	{
		if (IsClient() && hostLink) hostLink->Send(cs::Cat("travel ", system));
	}

	void CoopPeer::RequestLoad(const cs::String& slot)
	{
		if (IsClient() && hostLink) hostLink->Send(cs::Cat("load ", slot));
	}

	void CoopPeer::RequestResync(const cs::String& why)
	{
		if (IsClient() && hostLink) hostLink->Send(cs::Cat("resync ", why));
	}

	bool CoopPeer::FreshEdges(long long& lastTick)
	{
		if (!EdgesPerTick) return true;
		bool fresh = lastTick != EngineTick;
		lastTick = EngineTick;
		return fresh;
	}

	void CoopPeer::SendPilot(PilotFrame frame)
	{
		if (!IsClient()) return;
		if (!FreshEdges(pilotSentTick)) frame = frame.HeldOnly();
		if (hostLink) hostLink->Send(cs::Cat("pin ", frame.Write()));
		PilotSent++;
	}

	PilotFrame CoopPeer::HostPilotFor(int seat)
	{
		static const std::vector<PilotFrame> none;
		auto in = pilotIn.find(seat);
		auto last = pilotLast.find(seat);
		const std::vector<PilotFrame>& since = in != pilotIn.end() ? in->second : none;
		PilotFrame frame = PilotFrame::Compose(last != pilotLast.end() ? &last->second : nullptr, since);
		if (in != pilotIn.end() && !in->second.empty())
		{
			pilotLast[seat] = in->second.back();
			in->second.clear();
		}
		return frame;
	}

	void CoopPeer::ForgetPilot(int seat)
	{
		pilotIn.erase(seat);
		pilotLast.erase(seat);
	}

	void CoopPeer::RequestSync(const cs::String& reason, bool load)
	{
		syncLoad |= load;
		RequestSync(reason);
	}

	void CoopPeer::Selected(const cs::String& key)
	{
		if (isHost) pending.push_back(cs::Cat("a 0 select ", key));
		else if (hostLink) hostLink->Send(cs::Cat("select ", key));
	}

	void CoopPeer::ApplyRosterEntry(const cs::String& entry)
	{
		auto f = Wire::Split(entry, ' ', 4);
		if (f.size() < 2) return;
		int seat;
		try { seat = Wire::ParseI(f[1]); }
		catch (const std::exception&) { return; }
		if (seat < 0 || seat >= MaxPlayers) return;
		if (f[0] == "j")
		{
			auto p = std::make_shared<CoopPlayer>();
			p->Seat = seat;
			p->Name = f.size() > 2 ? Join(f, 2) : cs::Cat("player ", seat + 1);
			Players[seat] = p;
		}
		else if (f[0] == "l")
		{
			if (Players[seat] != nullptr) Players[seat]->Left = true;
			if (!isHost || seat != 0) Players[seat] = nullptr;
		}
		else if (f[0] == "a")
		{
			if (f.size() >= 3 && f[2] == "select" && Players[seat] != nullptr)
			{
				Players[seat]->Selection = f.size() > 3 ? f[3] : cs::String("n");
				Log(cs::Cat(NameOf(seat), " (player ", seat + 1, ") selected '", Players[seat]->Selection, "' at frame ", Frame()));
			}
			else if (f.size() >= 3) Log(cs::Cat(NameOf(seat), " (player ", seat + 1, ") asked: ", Join(f, 2), " (applied at frame ", Frame(), ")"));
		}
	}

	// ================================================================ host

	long long CoopPeer::Frame() const
	{
		if (isHost) return frameNo;
		return ClientRecord != nullptr ? ClientRecord->Frame : lastFrame;
	}

	FrameRecord* CoopPeer::HostOpenFrame()
	{
		if (open) return open.get();
		open = std::make_unique<FrameRecord>();
		open->Frame = ++frameNo;
		open->Dt = Time::unscaledDeltaTime;
		open->Entries = std::move(pending);
		pending.clear();
		return open.get();
	}

	std::vector<std::pair<int, cs::String>> CoopPeer::TakeClockRequests()
	{
		auto list = std::move(clockRequests);
		clockRequests.clear();
		return list;
	}

	void CoopPeer::HostEndUpdate(UnitySpaceApp* app)
	{
		if (open)
		{
			std::unique_ptr<FrameRecord> rec = std::move(open);
			cs::String msg = rec->Write();
			for (auto& r : remotes)
				if (r.State == RemoteState::Syncing || r.State == RemoteState::Running) r.Link->Send(msg);
			if (app != nullptr && rec->Frame % DigestEvery == 0 && (!remotes.empty() || CoopSmoke::Active()))
			{
				cs::String d = app->CoopDigest();
				digests[rec->Frame] = d;
				digests.erase(rec->Frame - DigestEvery * 64);
				Log(cs::Cat("digest frame ", rec->Frame, " = ", d, " (", localName, ", player 1)"));
			}
			if (app != nullptr && ++presenceTick % PresenceEvery == 0) HostPresence(app);
			CoopSmoke::HostFrame(*this, app, *rec);
		}
		if (syncWanted && app != nullptr) TryBeginSync(app);
		PumpChunks();
	}

	void CoopPeer::HostPresence(UnitySpaceApp* app)
	{
		if (auto me = Players[0]) app->CoopPresence(me->Body, me->OffsetKm);
		if (remotes.empty()) return;
		cs::String msg = "roster";
		for (const auto& p : Players)
			if (p != nullptr)
				msg += cs::Cat("\n", p->Seat, " ", p->Body, " ", Wire::D(p->OffsetKm.X), " ", Wire::D(p->OffsetKm.Y), " ", Wire::D(p->OffsetKm.Z));
		for (auto& r : remotes)
			if (r.State == RemoteState::Running) r.Link->SendUnreliable(msg);
	}

	void CoopPeer::RequestSync(const cs::String& reason)
	{
		if (!isHost) return;
		if (!syncWanted) Log(cs::Cat("sync wanted: ", reason));
		syncWanted = true;
		syncReason = reason;
	}

	void CoopPeer::TryBeginSync(UnitySpaceApp* app)
	{
		if (!app->CoopSaveBlocker().empty()) return; // tried again next frame
		syncWanted = false;
		double t0 = FPlatformTime::Seconds();
		// US-T43: what the save does not hold (who flies the helm, every player's seat, the shared settings), taken before it,
		// so every peer (this one too) takes the seats again after loading the same bytes.
		cs::String state = cs::Cat(app->CoopSyncState(!syncLoad), "\n", CoopSettings::Snapshot());
		syncLoad = false;
		PendingSyncState = state;
		cs::List<uint8_t> save = app->CoopCaptureSave();
		long long captured = Ms(t0);
		if (HostReloads)
		{
			applying = true;
			try { app->CoopApplySave(save, state); }
			catch (...)
			{
				applying = false;
				throw;
			}
			applying = false;
		}
		long long reloaded = Ms(t0);
		syncChunks = Wire::Pack(save);
		haveSyncChunks = true;
		syncId++;
		syncFrame = frameNo;
		cs::String digest = app->CoopDigest();
		digests[frameNo] = digest;
		int sent = 0;
		for (auto& r : remotes)
		{
			if (r.State == RemoteState::Joining) continue;
			r.State = RemoteState::Syncing;
			r.SyncId = syncId;
			r.NextChunk = 0;
			r.Acked = -1;
			r.Link->Send(cs::Cat("sync ", syncId, " ", syncFrame, " ", static_cast<int>(syncChunks.size())));
			r.Link->Send(cs::Cat("syncstate ", syncId, "\n", state)); // US-T43
			sent++;
		}
		Log(cs::Cat("sync #", syncId, " at frame ", syncFrame, " (", syncReason, "): save ", save.Count() / 1024, " KB in ", static_cast<int>(syncChunks.size()),
			" chunks to ", sent, " player(s); captured in ", captured, " ms",
			HostReloads ? cs::Cat(", host reloaded it in ", reloaded - captured, " ms") : cs::String(),
			"; digest frame ", syncFrame, " = ", digest, " (", localName, ", player 1)"));
	}

	void CoopPeer::PumpChunks()
	{
		if (!haveSyncChunks) return;
		const int count = static_cast<int>(syncChunks.size());
		for (auto& r : remotes)
		{
			if (r.State != RemoteState::Syncing || r.SyncId != syncId) continue;
			while (r.NextChunk < count && r.NextChunk - (r.Acked + 1) < ChunkWindow)
			{
				r.Link->Send(cs::Cat("chunk ", syncId, " ", r.NextChunk, " ", syncChunks[static_cast<size_t>(r.NextChunk)]));
				r.NextChunk++;
			}
		}
	}

	CoopPeer::Remote* CoopPeer::FindRemote(ICoopLink* link)
	{
		for (auto& r : remotes)
			if (r.Link == link) return &r;
		return nullptr;
	}

	void CoopPeer::HostLogin(ICoopLink* link)
	{
		Remote r;
		r.Link = link;
		r.State = RemoteState::Joining;
		remotes.push_back(r);
	}

	void CoopPeer::HostLogout(ICoopLink* link)
	{
		auto it = std::find_if(remotes.begin(), remotes.end(), [link](const Remote& r) { return r.Link == link; });
		if (it == remotes.end()) return;
		int seat = it->Seat;
		remotes.erase(it);
		if (seat > 0)
		{
			pending.push_back(cs::Cat("s ", CommandCodec::Write(cs::New<ReleaseSeatCommand>(PlayerId(seat)))));
			pending.push_back(cs::Cat("h ", seat, " release")); // US-T43: a helm they flew goes back to its crew too
			pending.push_back(cs::Cat("l ", seat));
			ForgetPilot(seat);
			Log(cs::Cat(NameOf(seat), " (player ", seat + 1, ") left"));
		}
	}

	bool CoopPeer::AnyBye() const
	{
		for (const auto& r : remotes)
			if (r.Bye) return true;
		return false;
	}

	void CoopPeer::HostReceive(ICoopLink* link, const cs::String& msg)
	{
		Remote* rp = FindRemote(link);
		if (rp == nullptr || msg.empty()) return;
		Remote& r = *rp;
		size_t sp = msg.find(' ');
		cs::String kind = sp == cs::String::npos ? msg : msg.substr(0, sp);
		cs::String rest = sp == cs::String::npos ? cs::String() : msg.substr(sp + 1);
		try
		{
			if (kind == "hello") Hello(r, rest);
			else if (kind == "cmd") Command(r, rest);
			else if (kind == "warp" || kind == "pause")
			{
				clockRequests.emplace_back(r.Seat, msg);
				pending.push_back(cs::Cat("a ", r.Seat, " ", msg));
			}
			else if (kind == "select") pending.push_back(cs::Cat("a ", r.Seat, " select ", rest));
			else if (kind == "ack")
			{
				auto f = Wire::Split(rest, ' ');
				if (Wire::ParseI(f[0]) == r.SyncId) r.Acked = std::max(r.Acked, Wire::ParseI(f[1]));
			}
			else if (kind == "synced")
			{
				if (Wire::ParseI(rest) == r.SyncId && r.State == RemoteState::Syncing)
				{
					r.State = RemoteState::Running;
					Log(cs::Cat(NameOf(r.Seat), " (player ", r.Seat + 1, ") is in sync #", r.SyncId, " from frame ", syncFrame));
				}
			}
			else if (kind == "digest") Digest(r, rest);
			else if (kind == "pres")
			{
				auto f = Wire::Split(rest, ' ');
				auto p = r.Seat >= 0 ? Players[r.Seat] : nullptr;
				if (p != nullptr && f.size() >= 4)
				{
					p->Body = Wire::ParseI(f[0]);
					p->OffsetKm = DVec3(Wire::ParseD(f[1]), Wire::ParseD(f[2]), Wire::ParseD(f[3]));
				}
			}
			else if (kind == "resync")
				RequestSync(!rest.empty() && std::isdigit(static_cast<unsigned char>(rest[0])) ? cs::Cat(NameOf(r.Seat), " fell ", rest, " frames behind")
					: cs::Cat(NameOf(r.Seat), " asked: ", rest));
			// US-T43: the helm's hands, and what a client asks of the host.
			else if (kind == "pin")
			{
				if (r.Seat >= 0) pilotIn[r.Seat].push_back(PilotFrame::Read(rest));
			}
			else if (kind == "setreq")
			{
				size_t sp2 = rest.find(' ');
				cs::String key = sp2 != cs::String::npos && sp2 > 0 ? rest.substr(0, sp2) : rest;
				if (sp2 == cs::String::npos || !CoopSettings::IsSharedKey(key)) Warn(cs::Cat("player ", r.Seat + 1, " asked to set '", key, "', not a shared setting"));
				else
				{
					pending.push_back(cs::Cat("a ", r.Seat, " set ", rest));
					pending.push_back(cs::Cat("set ", rest));
				}
			}
			else if (kind == "travel") pending.push_back(cs::Cat("t ", r.Seat, " ", rest));
			else if (kind == "load") pending.push_back(cs::Cat("a ", r.Seat, " load ", rest));
			else if (kind == "bye")
			{
				r.Bye = true;
				Log(cs::Cat(NameOf(r.Seat), " (player ", r.Seat + 1, ") says bye"));
			}
			else Warn(cs::Cat("unknown message '", kind, "' from player ", r.Seat + 1));
		}
		catch (const std::exception& e)
		{
			Warn(cs::Cat("bad message from player ", r.Seat + 1, " ('", msg.size() > 80 ? msg.substr(0, 80) : msg, "'): ", e.what()));
		}
	}

	void CoopPeer::Hello(Remote& r, cs::String name)
	{
		if (r.Seat >= 0) return;
		int seat = -1;
		for (int i = 1; i < MaxPlayers; i++)
			if (Players[i] == nullptr) { seat = i; break; }
		if (seat < 0)
		{
			r.Link->Send("full");
			Warn(cs::Cat("refused ", name, ": all ", MaxPlayers, " seats are taken"));
			return;
		}
		name = Blank(name) ? cs::Cat("player ", seat + 1) : Trimmed(name);
		r.Seat = seat;
		r.State = RemoteState::Welcomed;
		// The roster as it is; joins after this come in the frames (the "j" entry below is one).
		cs::String welcome = cs::Cat("welcome ", seat, " ", MaxPlayers);
		for (const auto& p : Players)
			if (p != nullptr) welcome += cs::Cat("\n", p->Seat, " ", p->Name);
		r.Link->Send(welcome);
		pending.push_back(cs::Cat("j ", seat, " ", name));
		auto p = std::make_shared<CoopPlayer>();
		p->Seat = seat;
		p->Name = name;
		Players[seat] = p;
		Log(cs::Cat(name, " joined as player ", seat + 1));
		RequestSync(cs::Cat(name, " joined"));
	}

	void CoopPeer::Command(Remote& r, const cs::String& entry)
	{
		// US-T43: the app's own entries ("h" the helm, "k" a cockpit key, "x" an action that writes the Core), each
		// "<kind> <seat> ...": a client speaks only for its own seat.
		if (entry.size() > 2 && (entry[0] == 'h' || entry[0] == 'k' || entry[0] == 'x') && entry[1] == ' ')
		{
			auto f = Wire::Split(entry, ' ', 3);
			int seat = -1;
			bool ok = f.size() >= 3;
			if (ok)
			{
				try { seat = Wire::ParseI(f[1]); }
				catch (const std::exception&) { ok = false; }
			}
			if (!ok || seat != r.Seat)
			{
				Warn(cs::Cat("player ", r.Seat + 1, " sent '", entry, "' for another seat: dropped"));
				return;
			}
			pending.push_back(entry);
			return;
		}
		if (!Wire::StartsWith(entry, "s "))
		{
			Warn(cs::Cat("unknown command entry '", entry, "'"));
			return;
		}
		cs::String codec = entry.substr(2);
		size_t at = codec.find(" @");
		auto cmd = CommandCodec::Read(at == cs::String::npos ? codec : codec.substr(0, at));
		// A client speaks only for its own seat.
		if (cmd->Player().Seat != r.Seat)
		{
			Warn(cs::Cat("player ", r.Seat + 1, " sent a command for ", cmd->Player().ToString(), ": dropped"));
			return;
		}
		pending.push_back(entry);
	}

	void CoopPeer::Digest(Remote& r, const cs::String& rest)
	{
		auto f = Wire::Split(rest, ' ');
		long long frame = Wire::ParseL(f[0]);
		const cs::String& theirs = f.at(1);
		auto it = digests.find(frame);
		if (it == digests.end())
		{
			Warn(cs::Cat("player ", r.Seat + 1, "'s digest for frame ", frame, " has nothing to compare with"));
			return;
		}
		const cs::String& ours = it->second;
		if (ours == theirs)
		{
			DigestMatches++;
			Log(cs::Cat("frame ", frame, " digests match: host ", ours, " = ", NameOf(r.Seat), " (player ", r.Seat + 1, ") ", theirs));
			return;
		}
		Desyncs++;
		Warn(cs::Cat("DESYNC at frame ", frame, ": host ", ours, ", ", NameOf(r.Seat), " (player ", r.Seat + 1, ") ", theirs));
		if (r.State != RemoteState::Running) return; // a sync is already on its way
		if (++r.Resyncs > MaxResyncs)
		{
			Warn(cs::Cat("player ", r.Seat + 1, " has desynced ", r.Resyncs, " times: not resyncing again"));
			return;
		}
		RequestSync(cs::Cat("desync at frame ", frame, " with player ", r.Seat + 1));
	}

	// ================================================================ client

	void CoopPeer::ClientHello()
	{
		if (hostLink) hostLink->Send(cs::Cat("hello ", localName));
	}

	void CoopPeer::ClientFrameDone(UnitySpaceApp* app)
	{
		if (queue.empty()) return;
		const long long frame = queue.front().Frame;
		queue.pop_front();
		lastFrame = frame;
		FramesSinceSync++;
		if (frame % DigestEvery == 0) SendDigest(app, frame);
		if (frame % PresenceEvery == 0)
		{
			int body = -1;
			DVec3 off;
			app->CoopPresence(body, off);
			if (auto me = Players[localSeat])
			{
				me->Body = body;
				me->OffsetKm = off;
			}
			if (hostLink) hostLink->SendUnreliable(cs::Cat("pres ", body, " ", Wire::D(off.X), " ", Wire::D(off.Y), " ", Wire::D(off.Z)));
		}
		if (static_cast<int>(queue.size()) > MaxBacklog && running)
		{
			Warn(cs::Cat(static_cast<int>(queue.size()), " frames behind the host: asking for a resync"));
			if (hostLink) hostLink->Send(cs::Cat("resync ", static_cast<int>(queue.size())));
			queue.clear();
		}
	}

	void CoopPeer::SendDigest(UnitySpaceApp* app, long long frame)
	{
		cs::String d = app->CoopDigest();
		Log(cs::Cat("digest frame ", frame, " = ", d, " (", localName, ", player ", localSeat + 1, ")"));
		if (hostLink) hostLink->Send(cs::Cat("digest ", frame, " ", d));
	}

	void CoopPeer::SayBye()
	{
		if (hostLink) hostLink->Send("bye");
	}

	void CoopPeer::ClientReceive(const cs::String& msg)
	{
		if (msg.empty()) return;
		size_t sp = msg.find_first_of(" \n");
		cs::String kind = sp == cs::String::npos ? msg : msg.substr(0, sp);
		try
		{
			if (kind == "frame")
			{
				if (haveChunks || running) queue.push_back(FrameRecord::Read(msg));
			}
			else if (kind == "welcome")
			{
				auto lines = Wire::Split(msg, '\n');
				auto f = Wire::Split(lines[0], ' ');
				localSeat = Wire::ParseI(f.at(1));
				for (size_t i = 1; i < lines.size(); i++)
				{
					auto p = Wire::Split(lines[i], ' ', 2);
					int seat = Wire::ParseI(p[0]);
					if (seat < 0 || seat >= MaxPlayers) continue;
					auto player = std::make_shared<CoopPlayer>();
					player->Seat = seat;
					player->Name = p.size() > 1 ? p[1] : cs::Cat("player ", seat + 1);
					Players[seat] = player;
				}
				auto me = std::make_shared<CoopPlayer>();
				me->Seat = localSeat;
				me->Name = localName;
				Players[localSeat] = me;
				welcomed = true;
				Log(cs::Cat("welcomed by ", NameOf(0), " as player ", localSeat + 1, " (", localName, ")"));
			}
			else if (kind == "full")
			{
				refused = true;
				Warn("the host's seats are all taken");
			}
			else if (kind == "sync")
			{
				auto f = Wire::Split(msg, ' ');
				clientSyncId = Wire::ParseI(f.at(1));
				clientSyncFrame = Wire::ParseL(f.at(2));
				chunks.assign(static_cast<size_t>(Wire::ParseI(f.at(3))), cs::String());
				haveChunks = true;
				chunksGot = 0;
				ClientSyncState.clear();
				HaveClientSyncState = false;
				running = false;
				// Everything queued is from before the save; what comes now is after it.
				queue.clear();
			}
			else if (kind == "syncstate")
			{
				// US-T43: "syncstate <id>\n<the state>", sent just after its "sync".
				size_t nl = msg.find('\n');
				if (nl != cs::String::npos && nl > 10 && Wire::ParseI(msg.substr(10, nl - 10)) == clientSyncId)
				{
					ClientSyncState = msg.substr(nl + 1);
					HaveClientSyncState = true;
				}
			}
			else if (kind == "chunk")
			{
				// "chunk <id> <i> <data>"
				size_t a = msg.find(' ', 6), b = msg.find(' ', a + 1);
				int id = Wire::ParseI(msg.substr(6, a - 6)), i = Wire::ParseI(msg.substr(a + 1, b - a - 1));
				if (id != clientSyncId || !haveChunks || i < 0 || i >= static_cast<int>(chunks.size())) return;
				if (chunks[static_cast<size_t>(i)].empty()) chunksGot++;
				chunks[static_cast<size_t>(i)] = msg.substr(b + 1);
				if (hostLink) hostLink->Send(cs::Cat("ack ", id, " ", i));
			}
			else if (kind == "roster")
			{
				auto lines = Wire::Split(msg, '\n');
				for (size_t i = 1; i < lines.size(); i++)
				{
					auto f = Wire::Split(lines[i], ' ');
					if (f.size() < 5) continue;
					int seat = Wire::ParseI(f[0]);
					if (seat == localSeat || seat < 0 || seat >= MaxPlayers || Players[seat] == nullptr) continue;
					Players[seat]->Body = Wire::ParseI(f[1]);
					Players[seat]->OffsetKm = DVec3(Wire::ParseD(f[2]), Wire::ParseD(f[3]), Wire::ParseD(f[4]));
				}
			}
			else Warn(cs::Cat("unknown message '", kind, "' from the host"));
		}
		catch (const std::exception& e)
		{
			Warn(cs::Cat("bad message from the host ('", msg.size() > 80 ? msg.substr(0, 80) : msg, "'): ", e.what()));
		}
	}

	bool CoopPeer::ClientTryLoad(UnitySpaceApp* app)
	{
		if (!haveChunks || chunksGot < static_cast<int>(chunks.size()) || app == nullptr || !HaveClientSyncState) return false;
		double t0 = FPlatformTime::Seconds();
		cs::List<uint8_t> save = Wire::Unpack(chunks);
		chunks.clear();
		haveChunks = false;
		applying = true;
		try
		{
			app->CoopApplySave(save, ClientSyncState);
		}
		catch (const std::exception& e)
		{
			applying = false;
			Warn(cs::Cat("could not load the host's save (sync #", clientSyncId, "): ", e.what()));
			refused = true;
			return false;
		}
		applying = false;
		lastFrame = clientSyncFrame;
		FramesSinceSync = 0;
		running = true;
		if (hostLink) hostLink->Send(cs::Cat("synced ", clientSyncId));
		Log(cs::Cat("loaded sync #", clientSyncId, " at frame ", clientSyncFrame, ": save ", save.Count() / 1024, " KB in ", Ms(t0), " ms, ",
			static_cast<int>(queue.size()), " frames already waiting"));
		SendDigest(app, clientSyncFrame);
		return true;
	}
}
