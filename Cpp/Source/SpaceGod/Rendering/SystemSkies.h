#pragma once
// US-T132: every place beyond Sol gets its own painted sky and light, made lazily, cached, and fetchable ahead.

#include "Cs/Cs.h"
#include "Rendering/Pipeline/RenderBackend.h"

#include <atomic>
#include <memory>
#include <vector>

namespace SpaceGod::Rendering
{
	/// <summary>
	/// The skies of the places beyond Sol (US-T132). Each is painted from the place's biome (Core::Biomes) on worker
	/// threads the first time it is wanted, then kept (the last few places), so going back is instant. Nothing is made on
	/// the frame and nothing waits on a loading screen: <see cref="Prefetch"/> starts a sky ahead of arrival (travel and
	/// FTL call it through AssetStreamer::WarmSystem, and the streaming work, US-T133, can call it for any place), and the
	/// sky view swaps the sky in, faded, when it is ready (a 2048 x 1024 sky takes well under a second).
	/// </summary>
	class SystemSkies
	{
	public:
		/// <summary>One place's sky, painting or painted.</summary>
		struct Painted
		{
			std::atomic<bool> Ready{ false };
			/// <summary>BGRA8 levels, top row first, the whole mip chain (empty if it failed).</summary>
			std::vector<std::vector<uint8_t>> Levels;
			int Width = 0;
			double Seconds = 0;
		};

		/// <summary>Whether <paramref name="place"/> gets a painted sky: everywhere but Sol.</summary>
		static bool IsWild(const cs::String& place);

		/// <summary>The sky of <paramref name="place"/> at <paramref name="width"/>: the cached one, or one started now.</summary>
		static std::shared_ptr<Painted> Request(const cs::String& place, int width);

		/// <summary>
		/// Starts painting the sky of <paramref name="place"/> (a system or void-station name) at the detail the settings ask,
		/// unless it is cached or under way; returns at once. Sol is never painted. The entry point for streaming (US-T133).
		/// </summary>
		static void Prefetch(const cs::String& place);

		/// <summary>Whether the sky of <paramref name="place"/> at the current detail is painted and waiting.</summary>
		static bool IsReady(const cs::String& place);

		/// <summary>
		/// The seeded sky of <paramref name="place"/> in ProceduralSky::BakePanorama's layout (RGBA rows bottom-up, the mip
		/// chain after it, <paramref name="mipOffsets"/> where each level starts), for the sky view's worker threads: the painted
		/// sky beyond Sol (waiting for it if it is still being painted, cached or fetched ahead), ProceduralSky's in Sol.
		/// Empty if <paramref name="cancel"/> is set meanwhile (a painting under way still finishes into the cache).
		/// </summary>
		static cs::List<uint8_t> BakePanorama(const cs::String& place, int width, cs::List<int>& mipOffsets, const std::atomic<bool>* cancel);

		/// <summary>The light graded to <paramref name="place"/>'s sky (inactive for Sol).</summary>
		static SystemGrade GradeFor(const cs::String& place);

		/// <summary>The detail a sky is painted at now: the panorama's width.</summary>
		static int CurrentWidth();
	};
}
