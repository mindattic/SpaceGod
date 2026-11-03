#include "UnityCompat/UnityTexture.h"

#include "UnityCompat/GuiHost.h"

namespace SpaceGod::UnityCompat
{
	Texture::~Texture() = default;

	Texture2D::Texture2D(int width_, int height_, TextureFormat format_, bool mipChain_, bool linear_)
		: format(format_), mipChain(mipChain_), linear(linear_)
	{
		width = width_ > 0 ? width_ : 0;
		height = height_ > 0 ? height_ : 0;
		// Unity's new Texture2D starts grey-ish undefined; every caller fills it before use. White keeps a forgotten
		// pixel visible rather than transparent.
		pixels = cs::List<Color32>();
		for (int i = 0; i < width * height; i++) pixels.Add(Color32(255, 255, 255, 255));
		Dirty = true;
	}

	Texture2D::~Texture2D()
	{
		if (OwnsNative && Native) GuiHost::ReleaseNativeTexture(Native);
		Native = nullptr;
	}

	void Texture2D::SetPixel(int x, int y, Color c)
	{
		if (x < 0 || y < 0 || x >= width || y >= height) return;
		pixels[y * width + x] = Color32::From(c);
	}

	Color Texture2D::GetPixel(int x, int y) const
	{
		if (width == 0 || height == 0) return Color();
		// Unity clamps out-of-range coordinates (wrapMode Clamp) or repeats them; clamping covers every use here.
		x = x < 0 ? 0 : (x >= width ? width - 1 : x);
		y = y < 0 ? 0 : (y >= height ? height - 1 : y);
		return pixels[y * width + x].ToColor();
	}

	void Texture2D::SetPixels(const cs::List<Color>& colors)
	{
		int n = colors.Count() < width * height ? colors.Count() : width * height;
		for (int i = 0; i < n; i++) pixels[i] = Color32::From(colors[i]);
	}

	void Texture2D::SetPixels32(const cs::List<Color32>& colors)
	{
		int n = colors.Count() < width * height ? colors.Count() : width * height;
		for (int i = 0; i < n; i++) pixels[i] = colors[i];
	}

	void Texture2D::Apply(bool, bool) { Dirty = true; }

	static cs::Ref<Texture2D> SolidTexture(Color32 c)
	{
		auto t = cs::New<Texture2D>(4, 4, TextureFormat::RGBA32, false);
		cs::List<Color32> px;
		for (int i = 0; i < 16; i++) px.Add(c);
		t->SetPixels32(px);
		t->Apply();
		return t;
	}

	const cs::Ref<Texture2D>& Texture2D::whiteTexture()
	{
		static const cs::Ref<Texture2D> white = SolidTexture(Color32(255, 255, 255, 255));
		return white;
	}

	const cs::Ref<Texture2D>& Texture2D::blackTexture()
	{
		static const cs::Ref<Texture2D> black = SolidTexture(Color32(0, 0, 0, 255));
		return black;
	}

	cs::Ref<Texture2D> Texture2D::FromNative(void* utexture, int width_, int height_)
	{
		auto t = cs::New<Texture2D>();
		t->width = width_;
		t->height = height_;
		t->Native = utexture;
		t->OwnsNative = false;
		t->Dirty = false;
		return t;
	}
}
