// US-T133: seamless area streaming (see AreaStreaming.h).
#include "Streaming/AreaStreaming.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Streaming.h"
#include "Systems/Galaxy.h"

#include "Rendering/AssetStreamer.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"
#include "Rendering/VoidStationModel.h"
#include "SpaceGodLog.h"
#include "Views/SkyView.h"

#include "Engine/StreamableManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	struct AreaStreaming::Area
	{
		AreaId Id;
		std::weak_ptr<StarSystem> System;
		bool HasSystem = false;

		/// <summary>The priority it is worked at: the highest it was asked at in the last frame it was asked for.</summary>
		StreamPriority Priority = StreamPriority::Background;
		StreamPriority AskedMax = StreamPriority::Background;
		bool AskedNow = false;
		float SinceAsked = 0;
		double FirstAskedAt = 0;

		bool Built = false;
		std::deque<std::function<void()>> Steps;
		/// <summary>Every asset it loads (kept from release while it lives; all in memory for it to be ready).</summary>
		cs::List<cs::String> Paths;
	};

	namespace
	{
		/// <summary>A mapped body of the active system: its colour map and relief map paths (either may be empty).</summary>
		struct BodyMaps
		{
			int Body = -1;
			cs::String Colour, Relief;
		};

		FStreamableManager& Streamable()
		{
			// Never destroyed: the engine's object system is gone by the time statics are.
			static FStreamableManager* manager = new FStreamableManager();
			return *manager;
		}

		const char* PriorityName(StreamPriority p)
		{
			switch (p)
			{
				case StreamPriority::Urgent: return "urgent";
				case StreamPriority::Imminent: return "imminent";
				case StreamPriority::Ahead: return "ahead";
				default: return "background";
			}
		}
	}

	struct AreaStreaming::State
	{
		cs::Dictionary<cs::String, cs::Ref<Area>> Areas;
		std::weak_ptr<Views::SkyView> Sky;
		std::weak_ptr<Galaxy> Galaxy;
		bool AdmitInstalled = false;
		/// <summary>The system the sky slot was last given to.</summary>
		cs::String SkyFor;

		cs::String Active;
		cs::List<BodyMaps> Maps;
		std::vector<int> Visible;
		std::vector<StreamRing> Rings;
		/// <summary>Every relief map seen in a catalogue, and those let in (a body in the Full ring) in the active system.</summary>
		cs::HashSet<cs::String> ReliefPaths, AdmittedRelief;

		bool Released = false;
		bool KitRequested = false;
		TSharedPtr<FStreamableHandle> KitHandle;

		double Clock = 0;
		double LastTickAt = -1;
		int ArrivalFrame = -1;
		int ArrivalPending = 0;
		Stats St;
	};

	AreaStreaming::State& AreaStreaming::S()
	{
		static State state;
		return state;
	}

	const AreaStreaming::Stats& AreaStreaming::Statistics() { return S().St; }

	// ---------------------------------------------------------------- the API

	void AreaStreaming::Prefetch(const AreaId& area, StreamPriority priority)
	{
		auto& s = S();
		cs::String key = area.Key();
		cs::Ref<Area> a;
		if (!s.Areas.TryGetValue(key, a))
		{
			a = cs::New<Area>();
			a->Id = area;
			a->Priority = priority;
			a->FirstAskedAt = s.Clock;
			s.Areas[key] = a;
		}
		if (!a->AskedNow || priority > a->AskedMax) a->AskedMax = priority;
		a->AskedNow = true;
		a->SinceAsked = 0;
	}

	void AreaStreaming::Prefetch(const cs::Ref<StarSystem>& system, StreamPriority priority)
	{
		if (system == nullptr) return;
		Prefetch(AreaId::System(system->Name), priority);
		cs::Ref<Area> a;
		if (S().Areas.TryGetValue(AreaId::System(system->Name).Key(), a) && !a->HasSystem)
		{
			a->System = system;
			a->HasSystem = true;
			a->Built = false; // asked for by a star's name before it was a system: built again, in full
		}
	}

	void AreaStreaming::Prefetch(const cs::Ref<StarSystem>& system, const cs::String& star, StreamPriority priority)
	{
		if (system != nullptr) Prefetch(system, priority);
		else if (!star.empty()) Prefetch(AreaId::System(star), priority);
	}

	bool AreaStreaming::IsReady(const AreaId& area)
	{
		auto& s = S();
		if (area.Kind == AreaKind::Kit) return s.KitRequested && (!s.KitHandle.IsValid() || s.KitHandle->HasLoadCompleted());
		if (area.Name == s.Active) return true;
		cs::Ref<Area> a;
		if (!s.Areas.TryGetValue(area.Key(), a) || !a->Built || !a->Steps.empty()) return false;
		for (const auto& p : a->Paths)
			if (!Rendering::AssetStreamer::Ready(p)) return false;
		auto sky = s.Sky.lock();
		return !sky || sky->PrefetchReady(area.Name);
	}

	bool AreaStreaming::IsReady(const cs::Ref<StarSystem>& system)
	{
		return system != nullptr && IsReady(AreaId::System(system->Name));
	}

	// ---------------------------------------------------------------- rings

	StreamRing AreaStreaming::RingFor(const StreamView& view, DVec3 at, double size, StreamRing current)
	{
		StreamView v = view;
		v.ModelPx = FullRingPx;
		if (Core::Streaming::WantsLoaded(Core::Streaming::Urgency(v, at, size), current == StreamRing::Full)) return StreamRing::Full;
		v.ModelPx = LowRingPx;
		if (Core::Streaming::WantsLoaded(Core::Streaming::Urgency(v, at, size), current != StreamRing::Impostor)) return StreamRing::Low;
		return StreamRing::Impostor;
	}

	StreamRing AreaStreaming::BodyRing(int body)
	{
		auto& s = S();
		return body >= 0 && body < static_cast<int>(s.Rings.size()) ? s.Rings[body] : StreamRing::Impostor;
	}

	void AreaStreaming::UpdateRings(const StreamView& view, const cs::List<DVec3>& bodyScene, const cs::List<double>& bodyRadius, int focusBody)
	{
		auto& s = S();
		int n = cs::Math::Min(bodyScene.Count(), bodyRadius.Count());
		for (int i : s.Visible)
		{
			if (i >= n || i >= static_cast<int>(s.Rings.size())) continue;
			s.Rings[i] = i == focusBody ? StreamRing::Full : RingFor(view, bodyScene[i], bodyRadius[i] * 2, s.Rings[i]);
		}
		// A relief map is let in once its body is close (it stays in while the system is: nothing unloads a map a
		// material is drawing with).
		for (const auto& m : s.Maps)
			if (!m.Relief.empty() && BodyRing(m.Body) == StreamRing::Full && s.AdmittedRelief.Add(m.Relief)) Rendering::AssetStreamer::Warm(m.Relief);
	}

	int AreaStreaming::PendingInView()
	{
		auto& s = S();
		int pending = 0;
		for (const auto& m : s.Maps)
		{
			StreamRing ring = BodyRing(m.Body);
			if (!m.Colour.empty() && ring != StreamRing::Impostor && !Rendering::AssetStreamer::Ready(m.Colour)) pending++;
			if (!m.Relief.empty() && ring == StreamRing::Full && !Rendering::AssetStreamer::Ready(m.Relief)) pending++;
		}
		return pending;
	}

	// ---------------------------------------------------------------- driven by the app

	void AreaStreaming::Attach(const cs::Ref<Views::SkyView>& sky, const cs::Ref<Core::Galaxy>& galaxy)
	{
		auto& s = S();
		s.Sky = sky;
		s.Galaxy = galaxy;
		if (!s.AdmitInstalled)
		{
			s.AdmitInstalled = true;
			Rendering::AssetStreamer::Admit = [](const cs::String& path)
			{
				auto& st = S();
				return !st.ReliefPaths.Contains(path) || st.AdmittedRelief.Contains(path);
			};
		}
		if (!s.KitRequested)
		{
			// Once, at the start: every Star Sparrow mesh loads in the background (headless there is no kit, and hulls
			// are procedural, built from code as before).
			s.KitRequested = true;
			// US-T142: Ultimate Spaceships Creator's meshes instead when that pack is there (it is drawn first).
			bool usc = FApp::CanEverRender() && Rendering::UscKit::Available();
			if (usc || (FApp::CanEverRender() && Rendering::StarSparrowKit::Available()))
			{
				TArray<FSoftObjectPath> paths;
				for (const auto& p : usc ? Rendering::UscKit::AssetPaths() : Rendering::StarSparrowKit::AssetPaths())
				{
					FSoftObjectPath path(FString(UTF8_TO_TCHAR(p.c_str())));
					if (FPackageName::DoesPackageExist(path.GetLongPackageName())) paths.Add(path);
				}
				if (paths.Num() > 0) s.KitHandle = Streamable().RequestAsyncLoad(paths, FStreamableDelegate());
				UE_LOG(LogSpaceGod, Display, TEXT("AreaStreaming: streaming the %s in the background (%d assets)"),
					usc ? TEXT("Ultimate Spaceships Creator pack") : TEXT("Star Sparrow kit"), paths.Num());
			}
		}
	}

	void AreaStreaming::EnterSystem(const cs::Ref<StarSystem>& system)
	{
		auto& s = S();
		if (system == nullptr) return;
		// How well it was streamed ahead (measured before it counts as the active system, which is always ready).
		cs::Ref<Area> a;
		bool asked = s.Areas.TryGetValue(AreaId::System(system->Name).Key(), a);
		bool ready = asked && a->Built && a->Steps.empty();
		if (ready)
			for (const auto& p : a->Paths)
				if (!Rendering::AssetStreamer::Ready(p)) { ready = false; break; }
		auto sky = s.Sky.lock();
		if (ready && sky) ready = sky->PrefetchReady(system->Name);
		s.St.LastArrival = system->Name;
		s.St.LastArrivalReady = ready;
		s.St.LastArrivalAheadS = asked ? s.Clock - a->FirstAskedAt : 0;
		s.St.WorstAfterArrivalMs = 0;
		s.ArrivalFrame = 0;

		s.Active = system->Name;
		s.Maps.Clear();
		s.Visible.clear();
		s.AdmittedRelief.Clear();
		const auto& catalog = system->Catalog;
		int n = catalog->Count();
		s.Rings.assign(static_cast<size_t>(n), StreamRing::Impostor);
		for (int i = 0; i < n; i++)
		{
			const auto& b = (*catalog)[i];
			if (!b->IsVisible()) continue;
			s.Visible.push_back(i);
			if (b->Texture.empty() && b->NormalMap.empty()) continue;
			BodyMaps m;
			m.Body = i;
			m.Colour = Rendering::AssetStreamer::TexturePath(b->Texture);
			m.Relief = Rendering::AssetStreamer::TexturePath(b->NormalMap);
			if (!m.Relief.empty()) s.ReliefPaths.Add(m.Relief);
			s.Maps.Add(m);
		}
	}

	void AreaStreaming::Build(Area& a)
	{
		auto& s = S();
		a.Built = true;
		a.Steps.clear();
		a.Paths.Clear();
		if (a.Id.Kind != AreaKind::System) return;
		cs::Ref<StarSystem> system = a.System.lock();
		if (system == nullptr)
		{
			// Asked for by a star's name: a system by now, perhaps (the galaxy builds stars as the view nears them).
			if (auto galaxy = s.Galaxy.lock()) system = galaxy->Find(a.Id.Name);
			if (system == nullptr) return; // only its sky, which the sky slot streams by name
			a.System = system;
			a.HasSystem = true;
		}

		// Its bodies, biggest first (what is seen from farthest away): colour maps loading in the background, relief maps
		// left for the rings (only near), and the meshes its stations and Void Station are built from made into their caches.
		const auto& catalog = system->Catalog;
		std::vector<int> order;
		for (int i = 0; i < catalog->Count(); i++)
			if ((*catalog)[i]->IsVisible()) order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [&catalog](int x, int y) { return (*catalog)[x]->RadiusKm > (*catalog)[y]->RadiusKm; });
		for (int i : order)
		{
			const auto& b = (*catalog)[i];
			if (!b->Texture.empty())
			{
				cs::String path = Rendering::AssetStreamer::TexturePath(b->Texture);
				a.Paths.Add(path);
				a.Steps.push_back([path] { Rendering::AssetStreamer::Warm(path); });
			}
			if (!b->NormalMap.empty()) s.ReliefPaths.Add(Rendering::AssetStreamer::TexturePath(b->NormalMap));
			if (b->Kind == BodyKind::Station)
			{
				cs::String name = b->Name;
				a.Steps.push_back([name] { Rendering::ShipMeshFactory::GetStationBody(name); });
			}
			if (b->IsStar() && b->Singularity)
			{
				a.Steps.push_back([] { Rendering::VoidStationModel::Shell(false); });
				a.Steps.push_back([] { Rendering::VoidStationModel::Shell(true); });
				a.Steps.push_back([] { Rendering::VoidStationModel::AccretionRing(); });
				a.Steps.push_back([] { Rendering::VoidStationModel::Gimbal(0); Rendering::VoidStationModel::Gimbal(1); });
			}
		}
		// The models of the ships in it (a Kenney craft loads from disk; procedural and kit hulls need nothing here).
		for (const auto& path : Rendering::AssetStreamer::SystemPaths(system))
		{
			if (!cs::Strings::StartsWith(path, "Ships/")) continue;
			a.Paths.Add(path);
			a.Steps.push_back([path] { Rendering::AssetStreamer::Warm(path); });
		}
	}

	void AreaStreaming::Release(Area& a)
	{
		auto& s = S();
		if (a.Id.Kind == AreaKind::System && s.SkyFor == a.Id.Name)
		{
			if (auto sky = s.Sky.lock()) sky->DropPrefetch(a.Id.Name);
			s.SkyFor = cs::String();
		}
		a.Steps.clear();
	}

	void AreaStreaming::Tick(float dt)
	{
		auto& s = S();
		s.Clock += dt;

		// Frame time, and how the frames went after the last arrival (the frame a system is built in, and the worst after).
		double now = FPlatformTime::Seconds();
		double frameMs = s.LastTickAt >= 0 ? (now - s.LastTickAt) * 1000.0 : 0;
		s.LastTickAt = now;
		if (s.ArrivalFrame >= 0)
		{
			if (s.ArrivalFrame == 0)
			{
				s.St.LastArrivalFrameMs = frameMs;
				s.ArrivalPending = PendingInView();
			}
			else s.St.WorstAfterArrivalMs = cs::Math::Max(s.St.WorstAfterArrivalMs, frameMs);
			if (++s.ArrivalFrame > 120)
			{
				s.ArrivalFrame = -1;
				UE_LOG(LogSpaceGod, Display, TEXT("AreaStreaming: entered %s: streamed ahead %s (asked %.1f s before), %d map(s) in view still loading; the arrival frame %.1f ms, the worst of the next 120 %.1f ms"),
					UTF8_TO_TCHAR(s.St.LastArrival.c_str()), s.St.LastArrivalReady ? TEXT("in full") : TEXT("only in part"), s.St.LastArrivalAheadS,
					s.ArrivalPending, s.St.LastArrivalFrameMs, s.St.WorstAfterArrivalMs);
			}
		}

		// Ageing: an area asked for this frame takes the priority it was asked at; one no longer asked for goes in time.
		cs::List<cs::String> expired;
		for (const auto& kv : s.Areas)
		{
			Area& a = *kv.second;
			if (a.AskedNow)
			{
				a.Priority = a.AskedMax;
				a.AskedNow = false;
				a.AskedMax = StreamPriority::Background;
			}
			else
			{
				a.SinceAsked += dt;
				if (a.SinceAsked > ExpireAfterS && a.Id.Kind != AreaKind::Kit) expired.Add(kv.first);
			}
			// A star asked for by name that the galaxy has since built into a system: its full build is due.
			if (a.Built && !a.HasSystem && a.Id.Kind == AreaKind::System)
				if (auto galaxy = s.Galaxy.lock())
					if (galaxy->Find(a.Id.Name) != nullptr) a.Built = false;
		}
		for (const auto& key : expired)
		{
			Release(*s.Areas[key]);
			s.Areas.Remove(key);
			s.Released = true;
		}

		// Most urgent first (then the one asked for longest).
		std::vector<cs::Ref<Area>> order;
		for (const auto& kv : s.Areas) order.push_back(kv.second);
		std::stable_sort(order.begin(), order.end(), [](const cs::Ref<Area>& x, const cs::Ref<Area>& y)
		{
			if (x->Priority != y->Priority) return x->Priority > y->Priority;
			return x->FirstAskedAt < y->FirstAskedAt;
		});

		// The one sky held ahead goes to the most urgent system that is not the one in view.
		if (auto sky = s.Sky.lock())
			for (const auto& a : order)
			{
				if (a->Id.Kind != AreaKind::System || a->Id.Name == s.Active) continue;
				if (s.SkyFor != a->Id.Name)
					UE_LOG(LogSpaceGod, Verbose, TEXT("AreaStreaming: the sky of %s streams in ahead (%s)"), UTF8_TO_TCHAR(a->Id.Name.c_str()), ANSI_TO_TCHAR(PriorityName(a->Priority)));
				sky->Prefetch(a->Id.Name);
				s.SkyFor = a->Id.Name;
				break;
			}

		// The budget: steps of the most urgent areas first, at least one a frame for an Urgent one.
		double start = FPlatformTime::Seconds();
		int done = 0, left = 0;
		for (const auto& a : order)
		{
			if (!a->Built) Build(*a);
			while (!a->Steps.empty())
			{
				double spent = (FPlatformTime::Seconds() - start) * 1000.0;
				bool owed = done == 0 && a->Priority == StreamPriority::Urgent;
				if (spent >= BudgetMs && !owed) break;
				auto step = std::move(a->Steps.front());
				a->Steps.pop_front();
				step();
				done++;
			}
			left += static_cast<int>(a->Steps.size());
		}
		s.St.Areas = s.Areas.Count();
		s.St.StepsLeft = left;
		s.St.StepsDone += done;
		s.St.SpentMs = (FPlatformTime::Seconds() - start) * 1000.0;
		s.St.WorstSpentMs = cs::Math::Max(s.St.WorstSpentMs, s.St.SpentMs);
	}

	void AreaStreaming::Keep(cs::HashSet<cs::String>& keep)
	{
		for (const auto& kv : S().Areas)
			for (const auto& p : kv.second->Paths) keep.Add(p);
	}

	bool AreaStreaming::ConsumeReleased()
	{
		auto& s = S();
		bool released = s.Released;
		s.Released = false;
		return released;
	}
}
