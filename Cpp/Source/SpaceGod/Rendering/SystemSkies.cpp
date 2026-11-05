// US-T132: every place beyond Sol gets its own painted sky and light, made lazily, cached, and fetchable ahead.

#include "Rendering/SystemSkies.h"
#include "Rendering/SystemSkyBake.h"
#include "Rendering/ProceduralSky.h"
#include "Rendering/Skyboxes.h"
#include "Systems/Biomes.h"
#include "Views/BodyView.h"
#include "SpaceGodLog.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <thread>
#include <utility>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		/// The skies kept: the place in view, where it came from and a couple fetched ahead (about 11 MB each at 2048).
		constexpr size_t Kept = 5;

		std::mutex& Gate()
		{
			static std::mutex gate;
			return gate;
		}

		/// Most recently wanted first.
		std::list<std::pair<cs::String, std::shared_ptr<SystemSkies::Painted>>>& Cache()
		{
			static std::list<std::pair<cs::String, std::shared_ptr<SystemSkies::Painted>>> cache;
			return cache;
		}

		cs::String KeyOf(const cs::String& place, int width) { return cs::Cat(place, "@", width); }

		void Paint(const cs::String& place, int width, const std::shared_ptr<SystemSkies::Painted>& into)
		{
			// Leaves two cores to the game, and at most half the machine, so a sky fetched ahead never stalls a frame.
			int cores = static_cast<int>(std::thread::hardware_concurrency());
			int threads = std::max(1, std::min(cores - 2, cores / 2));
			// A thread of its own, not the pool: the sky view's workers wait on it from the pool (BakePanorama).
			Async(EAsyncExecution::Thread, [place, width, into, threads]()
			{
				double start = FPlatformTime::Seconds();
				try
				{
					auto layout = SystemSkyBake::Layout(Biomes::For(place));
					into->Levels = SystemSkyBake::Bake(layout, width, threads);
				}
				catch (const std::exception& e)
				{
					UE_LOG(LogSpaceGod, Warning, TEXT("[US-T132] The sky of %s could not be painted: %s"), UTF8_TO_TCHAR(place.c_str()), UTF8_TO_TCHAR(e.what()));
					into->Levels.clear();
				}
				into->Width = width;
				into->Seconds = FPlatformTime::Seconds() - start;
				UE_LOG(LogSpaceGod, Display, TEXT("[US-T132] Painted the sky of %s (%s) at %d in %.2f s"), UTF8_TO_TCHAR(place.c_str()),
					UTF8_TO_TCHAR(Biomes::ToString(Biomes::KindOf(place))), width, into->Seconds);
				into->Ready = true;
			});
		}

		Vector4 Rgb4(BiomeRgb c, float s) { return Vector4(c.R * s, c.G * s, c.B * s, 1); }
	}

	bool SystemSkies::IsWild(const cs::String& place) { return !place.empty() && !Biomes::IsSol(place); }

	int SystemSkies::CurrentWidth() { return SkyboxSettings::NebulaFaceSize() * 4; }

	std::shared_ptr<SystemSkies::Painted> SystemSkies::Request(const cs::String& place, int width)
	{
		cs::String key = KeyOf(place, width);
		std::shared_ptr<Painted> painted;
		{
			std::lock_guard<std::mutex> lock(Gate());
			auto& cache = Cache();
			for (auto it = cache.begin(); it != cache.end(); ++it)
				if (it->first == key)
				{
					cache.splice(cache.begin(), cache, it);
					return it->second;
				}
			painted = std::make_shared<Painted>();
			cache.emplace_front(key, painted);
			// A sky let go of while still painting finishes into nothing: the painter holds its own reference.
			while (cache.size() > Kept) cache.pop_back();
		}
		Paint(place, width, painted);
		return painted;
	}

	void SystemSkies::Prefetch(const cs::String& place)
	{
		if (!IsWild(place)) return;
		Request(place, CurrentWidth());
	}

	bool SystemSkies::IsReady(const cs::String& place)
	{
		cs::String key = KeyOf(place, CurrentWidth());
		std::lock_guard<std::mutex> lock(Gate());
		for (const auto& e : Cache())
			if (e.first == key) return e.second->Ready;
		return false;
	}

	cs::List<uint8_t> SystemSkies::BakePanorama(const cs::String& place, int width, cs::List<int>& mipOffsets, const std::atomic<bool>* cancel)
	{
		if (!IsWild(place)) return ProceduralSky::BakePanorama(ProceduralSky::SeedFor(place), width, mipOffsets, cancel);
		auto painted = Request(place, width);
		while (!painted->Ready)
		{
			if (cancel && cancel->load()) return cs::List<uint8_t>();
			FPlatformProcess::Sleep(0.005f);
		}
		if (painted->Levels.empty() || (cancel && cancel->load())) return cs::List<uint8_t>();
		// BGRA top row first, level by level, to RGBA bottom row first, the levels end to end.
		size_t total = 0;
		for (const auto& l : painted->Levels) total += l.size();
		auto data = cs::NewArray<uint8_t>(static_cast<int>(total));
		mipOffsets = cs::NewArray<int>(static_cast<int>(painted->Levels.size()));
		uint8_t* out = data.Raw().data();
		size_t at = 0;
		int w = width, h = width / 2;
		for (size_t i = 0; i < painted->Levels.size(); i++)
		{
			mipOffsets[static_cast<int>(i)] = static_cast<int>(at);
			const uint8_t* in = painted->Levels[i].data();
			for (int y = 0; y < h; y++)
			{
				const uint8_t* src = in + static_cast<size_t>(h - 1 - y) * w * 4;
				uint8_t* dst = out + at + static_cast<size_t>(y) * w * 4;
				for (int x = 0; x < w; x++, src += 4, dst += 4)
				{
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];
				}
			}
			at += static_cast<size_t>(w) * h * 4;
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}
		return data;
	}

	SystemGrade SystemSkies::GradeFor(const cs::String& place)
	{
		SystemGrade g;
		if (!IsWild(place)) return g;
		Biome b = Biomes::For(place);
		auto layout = SystemSkyBake::Layout(b);
		g.Active = true;
		g.KeyTint = Rgb4(b.KeyTint, 1);
		// The fill comes from the sky's heart, in the colour of the lit gas around it.
		Vector3 heart = Views::BodyView::ToUnity(DVec3(layout.Heart.X, layout.Heart.Y, layout.Heart.Z));
		g.FillDirection = Vector4(heart.x, heart.y, heart.z, 0);
		BiomeRgb fill{ (b.Mid.R + b.Hot.R) * 0.5f, (b.Mid.G + b.Hot.G) * 0.5f, (b.Mid.B + b.Hot.B) * 0.5f };
		g.FillColor = Rgb4(fill, b.Fill);
		// Fog and dust: the haze colour lifted into the shadows (black space too, faintly) and a wash over the frame.
		g.Haze = Rgb4(b.HazeColor, 0.05f * b.Haze);
		float wash = 0.3f * b.Haze;
		float peak = std::max({ b.HazeColor.R, b.HazeColor.G, b.HazeColor.B, 0.01f });
		g.Tint = Vector4(1 - wash + wash * b.HazeColor.R / peak, 1 - wash + wash * b.HazeColor.G / peak, 1 - wash + wash * b.HazeColor.B / peak, 1);
		return g;
	}
}
