// US-T54: the C# track's IMGUI drawn through Slate, as the C++ track's GuiHost draws it, so text at a clip edge (scroll
// views, TextClipping.Clip) is cut through its glyphs and rotated quads exactly at the clip, as Unity cuts them. AHUD's
// Canvas cannot clip (FCanvas has no clip stack in UE5; US-T40's render-target compositing from the HUD crashed the
// renderer), and UnrealSharp exposes no Slate drawing, so this is a small native layer bound to C# with UnrealSharp's
// binds registry (no reflection, no glue):
//
//   C# (UnityCompat/Imgui/GuiCanvases.cs, SlateCanvas) runs Unity's OnGUI cycle in ASpaceHud's DrawHUD as before and
//   records the Repaint pass here -- Begin, then Clip / Texture / Text in call order, then End -- and measures text
//   with Slate's font measure (the C++ track's FSlateFontMeasure over the same Arial composite font).
//   A full-screen, hit-test-invisible Slate leaf widget on the game viewport replays the last recorded frame in its
//   OnPaint: boxes with UV regions, text in Arial at Unity's pixel em, each under its FSlateClippingZone, a new layer
//   whenever the kind of element changes (GuiHost.cpp's FSlateGuiRenderer, element for element).
//
// Also UploadTexture: a Texture2D's pixels as the UTexture2D's mips, with Unity's mip chain (box-filtered down to 1x1,
// as Unity's Apply(updateMipmaps) and the C++ track's GuiHost::Upload make it), filter, wrap and sRGB, so small icons
// minify trilinearly as the original's do.

#include "CoreMinimal.h"
#include "CSBindsRegistry.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "TextureResource.h"
#include "Widgets/SLeafWidget.h"

namespace SpaceGodGuiOverlay
{
	enum class ECmd : uint8 { Clip, Texture, Text };

	struct FCmd
	{
		ECmd Kind = ECmd::Texture;
		FBox2f Rect;          // screen pixels (x0, y0) - (x1, y1); for a clip, the zone (an empty box: no clip)
		FBox2f Uv;            // texture: the region, 0..1, v down
		FLinearColor Color;
		float Rotation = 0;   // texture: degrees clockwise about its centre
		TWeakObjectPtr<UTexture> Texture;
		FString Text;
		int32 Size = 0;       // text: Unity font size (pixels)
		int32 Style = 0;      // text: Unity FontStyle (Normal, Bold, Italic, BoldAndItalic)
	};

	TArray<FCmd> Building, Shown;
	uint64 ShownFrame = 0;

	// ---------------------------------------------------------------- fonts (GuiHost.cpp's Arial / FontFor)

	/// Arial from the system font folder (Unity's built-in font on Windows), its four faces as one composite font; null
	/// where Arial is not installed (then Slate's Roboto).
	TSharedPtr<const FCompositeFont> Arial()
	{
		static TSharedPtr<FStandaloneCompositeFont> Font;
		static bool bTried = false;
		if (bTried) return Font;
		bTried = true;
		FString Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
		if (Dir.IsEmpty()) return Font;
		Dir = FPaths::Combine(Dir, TEXT("Fonts"));
		const TCHAR* Faces[4] = { TEXT("Regular"), TEXT("Bold"), TEXT("Italic"), TEXT("BoldItalic") };
		const TCHAR* Files[4] = { TEXT("arial.ttf"), TEXT("arialbd.ttf"), TEXT("ariali.ttf"), TEXT("arialbi.ttf") };
		const FString Regular = FPaths::Combine(Dir, Files[0]);
		if (!FPaths::FileExists(Regular)) return Font;
		Font = MakeShared<FStandaloneCompositeFont>();
		for (int32 i = 0; i < 4; i++)
		{
			FString Path = FPaths::Combine(Dir, Files[i]);
			if (!FPaths::FileExists(Path)) Path = Regular;
			Font->DefaultTypeface.AppendFont(Faces[i], Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		return Font;
	}

	FSlateFontInfo FontFor(int32 Size, int32 Style)
	{
		const TCHAR* Face = Style == 1 ? TEXT("Bold") : Style == 2 ? TEXT("Italic") : Style == 3 ? TEXT("BoldItalic") : TEXT("Regular");
		// Unity's IMGUI size is the em in pixels; Slate's is points at 96 dpi.
		const float Points = static_cast<float>(Size) * 0.75f;
		if (TSharedPtr<const FCompositeFont> Font = Arial()) return FSlateFontInfo(Font, Points, FName(Face));
		return FCoreStyle::GetDefaultFontStyle(Face, Points);
	}

	// ---------------------------------------------------------------- textures

	struct FBrushEntry
	{
		TWeakObjectPtr<UTexture> Texture;
		TSharedPtr<FSlateBrush> Brush;
	};
	TMap<UTexture*, FBrushEntry> Brushes;

	const FSlateBrush* BrushFor(UTexture* Texture)
	{
		FBrushEntry& Entry = Brushes.FindOrAdd(Texture);
		if (!Entry.Brush.IsValid() || Entry.Texture.Get() != Texture)
		{
			Entry.Texture = Texture;
			Entry.Brush = MakeShared<FSlateBrush>();
			Entry.Brush->SetResourceObject(Texture);
			FVector2f Size(1, 1);
			if (UTexture2D* T2 = Cast<UTexture2D>(Texture)) Size = FVector2f(FMath::Max(T2->GetSizeX(), 1), FMath::Max(T2->GetSizeY(), 1));
			Entry.Brush->ImageSize = Size;
			Entry.Brush->DrawAs = ESlateBrushDrawType::Image;
			Entry.Brush->Tiling = ESlateBrushTileType::NoTile;
		}
		return Entry.Brush.Get();
	}

	// ---------------------------------------------------------------- the overlay

	int32 PaintFrame(const FGeometry& Allotted, FSlateWindowElementList& Out, int32 LayerId)
	{
		// Only a frame the HUD drew this frame or the last (none while the HUD is hidden or gone).
		if (Shown.Num() == 0 || GFrameCounter > ShownFrame + 1) return LayerId;
		const float Scale = Allotted.Scale > 0 ? Allotted.Scale : 1.0f;
		const FVector2f LocalSize = Allotted.GetLocalSize();
		// A child whose local unit is one pixel, as GuiHost's.
		const FGeometry Px = Allotted.MakeChild(LocalSize * Scale, FSlateLayoutTransform(1.0f / Scale));
		auto Geometry = [&Px](const FBox2f& R, float Rotation)
		{
			const FVector2f Size = R.GetSize();
			if (Rotation == 0) return Px.ToPaintGeometry(Size, FSlateLayoutTransform(R.Min));
			return Px.ToPaintGeometry(Size, FSlateLayoutTransform(R.Min),
				FSlateRenderTransform(FQuat2f(FMath::DegreesToRadians(Rotation))), FVector2f(0.5f, 0.5f));
		};

		int32 Layer = LayerId;
		const void* LastKey = nullptr;
		static int32 TextKey = 0;
		bool bClipped = false;
		FBox2f Clip(ForceInit);
		auto Next = [&Layer, &LastKey](const void* Key)
		{
			// Draw order is the GUI's call order: a new layer whenever the kind of element changes (Slate batches within
			// a layer, which could otherwise put a later box under an earlier label).
			if (Key != LastKey) Layer++;
			LastKey = Key;
		};

		for (const FCmd& C : Shown)
		{
			switch (C.Kind)
			{
				case ECmd::Clip:
				{
					const bool bWant = C.Rect.bIsValid;
					if (bWant == bClipped && (!bWant || C.Rect == Clip)) break;
					if (bClipped) Out.PopClip();
					bClipped = bWant;
					Clip = C.Rect;
					if (bClipped) Out.PushClip(FSlateClippingZone(Geometry(C.Rect, 0)));
					LastKey = nullptr;
					break;
				}
				case ECmd::Texture:
				{
					UTexture* Texture = C.Texture.Get();
					if (Texture == nullptr) break;
					FSlateBrush Brush = *BrushFor(Texture); // a per-draw copy: the UV region differs between draws
					if (C.Uv.Min != FVector2f(0, 0) || C.Uv.Max != FVector2f(1, 1)) Brush.SetUVRegion(C.Uv);
					Next(Texture);
					FSlateDrawElement::MakeBox(Out, Layer, Geometry(C.Rect, C.Rotation), &Brush, ESlateDrawEffect::None, C.Color);
					break;
				}
				case ECmd::Text:
				{
					Next(&TextKey);
					FSlateDrawElement::MakeText(Out, Layer, Geometry(C.Rect, 0), C.Text, FontFor(C.Size, C.Style), ESlateDrawEffect::None, C.Color);
					break;
				}
			}
		}
		if (bClipped) Out.PopClip();
		return Layer;
	}

	class SGuiOverlay final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGuiOverlay) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Allotted, const FSlateRect&, FSlateWindowElementList& Out,
			int32 LayerId, const FWidgetStyle&, bool) const override
		{
			return PaintFrame(Allotted, Out, LayerId);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
	};

	TSharedPtr<SGuiOverlay> Overlay;
	TWeakObjectPtr<UGameViewportClient> InstalledOn;

	/// Puts the overlay on the game viewport (again, after a map load cleared its widgets), above the game's own
	/// viewport widgets, as Unity's OnGUI draws over everything.
	void EnsureInstalled()
	{
		if (!FSlateApplication::IsInitialized()) return;
		UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
		if (Viewport == nullptr) return;
		if (!Overlay.IsValid()) Overlay = SNew(SGuiOverlay);
		const bool bAttached = Overlay->GetParentWidget().IsValid();
		if (InstalledOn.Get() != Viewport || !bAttached)
		{
			if (bAttached && InstalledOn.IsValid()) InstalledOn->RemoveViewportWidgetContent(Overlay.ToSharedRef());
			Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 10000);
			InstalledOn = Viewport;
		}
	}

	// ---------------------------------------------------------------- texture upload

	void WriteMip(FTexture2DMipMap& Mip, const TArray<uint8>& Bgra)
	{
		Mip.BulkData.Lock(LOCK_READ_WRITE);
		void* Dst = Mip.BulkData.Realloc(Bgra.Num());
		FMemory::Memcpy(Dst, Bgra.GetData(), Bgra.Num());
		Mip.BulkData.Unlock();
	}
}

DECLARE_UNREALSHARP_BINDER(SpaceGodGui)
{
	using namespace SpaceGodGuiOverlay;

	/// Starts recording a GUI frame (the Repaint pass of ASpaceHud's DrawHUD).
	void Begin()
	{
		Building.Reset();
	}

	/// Shows the recorded frame from the next paint on.
	void End()
	{
		Swap(Building, Shown);
		Building.Reset();
		ShownFrame = GFrameCounter;
		EnsureInstalled();
	}

	/// The clip for the draws after it (screen pixels); On == 0: none.
	void Clip(float X, float Y, float W, float H, int32 On)
	{
		FCmd& C = Building.AddDefaulted_GetRef();
		C.Kind = ECmd::Clip;
		C.Rect = On ? FBox2f(FVector2f(X, Y), FVector2f(X + FMath::Max(W, 0.0f), Y + FMath::Max(H, 0.0f))) : FBox2f(ForceInit);
	}

	/// A texture's UV region (0..1, v down) into a screen rect, tinted (linear), rotated about its centre.
	void Texture(UObject* Object, float X, float Y, float W, float H, float U0, float V0, float U1, float V1,
		float R, float G, float B, float A, float Rotation)
	{
		UTexture* Tex = Cast<UTexture>(Object);
		if (Tex == nullptr) return;
		FCmd& C = Building.AddDefaulted_GetRef();
		C.Kind = ECmd::Texture;
		C.Texture = Tex;
		C.Rect = FBox2f(FVector2f(X, Y), FVector2f(X + W, Y + H));
		C.Uv = FBox2f(FVector2f(U0, V0), FVector2f(U1, V1));
		C.Color = FLinearColor(R, G, B, A);
		C.Rotation = Rotation;
	}

	/// One line of text, top-left at (X, Y), W x H its measured box, in Unity size / style, coloured (linear).
	void Text(const TCHAR* Chars, int32 Length, float X, float Y, float W, float H, int32 Size, int32 Style,
		float R, float G, float B, float A)
	{
		if (Chars == nullptr || Length <= 0) return;
		FCmd& C = Building.AddDefaulted_GetRef();
		C.Kind = ECmd::Text;
		C.Text = FString::ConstructFromPtrSize(Chars, Length);
		C.Rect = FBox2f(FVector2f(X, Y), FVector2f(X + W + 2, Y + H)); // GuiHost's +2: a glyph's overhang is not cut
		C.Size = Size;
		C.Style = Style;
		C.Color = FLinearColor(R, G, B, A);
	}

	/// Width in pixels of one line in Unity size / style (Slate's measure, as the C++ track's).
	float Measure(const TCHAR* Chars, int32 Length, int32 Size, int32 Style)
	{
		if (Chars == nullptr || Length <= 0 || !FSlateApplication::IsInitialized()) return 0;
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return static_cast<float>(M->Measure(FString::ConstructFromPtrSize(Chars, Length), FontFor(Size, Style), 1.0f).X);
	}

	/// The height of a line in Unity size / style (Slate's max character height, as the C++ track's).
	float LineHeight(int32 Size, int32 Style)
	{
		if (!FSlateApplication::IsInitialized()) return FMath::CeilToFloat(Size * 1.15f);
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return static_cast<float>(M->GetMaxCharacterHeight(FontFor(Size, Style), 1.0f));
	}

	/// Replaces a UTexture2D's mips with RGBA pixels (Unity's Color32 rows, bottom row first): mip 0, and with MipChain
	/// the box-filtered levels down to 1x1 (Unity's chain); Filter 0 point, 1 bilinear, 2 trilinear; Wrap 0 repeat,
	/// 1 clamp, 2 mirror; Linear: not sRGB. Returns 1 when done.
	int32 UploadTexture(UObject* Object, const uint8* Rgba, int32 Width, int32 Height, int32 MipChain, int32 Filter, int32 Wrap, int32 Linear)
	{
		UTexture2D* Tex = Cast<UTexture2D>(Object);
		if (Tex == nullptr || Rgba == nullptr || Width <= 0 || Height <= 0) return 0;
		FTexturePlatformData* Data = Tex->GetPlatformData();
		if (Data == nullptr || Data->Mips.Num() == 0) return 0;

		Tex->SRGB = Linear == 0;
		Tex->Filter = Filter == 0 ? TF_Nearest : Filter == 2 ? TF_Trilinear : TF_Bilinear;
		const TextureAddress Address = Wrap == 1 ? TA_Clamp : Wrap == 2 ? TA_Mirror : TA_Wrap;
		Tex->AddressX = Address;
		Tex->AddressY = Address;
		Tex->LODGroup = TEXTUREGROUP_UI;
		Tex->NeverStream = true;

		// Mip 0, top row first, BGRA.
		TArray<uint8> Level;
		Level.SetNumUninitialized(Width * Height * 4);
		for (int32 y = 0; y < Height; y++)
			for (int32 x = 0; x < Width; x++)
			{
				const uint8* S = Rgba + (static_cast<int64>(Height - 1 - y) * Width + x) * 4;
				uint8* P = &Level[(static_cast<int64>(y) * Width + x) * 4];
				P[0] = S[2];
				P[1] = S[1];
				P[2] = S[0];
				P[3] = S[3];
			}
		Data->SizeX = Width;
		Data->SizeY = Height;
		Data->PixelFormat = PF_B8G8R8A8;
		while (Data->Mips.Num() > 1) Data->Mips.RemoveAt(Data->Mips.Num() - 1);
		Data->Mips[0].SizeX = Width;
		Data->Mips[0].SizeY = Height;
		WriteMip(Data->Mips[0], Level);

		if (MipChain)
		{
			int32 W = Width, H = Height;
			while (W > 1 || H > 1)
			{
				const int32 NW = W > 1 ? W / 2 : 1, NH = H > 1 ? H / 2 : 1;
				TArray<uint8> NextLevel;
				NextLevel.SetNumUninitialized(NW * NH * 4);
				for (int32 y = 0; y < NH; y++)
					for (int32 x = 0; x < NW; x++)
						for (int32 Ch = 0; Ch < 4; Ch++)
						{
							int32 Sum = 0;
							for (int32 Dy = 0; Dy < 2; Dy++)
								for (int32 Dx = 0; Dx < 2; Dx++)
								{
									const int32 Sx = FMath::Min(x * 2 + Dx, W - 1), Sy = FMath::Min(y * 2 + Dy, H - 1);
									Sum += Level[(static_cast<int64>(Sy) * W + Sx) * 4 + Ch];
								}
							NextLevel[(static_cast<int64>(y) * NW + x) * 4 + Ch] = static_cast<uint8>((Sum + 2) / 4);
						}
				FTexture2DMipMap* Mip = new FTexture2DMipMap(NW, NH, 1);
				Data->Mips.Add(Mip);
				WriteMip(*Mip, NextLevel);
				Level = MoveTemp(NextLevel);
				W = NW;
				H = NH;
			}
		}
		Tex->UpdateResource();
		return 1;
	}

	BIND_UNREALSHARP_FUNCTION(Begin)
	BIND_UNREALSHARP_FUNCTION(End)
	BIND_UNREALSHARP_FUNCTION(Clip)
	BIND_UNREALSHARP_FUNCTION(Texture)
	BIND_UNREALSHARP_FUNCTION(Text)
	BIND_UNREALSHARP_FUNCTION(Measure)
	BIND_UNREALSHARP_FUNCTION(LineHeight)
	BIND_UNREALSHARP_FUNCTION(UploadTexture)
}
