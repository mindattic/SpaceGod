#pragma once

// Unity's Texture / Texture2D for textures made in code: the HUD's panel swatches (Hud.Solid), Icons' signed-distance
// sprites, the galaxy map's soft disc. Pixels are kept on the CPU bottom row first, as Unity stores them
// (SetPixel(x, 0) is the bottom row), and uploaded to a transient Unreal UTexture2D (sRGB, as Unity's default
// non-linear Texture2D) the first time the GUI draws the texture after Apply(). Textures that already exist on the
// Unreal side (ship thumbnails, content textures) wrap their UTexture through Native with no pixels.
//
// Held as cs::Ref<Texture2D>: `new Texture2D(n, n, TextureFormat.RGBA32, false) { filterMode = FilterMode.Point }`
// -> `auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false); t->filterMode = FilterMode::Point;`.
// `Object.Destroy(tex)` -> `tex = nullptr;` (the last reference releases the Unreal texture).

#include "UnityCompat/UnityMath.h"

namespace SpaceGod::UnityCompat
{
	/// UnityEngine.Color32: 8-bit RGBA.
	struct Color32
	{
		uint8_t r = 0, g = 0, b = 0, a = 0;
		constexpr Color32() = default;
		constexpr Color32(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_) : r(r_), g(g_), b(b_), a(a_) {}
		/// Color -> Color32 (Unity rounds each clamped channel times 255).
		static Color32 From(Color c)
		{
			auto q = [](float v) { return static_cast<uint8_t>(Mathf::RoundToInt(Mathf::Clamp01(v) * 255.0f)); };
			return Color32(q(c.r), q(c.g), q(c.b), q(c.a));
		}
		Color ToColor() const { return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
		bool operator==(const Color32& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	};

	enum class TextureFormat : int { Alpha8 = 1, RGB24 = 3, RGBA32 = 4, ARGB32 = 5, RGBAHalf = 17, RGBAFloat = 20 };
	enum class FilterMode : int { Point = 0, Bilinear = 1, Trilinear = 2 };
	enum class TextureWrapMode : int { Repeat = 0, Clamp = 1, Mirror = 2, MirrorOnce = 3 };

	/// UnityEngine.Texture: anything GUI.DrawTexture draws.
	class Texture
	{
	public:
		cs::String name;
		int width = 0, height = 0;
		FilterMode filterMode = FilterMode::Bilinear;
		TextureWrapMode wrapMode = TextureWrapMode::Repeat;
		HideFlags hideFlags = HideFlags::None;

		/// The Unreal texture (a UTexture*), made from the pixels on first draw, or set by whoever wraps an existing one.
		void* Native = nullptr;

		virtual ~Texture();
	};

	/// UnityEngine.Texture2D, CPU pixels plus an uploaded copy.
	class Texture2D : public Texture
	{
	public:
		Texture2D() = default;
		Texture2D(int width_, int height_, TextureFormat format = TextureFormat::RGBA32, bool mipChain = true, bool linear = false);
		~Texture2D() override;
		Texture2D(const Texture2D&) = delete;
		Texture2D& operator=(const Texture2D&) = delete;

		TextureFormat format = TextureFormat::RGBA32;
		bool mipChain = true;
		bool linear = false;

		/// x right, y up from the bottom row (Unity's order).
		void SetPixel(int x, int y, Color c);
		Color GetPixel(int x, int y) const;
		/// Row by row from the bottom, width * height of them.
		void SetPixels(const cs::List<Color>& colors);
		void SetPixels32(const cs::List<Color32>& colors);
		cs::List<Color32> GetPixels32() const { return pixels; }
		/// Marks the pixels for upload (the next draw makes or refreshes the Unreal texture). Unity's
		/// updateMipmaps / makeNoLongerReadable are accepted; the pixels stay readable here.
		void Apply(bool updateMipmaps = true, bool makeNoLongerReadable = false);

		/// Texture2D.whiteTexture: a shared white texture.
		static const cs::Ref<Texture2D>& whiteTexture();
		/// Texture2D.blackTexture.
		static const cs::Ref<Texture2D>& blackTexture();

		/// Wraps an Unreal texture made elsewhere (a UTexture*), drawn as is.
		static cs::Ref<Texture2D> FromNative(void* utexture, int width, int height);

		/// The pixels, bottom row first. Uploaded when Dirty.
		cs::List<Color32> pixels;
		bool Dirty = false;
		/// True when Native was made here from pixels (and is released here); false for a wrapped texture.
		bool OwnsNative = false;
	};
}
