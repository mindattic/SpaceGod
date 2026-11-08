#include "UnityCompat/GuiHost.h"
#include "UnityCompat/GuiPadNav.h" // US-T250

#include "UnityCompat/UnityRuntime.h"
#include "UI/UiInput.h"
#include "Smoke/SpaceGodRace.h"
#include "SpaceGodLog.h"

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "TextureResource.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SLeafWidget.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SpaceGod::UnityCompat
{
	namespace
	{
		FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

		/// Unity colours are display (sRGB) values; Slate's are linear and converted back on output.
		FLinearColor ToLinear(Color c)
		{
			auto lin = [](float v)
			{
				v = v < 0 ? 0 : (v > 1 ? 1 : v);
				return v <= 0.04045f ? v / 12.92f : FMath::Pow((v + 0.055f) / 1.055f, 2.4f);
			};
			return FLinearColor(lin(c.r), lin(c.g), lin(c.b), c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a));
		}

		// ---------------------------------------------------------------- KeyCode from Unreal's keys

		KeyCode ToKeyCode(const FKey& key)
		{
			static const TMap<FKey, KeyCode> map = []
			{
				TMap<FKey, KeyCode> m;
				const FKey letters[26] = { EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I,
					EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T,
					EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z };
				for (int i = 0; i < 26; i++) m.Add(letters[i], static_cast<KeyCode>(static_cast<int>(KeyCode::A) + i));
				const FKey digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six,
					EKeys::Seven, EKeys::Eight, EKeys::Nine };
				for (int i = 0; i < 10; i++) m.Add(digits[i], static_cast<KeyCode>(static_cast<int>(KeyCode::Alpha0) + i));
				const FKey pad[10] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
					EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
				for (int i = 0; i < 10; i++) m.Add(pad[i], static_cast<KeyCode>(static_cast<int>(KeyCode::Keypad0) + i));
				const FKey fkeys[12] = { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8,
					EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12 };
				for (int i = 0; i < 12; i++) m.Add(fkeys[i], static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + i));
				m.Add(EKeys::BackSpace, KeyCode::Backspace);
				m.Add(EKeys::Tab, KeyCode::Tab);
				m.Add(EKeys::Enter, KeyCode::Return);
				m.Add(EKeys::Pause, KeyCode::Pause);
				m.Add(EKeys::Escape, KeyCode::Escape);
				m.Add(EKeys::SpaceBar, KeyCode::Space);
				m.Add(EKeys::Apostrophe, KeyCode::Quote);
				m.Add(EKeys::Comma, KeyCode::Comma);
				m.Add(EKeys::Hyphen, KeyCode::Minus);
				m.Add(EKeys::Period, KeyCode::Period);
				m.Add(EKeys::Slash, KeyCode::Slash);
				m.Add(EKeys::Semicolon, KeyCode::Semicolon);
				m.Add(EKeys::Equals, KeyCode::Equals);
				m.Add(EKeys::LeftBracket, KeyCode::LeftBracket);
				m.Add(EKeys::Backslash, KeyCode::Backslash);
				m.Add(EKeys::RightBracket, KeyCode::RightBracket);
				m.Add(EKeys::Tilde, KeyCode::BackQuote);
				m.Add(EKeys::Delete, KeyCode::Delete);
				m.Add(EKeys::Decimal, KeyCode::KeypadPeriod);
				m.Add(EKeys::Divide, KeyCode::KeypadDivide);
				m.Add(EKeys::Multiply, KeyCode::KeypadMultiply);
				m.Add(EKeys::Subtract, KeyCode::KeypadMinus);
				m.Add(EKeys::Add, KeyCode::KeypadPlus);
				m.Add(EKeys::Up, KeyCode::UpArrow);
				m.Add(EKeys::Down, KeyCode::DownArrow);
				m.Add(EKeys::Right, KeyCode::RightArrow);
				m.Add(EKeys::Left, KeyCode::LeftArrow);
				m.Add(EKeys::Insert, KeyCode::Insert);
				m.Add(EKeys::Home, KeyCode::Home);
				m.Add(EKeys::End, KeyCode::End);
				m.Add(EKeys::PageUp, KeyCode::PageUp);
				m.Add(EKeys::PageDown, KeyCode::PageDown);
				m.Add(EKeys::NumLock, KeyCode::Numlock);
				m.Add(EKeys::CapsLock, KeyCode::CapsLock);
				m.Add(EKeys::ScrollLock, KeyCode::ScrollLock);
				m.Add(EKeys::RightShift, KeyCode::RightShift);
				m.Add(EKeys::LeftShift, KeyCode::LeftShift);
				m.Add(EKeys::RightControl, KeyCode::RightControl);
				m.Add(EKeys::LeftControl, KeyCode::LeftControl);
				m.Add(EKeys::RightAlt, KeyCode::RightAlt);
				m.Add(EKeys::LeftAlt, KeyCode::LeftAlt);
				return m;
			}();
			const KeyCode* k = map.Find(key);
			return k ? *k : KeyCode::None;
		}

		/// The character a key types (a US layout; Slate's pre-processors see key presses, not WM_CHAR): letters with
		/// shift / caps lock, digits and punctuation with shift. 0 for keys that type nothing.
		char32_t TypedCharacter(KeyCode k, bool shift, bool caps)
		{
			int c = static_cast<int>(k);
			if (c >= static_cast<int>(KeyCode::A) && c <= static_cast<int>(KeyCode::Z))
				return static_cast<char32_t>((shift != caps) ? c - 32 : c);
			if (c >= static_cast<int>(KeyCode::Keypad0) && c <= static_cast<int>(KeyCode::Keypad9))
				return static_cast<char32_t>('0' + (c - static_cast<int>(KeyCode::Keypad0)));
			static const char* shiftedDigits = ")!@#$%^&*(";
			if (c >= static_cast<int>(KeyCode::Alpha0) && c <= static_cast<int>(KeyCode::Alpha9))
				return static_cast<char32_t>(shift ? shiftedDigits[c - static_cast<int>(KeyCode::Alpha0)] : c);
			switch (k)
			{
				case KeyCode::Space: return U' ';
				case KeyCode::Quote: return shift ? U'"' : U'\'';
				case KeyCode::Comma: return shift ? U'<' : U',';
				case KeyCode::Minus: return shift ? U'_' : U'-';
				case KeyCode::Period: return shift ? U'>' : U'.';
				case KeyCode::Slash: return shift ? U'?' : U'/';
				case KeyCode::Semicolon: return shift ? U':' : U';';
				case KeyCode::Equals: return shift ? U'+' : U'=';
				case KeyCode::LeftBracket: return shift ? U'{' : U'[';
				case KeyCode::Backslash: return shift ? U'|' : U'\\';
				case KeyCode::RightBracket: return shift ? U'}' : U']';
				case KeyCode::BackQuote: return shift ? U'~' : U'`';
				case KeyCode::KeypadPeriod: return U'.';
				case KeyCode::KeypadDivide: return U'/';
				case KeyCode::KeypadMultiply: return U'*';
				case KeyCode::KeypadMinus: return U'-';
				case KeyCode::KeypadPlus: return U'+';
				case KeyCode::Return: return U'\n';
				default: return 0;
			}
		}

		int ButtonOf(const FKey& key)
		{
			if (key == EKeys::LeftMouseButton) return 0;
			if (key == EKeys::RightMouseButton) return 1;
			if (key == EKeys::MiddleMouseButton) return 2;
			if (key == EKeys::ThumbMouseButton) return 3;
			if (key == EKeys::ThumbMouseButton2) return 4;
			return -1;
		}

		// ---------------------------------------------------------------- input

		/// An input event as it came in: screen-space (absolute) mouse position, converted at paint time.
		struct PendingEvent
		{
			Event e;
			FVector2f absMouse = FVector2f::ZeroVector;
		};

		struct InputState
		{
			std::vector<PendingEvent> queue;
			FVector2f absMouse = FVector2f::ZeroVector;
			bool haveMouse = false;
			bool buttons[5] = {};
			bool shift = false, control = false, alt = false, command = false;

			// UiInput: held keys, and the presses / deltas since the last latch and of the latched frame.
			std::unordered_set<int> held;
			std::unordered_set<int> pressedSince, pressedFrame;
			bool mousePressedSince[5] = {}, mousePressedFrame[5] = {};
			FVector2f deltaSince = FVector2f::ZeroVector, deltaFrame = FVector2f::ZeroVector;
			float wheelSince = 0, wheelFrame = 0;
			uint64 latchedFrame = ~0ull;

			/// Pixels per Slate absolute unit / the overlay's absolute top-left (from the last paint).
			FVector2f overlayAbsTopLeft = FVector2f::ZeroVector;
			float absToPixel = 1;
			bool present = false;
		};
		InputState input;

		void FillModifiers(Event& e, const FModifierKeysState& m)
		{
			e.shift = m.IsShiftDown();
			e.control = m.IsControlDown();
			e.alt = m.IsAltDown();
			e.command = m.IsCommandDown();
			int mods = 0;
			if (e.shift) mods |= static_cast<int>(EventModifiers::Shift);
			if (e.control) mods |= static_cast<int>(EventModifiers::Control);
			if (e.alt) mods |= static_cast<int>(EventModifiers::Alt);
			if (e.command) mods |= static_cast<int>(EventModifiers::Command);
			if (m.AreCapsLocked()) mods |= static_cast<int>(EventModifiers::CapsLock);
			e.modifiers = static_cast<EventModifiers>(mods);
		}

		void Enqueue(const Event& e, FVector2f absMouse)
		{
			// Consecutive moves (or drags) merge into one, their deltas summed: one pass per frame and kind is enough.
			if ((e.type == EventType::MouseMove || e.type == EventType::MouseDrag) && !input.queue.empty())
			{
				PendingEvent& last = input.queue.back();
				if (last.e.type == e.type && last.e.button == e.button)
				{
					last.e.delta += e.delta;
					last.absMouse = absMouse;
					return;
				}
			}
			PendingEvent p;
			p.e = e;
			p.absMouse = absMouse;
			input.queue.push_back(p);
			// A frame that stalls must not grow the queue without end.
			if (input.queue.size() > 256) input.queue.erase(input.queue.begin());
		}

		class FGuiInputProcessor final : public IInputProcessor
		{
		public:
			void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

			bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& k) override
			{
				KeyCode code = ToKeyCode(k.GetKey());
				if (code != KeyCode::None)
				{
					if (!k.IsRepeat()) input.pressedSince.insert(static_cast<int>(code));
					input.held.insert(static_cast<int>(code));
				}
				Event e;
				e.type = e.rawType = EventType::KeyDown;
				e.keyCode = code;
				FillModifiers(e, k.GetModifierKeys());
				// One KeyDown carrying the key and the character it types, as the C# track's host sends it (US-T28).
				char32_t ch = TypedCharacter(code, k.IsShiftDown(), k.AreCapsLocked());
				if (ch != 0 && !k.IsControlDown() && !k.IsAltDown()) e.character = ch;
				Enqueue(e, input.absMouse);
				return false;
			}

			bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& k) override
			{
				KeyCode code = ToKeyCode(k.GetKey());
				input.held.erase(static_cast<int>(code));
				Event e;
				e.type = e.rawType = EventType::KeyUp;
				e.keyCode = code;
				FillModifiers(e, k.GetModifierKeys());
				Enqueue(e, input.absMouse);
				return false;
			}

			bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& m) override
			{
				input.absMouse = FVector2f(m.GetScreenSpacePosition());
				input.haveMouse = true;
				FVector2f d(m.GetCursorDelta());
				input.deltaSince += d;
				int held = -1;
				for (int b = 0; b < 3; b++)
					if (input.buttons[b]) { held = b; break; }
				Event e;
				e.type = e.rawType = held >= 0 ? EventType::MouseDrag : EventType::MouseMove;
				e.button = held >= 0 ? held : 0;
				e.delta = Vector2(d.X * input.absToPixel, d.Y * input.absToPixel);
				FillModifiers(e, m.GetModifierKeys());
				Enqueue(e, input.absMouse);
				return false;
			}

			bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& m) override
			{
				return Button(m, true, 1);
			}

			bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& m) override
			{
				return Button(m, false, 1);
			}

			bool HandleMouseButtonDoubleClickEvent(FSlateApplication&, const FPointerEvent& m) override
			{
				// Unity reports the second press of a double click as a MouseDown with clickCount 2.
				return Button(m, true, 2);
			}

			bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& w, const FPointerEvent* gesture) override
			{
				if (gesture != nullptr && gesture->GetGestureType() != EGestureEvent::None) return false;
				input.absMouse = FVector2f(w.GetScreenSpacePosition());
				float notches = w.GetWheelDelta();
				input.wheelSince += notches;
				Event e;
				e.type = e.rawType = EventType::ScrollWheel;
				// IMGUI: 3 lines a notch, positive when the wheel rolls towards the player.
				e.delta = Vector2(0, -notches * 3.0f);
				FillModifiers(e, w.GetModifierKeys());
				Enqueue(e, input.absMouse);
				return false;
			}

			const TCHAR* GetDebugName() const override { return TEXT("SpaceGodGui"); }

		private:
			static bool Button(const FPointerEvent& m, bool down, int clicks)
			{
				int b = ButtonOf(m.GetEffectingButton());
				if (b < 0) return false;
				input.absMouse = FVector2f(m.GetScreenSpacePosition());
				input.haveMouse = true;
				input.buttons[b] = down;
				if (down) input.mousePressedSince[b] = true;
				// Unity counts on past two (a triple click selects a text field's line, US-T52): Slate reports the second
				// press as a double click and the third as a plain press, which counts on here when it comes as soon
				// and as near as a double click's second press.
				static double lastDown = -10;
				static FVector2f lastAt = FVector2f::ZeroVector;
				static int count = 0;
				if (down)
				{
					double now = FPlatformTime::Seconds();
					FVector2f at(m.GetScreenSpacePosition());
					if (clicks == 2) count = 2;
					else count = count >= 2 && now - lastDown < 0.5 && FVector2f::Distance(at, lastAt) < 5 ? count + 1 : 1;
					lastDown = now;
					lastAt = at;
				}
				Event e;
				e.type = e.rawType = down ? EventType::MouseDown : EventType::MouseUp;
				e.button = b;
				e.clickCount = count > 0 ? count : clicks;
				FillModifiers(e, m.GetModifierKeys());
				Enqueue(e, input.absMouse);
				return false;
			}
		};

		/// The frame's edges for UiInput, taken once per frame before the world ticks (Update runs then).
		void Latch()
		{
			if (input.latchedFrame == GFrameCounter) return;
			input.latchedFrame = GFrameCounter;
			input.pressedFrame = std::move(input.pressedSince);
			input.pressedSince.clear();
			for (int b = 0; b < 5; b++)
			{
				input.mousePressedFrame[b] = input.mousePressedSince[b];
				input.mousePressedSince[b] = false;
			}
			input.deltaFrame = input.deltaSince;
			input.deltaSince = FVector2f::ZeroVector;
			input.wheelFrame = input.wheelSince;
			input.wheelSince = 0;
		}

		// ---------------------------------------------------------------- textures

		struct NativeEntry
		{
			UTexture2D* texture = nullptr;
			TSharedPtr<FSlateBrush> brush;
		};
		// Never destroyed: static Texture2Ds (icons, the screens' styles) release into these from their own static
		// destructors at exit, which may run after this file's statics would have been (that crashed the windowed run).
		std::unordered_map<void*, NativeEntry>& natives = *new std::unordered_map<void*, NativeEntry>();
		/// Brushes for textures wrapped from elsewhere (not owned), by UTexture.
		std::unordered_map<void*, TSharedPtr<FSlateBrush>>& wrappedBrushes = *new std::unordered_map<void*, TSharedPtr<FSlateBrush>>();

		void Upload(Texture2D& t)
		{
			if (t.width <= 0 || t.height <= 0) return;
			UTexture2D* tex = static_cast<UTexture2D*>(t.Native);
			bool made = false;
			if (tex == nullptr)
			{
				tex = UTexture2D::CreateTransient(t.width, t.height, PF_B8G8R8A8, NAME_None);
				if (tex == nullptr) return;
				tex->AddToRoot(); // held by a C++ object, not a UPROPERTY
				tex->NeverStream = true;
				made = true;
			}
			tex->SRGB = !t.linear;
			tex->Filter = t.filterMode == FilterMode::Point ? TF_Nearest : (t.filterMode == FilterMode::Trilinear ? TF_Trilinear : TF_Bilinear);
			TextureAddress address = t.wrapMode == TextureWrapMode::Clamp ? TA_Clamp : (t.wrapMode == TextureWrapMode::Repeat ? TA_Wrap : TA_Mirror);
			tex->AddressX = address;
			tex->AddressY = address;
			tex->LODGroup = TEXTUREGROUP_UI;

			FTexturePlatformData* data = tex->GetPlatformData();
			if (data == nullptr || data->Mips.Num() == 0) return;

			// Mip 0, top row first (Unity's rows are bottom first), BGRA.
			auto writeMip = [](FTexture2DMipMap& mip, const std::vector<uint8>& bgra)
			{
				void* dst = mip.BulkData.Lock(LOCK_READ_WRITE);
				dst = mip.BulkData.Realloc(static_cast<int64>(bgra.size()));
				FMemory::Memcpy(dst, bgra.data(), bgra.size());
				mip.BulkData.Unlock();
			};
			int w = t.width, h = t.height;
			std::vector<uint8> level(static_cast<size_t>(w) * h * 4);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					const Color32& c = t.pixels[(h - 1 - y) * w + x];
					uint8* p = &level[(static_cast<size_t>(y) * w + x) * 4];
					p[0] = c.b;
					p[1] = c.g;
					p[2] = c.r;
					p[3] = c.a;
				}
			while (data->Mips.Num() > 1) data->Mips.RemoveAt(data->Mips.Num() - 1);
			writeMip(data->Mips[0], level);

			// Unity's mip chain (Icons ask for one, sampled trilinear): box-filtered levels down to 1x1.
			if (t.mipChain && made)
			{
				int lw = w, lh = h;
				while (lw > 1 || lh > 1)
				{
					int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
					std::vector<uint8> next(static_cast<size_t>(nw) * nh * 4);
					for (int y = 0; y < nh; y++)
						for (int x = 0; x < nw; x++)
							for (int ch = 0; ch < 4; ch++)
							{
								int sum = 0, n = 0;
								for (int dy = 0; dy < 2; dy++)
									for (int dx = 0; dx < 2; dx++)
									{
										int sx = FMath::Min(x * 2 + dx, lw - 1), sy = FMath::Min(y * 2 + dy, lh - 1);
										sum += level[(static_cast<size_t>(sy) * lw + sx) * 4 + ch];
										n++;
									}
								next[(static_cast<size_t>(y) * nw + x) * 4 + ch] = static_cast<uint8>((sum + n / 2) / n);
							}
					FTexture2DMipMap* mip = new FTexture2DMipMap(nw, nh, 1);
					data->Mips.Add(mip);
					writeMip(*mip, next);
					level = std::move(next);
					lw = nw;
					lh = nh;
				}
			}
			tex->UpdateResource();
			t.Native = tex;
			t.OwnsNative = true;
			t.Dirty = false;
			if (made)
			{
				NativeEntry entry;
				entry.texture = tex;
				natives[tex] = entry;
			}
			else
			{
				// Same UTexture, new pixels: the brush stays valid.
			}
		}

		const FSlateBrush* BrushFor(const Texture* texture)
		{
			if (texture == nullptr) return nullptr;
			if (auto* t2 = dynamic_cast<const Texture2D*>(texture))
			{
				auto* t = const_cast<Texture2D*>(t2);
				if (t->Dirty && (t->Native == nullptr || t->OwnsNative)) Upload(*t);
			}
			void* native = texture->Native;
			if (native == nullptr) return nullptr;
			TSharedPtr<FSlateBrush>* slot = nullptr;
			auto it = natives.find(native);
			if (it != natives.end()) slot = &it->second.brush;
			else slot = &wrappedBrushes[native];
			if (!slot->IsValid())
			{
				TSharedPtr<FSlateBrush> b = MakeShared<FSlateBrush>();
				b->SetResourceObject(static_cast<UObject*>(native));
				b->ImageSize = FVector2f(static_cast<float>(FMath::Max(texture->width, 1)), static_cast<float>(FMath::Max(texture->height, 1)));
				b->DrawAs = ESlateBrushDrawType::Image;
				b->Tiling = ESlateBrushTileType::NoTile;
				*slot = b;
			}
			return slot->Get();
		}

		// ---------------------------------------------------------------- fonts

		/// Arial from the system font folder (what Unity's built-in font is on Windows, and what the C# track draws with,
		/// US-T28), its four faces as one composite font; null where Arial is not installed (then Slate's Roboto).
		TSharedPtr<const FCompositeFont> Arial()
		{
			static TSharedPtr<FStandaloneCompositeFont> font;
			static bool tried = false;
			if (tried) return font;
			tried = true;
			FString dir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
			if (dir.IsEmpty()) return font;
			dir = FPaths::Combine(dir, TEXT("Fonts"));
			const TCHAR* faces[4] = { TEXT("Regular"), TEXT("Bold"), TEXT("Italic"), TEXT("BoldItalic") };
			const TCHAR* files[4] = { TEXT("arial.ttf"), TEXT("arialbd.ttf"), TEXT("ariali.ttf"), TEXT("arialbi.ttf") };
			FString regular = FPaths::Combine(dir, files[0]);
			if (!FPaths::FileExists(regular)) return font;
			font = MakeShared<FStandaloneCompositeFont>();
			for (int i = 0; i < 4; i++)
			{
				FString path = FPaths::Combine(dir, files[i]);
				if (!FPaths::FileExists(path)) path = regular;
				font->DefaultTypeface.AppendFont(faces[i], path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			}
			UE_LOG(LogSpaceGod, Display, TEXT("GUI font: %s"), *regular);
			return font;
		}

		FSlateFontInfo FontFor(int fontSize, FontStyle style)
		{
			const TCHAR* face = TEXT("Regular");
			switch (style)
			{
				case FontStyle::Bold: face = TEXT("Bold"); break;
				case FontStyle::Italic: face = TEXT("Italic"); break;
				case FontStyle::BoldAndItalic: face = TEXT("BoldItalic"); break;
				default: break;
			}
			// Unity's IMGUI size is the em in pixels; Slate's is points at 96 dpi (em = size * 96 / 72 px).
			float size = static_cast<float>(fontSize) * 0.75f;
			if (TSharedPtr<const FCompositeFont> arial = Arial()) return FSlateFontInfo(arial, size, FName(face));
			return FCoreStyle::GetDefaultFontStyle(face, size);
		}

		// ---------------------------------------------------------------- the Slate renderer

		class FSlateGuiRenderer final : public IGuiRenderer
		{
		public:
			FSlateWindowElementList* elements = nullptr;
			const FGeometry* pixels = nullptr; // a child geometry whose local unit is one pixel
			int32 layer = 0;
			const void* lastKey = nullptr;
			int clipDepth = 0;

			Vector2 MeasureLine(const cs::String& text, int fontSize, FontStyle style) override
			{
				auto key = std::make_tuple(text, fontSize, static_cast<int>(style));
				auto it = widthCache.find(key);
				if (it != widthCache.end()) return Vector2(it->second, LineHeight(fontSize, style));
				float w = 0;
				if (FSlateApplication::IsInitialized())
				{
					TSharedRef<FSlateFontMeasure> measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
					w = static_cast<float>(measure->Measure(ToF(text), FontFor(fontSize, style), 1.0f).X);
				}
				else w = static_cast<float>(text.size()) * fontSize * 0.5f;
				if (widthCache.size() > 20000) widthCache.clear();
				widthCache[key] = w;
				return Vector2(w, LineHeight(fontSize, style));
			}

			float LineHeight(int fontSize, FontStyle style) override
			{
				int key = fontSize * 4 + static_cast<int>(style);
				auto it = heightCache.find(key);
				if (it != heightCache.end()) return it->second;
				float h = static_cast<float>(fontSize) * 1.15f;
				if (FSlateApplication::IsInitialized())
				{
					TSharedRef<FSlateFontMeasure> measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
					h = static_cast<float>(measure->GetMaxCharacterHeight(FontFor(fontSize, style), 1.0f));
				}
				heightCache[key] = h;
				return h;
			}

			void DrawTexture(const Rect& screen, const Texture* texture, const Rect& uv, Color tint, const RectOffset& border, const GuiMatrix& matrix) override
			{
				if (texture == nullptr || tint.a <= 0 || screen.width <= 0 || screen.height <= 0) return;
				GUI::Stats().Textures++;
				if (!elements || !pixels) return;
				const FSlateBrush* source = BrushFor(texture);
				if (!source) return;
				// A per-draw copy: the UV region and the 9-slice margin differ between draws of one texture.
				FSlateBrush brush = *source;
				bool slice = border.left > 0 || border.right > 0 || border.top > 0 || border.bottom > 0;
				if (slice)
				{
					float tw = static_cast<float>(FMath::Max(texture->width, 1)), th = static_cast<float>(FMath::Max(texture->height, 1));
					brush.DrawAs = ESlateBrushDrawType::Box;
					brush.Margin = FMargin(border.left / tw, border.top / th, border.right / tw, border.bottom / th);
				}
				else if (uv.x != 0 || uv.y != 0 || uv.width != 1 || uv.height != 1)
				{
					// Unity's uv rect is from the bottom; Slate's from the top.
					float v0 = 1.0f - (uv.y + uv.height);
					brush.SetUVRegion(FBox2f(FVector2f(uv.x, v0), FVector2f(uv.x + uv.width, v0 + uv.height)));
				}
				Next(texture);
				FSlateDrawElement::MakeBox(*elements, layer, Paint(screen, matrix), &brush, ESlateDrawEffect::None, ToLinear(tint));
			}

			void DrawText(Vector2 topLeft, const cs::String& text, int fontSize, FontStyle style, Color color, const GuiMatrix& matrix) override
			{
				if (text.empty()) return;
				GUI::Stats().Texts++;
				if (!elements || !pixels) return;
				Vector2 size = MeasureLine(text, fontSize, style);
				Next(&textKey);
				FSlateDrawElement::MakeText(*elements, layer, Paint(Rect(topLeft.x, topLeft.y, size.x + 2, size.y), matrix), ToF(text),
					FontFor(fontSize, style), ESlateDrawEffect::None, ToLinear(color));
			}

			void PushClip(const Rect& screen) override
			{
				if (!elements || !pixels) return;
				elements->PushClip(FSlateClippingZone(Paint(screen, GuiMatrix::identity())));
				clipDepth++;
				lastKey = nullptr;
			}

			void PopClip() override
			{
				if (!elements || clipDepth == 0) return;
				elements->PopClip();
				clipDepth--;
				lastKey = nullptr;
			}

			void Begin(FSlateWindowElementList& list, const FGeometry& px, int32 firstLayer)
			{
				elements = &list;
				pixels = &px;
				layer = firstLayer;
				lastKey = nullptr;
				clipDepth = 0;
			}

			int32 End()
			{
				while (clipDepth > 0) PopClip();
				elements = nullptr;
				pixels = nullptr;
				return layer;
			}

		private:
			int textKey = 0;
			std::map<std::tuple<cs::String, int, int>, float> widthCache;
			std::unordered_map<int, float> heightCache;

			/// Draw order is the GUI's call order: a new layer whenever the kind of element changes (Slate batches
			/// within a layer, which could otherwise put a later box under an earlier label).
			void Next(const void* key)
			{
				if (key != lastKey) layer++;
				lastKey = key;
			}

			FPaintGeometry Paint(const Rect& r, const GuiMatrix& m) const
			{
				FVector2f size(r.width, r.height);
				FVector2f pos(r.x, r.y);
				if (m.isIdentity()) return pixels->ToPaintGeometry(size, FSlateLayoutTransform(pos));
				// screen = M (pos + q): as a render transform about the child's origin, R(q) = A q + (A pos + t - pos).
				// FMatrix2x2 multiplies row vectors, hence the transpose.
				FMatrix2x2f a(m.m00, m.m10, m.m01, m.m11);
				FVector2f t(m.m00 * pos.X + m.m01 * pos.Y + m.m02 - pos.X, m.m10 * pos.X + m.m11 * pos.Y + m.m12 - pos.Y);
				return pixels->ToPaintGeometry(size, FSlateLayoutTransform(pos), FSlateRenderTransform(a, t), FVector2f(0, 0));
			}
		};

		FSlateGuiRenderer slateRenderer;

		// ---------------------------------------------------------------- handlers and the frame

		struct Handler
		{
			const void* owner = nullptr;
			std::function<void()> onGui;
			int order = 0;
			uint64 serial = 0;
		};
		std::vector<Handler> handlers;
		uint64 nextSerial = 0;
		bool inFrame = false;

		Vector2 ToPixels(FVector2f abs)
		{
			FVector2f p = (abs - input.overlayAbsTopLeft) * input.absToPixel;
			return Vector2(p.X, p.Y);
		}

		void RunPass(Event& e)
		{
			// A copy: a handler may register or unregister another (a screen opening the model viewer).
			std::vector<Handler> now = handlers;
			for (size_t i = 0; i < now.size(); i++)
			{
				if (!now[i].onGui) continue;
				try
				{
					GUI::RunOnGUI(now[i].onGui, e, static_cast<int>(i));
				}
				catch (const std::exception& ex)
				{
					// Unity logs an exception in OnGUI and carries on with the next frame.
					UE_LOG(LogSpaceGod, Error, TEXT("OnGUI threw: %s"), UTF8_TO_TCHAR(ex.what()));
				}
				GUI::Stats().Passes++;
			}
		}

		int32 RunFrame(const FPaintArgs& args, const FGeometry& allotted, FSlateWindowElementList& out, int32 layerId)
		{
			if (inFrame) return layerId; // a nested paint (should not happen) draws nothing
			inFrame = true;
			const double raceT0 = Race::Probe::Now(); // US-T46: the IMGUI pass, for the race (docs/RACE.md)
			float scale = allotted.Scale > 0 ? allotted.Scale : 1.0f;
			FVector2f localSize = allotted.GetLocalSize();
			int pw = FMath::RoundToInt(localSize.X * scale), ph = FMath::RoundToInt(localSize.Y * scale);
			// US-T252: the Accessibility UI scale. The GUI is laid out on a screen that many times smaller (Screen::width and
			// height read in GUI units while the passes run, the pointer divided to match) and drawn that many times larger
			// by the child geometry; outside the passes Screen keeps the real pixels the camera and the game's input use.
			float ui = FMath::Clamp(GUI::uiScale > 0 ? GUI::uiScale : 1.0f, 0.5f, 3.0f);
			int gw = FMath::Max(1, FMath::RoundToInt(pw / ui)), gh = FMath::Max(1, FMath::RoundToInt(ph / ui));
			if (pw > 0 && ph > 0)
			{
				Screen::width = gw;
				Screen::height = gh;
			}
			GUI::passScale = GUI::frameScale = ui;
			auto toGui = [ui](Vector2 p) { return Vector2(p.x / ui, p.y / ui); };
			// Paint geometry is window space, the input events' positions desktop space (US-T77: with the window anywhere
			// but the desktop's top-left, every click and hover missed the controls by the window's position).
			input.overlayAbsTopLeft = FVector2f(allotted.GetAbsolutePosition()) + FVector2f(args.GetWindowToDesktopTransform());
			// Absolute (desktop) units to the overlay's pixels: its absolute size over its pixel size.
			FVector2f absSize = FVector2f(allotted.GetAbsoluteSize());
			input.absToPixel = absSize.X > 0 ? static_cast<float>(pw) / absSize.X : 1.0f;
			input.present = true;

			FGeometry px = allotted.MakeChild(FVector2f(static_cast<float>(gw), static_cast<float>(gh)), FSlateLayoutTransform(ui / scale));
			GUI::SetRenderer(&slateRenderer);
			GUI::BeginFrame();
			GUI::Stats() = GUI::FrameStats();

			// US-T250: the pad's events first (GuiPadNav): a press of the focused control, a slider's step, a scroll, Esc (which
			// UiInput hears too, as it would the key).
			{
				std::vector<Event> pad;
				bool padEscape = false;
				GuiPadNav::BeginFrame(pad, input.haveMouse ? toGui(ToPixels(input.absMouse)) : Vector2(-10000, -10000), padEscape);
				if (padEscape) input.pressedSince.insert(static_cast<int>(KeyCode::Escape));
				for (Event& e : pad)
				{
					Event layout;
					layout.type = layout.rawType = EventType::Layout;
					layout.mousePosition = e.mousePosition;
					RunPass(layout);
					RunPass(e);
				}
			}

			std::vector<PendingEvent> events;
			events.swap(input.queue);
			for (PendingEvent& p : events)
			{
				Vector2 mouse = toGui(ToPixels(p.absMouse));
				Event layout;
				layout.type = layout.rawType = EventType::Layout;
				layout.mousePosition = mouse;
				RunPass(layout);
				Event e = p.e;
				e.mousePosition = mouse;
				e.delta = toGui(e.delta);
				RunPass(e);
				// A press released with no control left to take it must not stay hot (its panel may have closed).
				if (p.e.type == EventType::MouseUp) GUIUtility::hotControl = 0;
			}

			Vector2 mouse = input.haveMouse ? toGui(ToPixels(input.absMouse)) : Vector2(-10000, -10000);
			Event layout;
			layout.type = layout.rawType = EventType::Layout;
			layout.mousePosition = mouse;
			RunPass(layout);

			GUI::DrawnButtons().clear();
			slateRenderer.Begin(out, px, layerId);
			Event repaint;
			repaint.type = repaint.rawType = EventType::Repaint;
			repaint.mousePosition = mouse;
			RunPass(repaint);
			int32 last = slateRenderer.End();
			// US-T252: back to the real pixels for everything outside the passes.
			if (pw > 0 && ph > 0)
			{
				Screen::width = pw;
				Screen::height = ph;
			}
			GUI::passScale = 1;
			inFrame = false;
			Race::Probe::GuiMs = Race::Probe::Now() - raceT0; // US-T46
			return last;
		}

		class SGuiOverlay final : public SLeafWidget
		{
		public:
			SLATE_BEGIN_ARGS(SGuiOverlay) {}
			SLATE_END_ARGS()

			void Construct(const FArguments&)
			{
				SetVisibility(EVisibility::HitTestInvisible);
			}

			int32 OnPaint(const FPaintArgs& args, const FGeometry& allotted, const FSlateRect&, FSlateWindowElementList& out,
				int32 layerId, const FWidgetStyle&, bool) const override
			{
				return RunFrame(args, allotted, out, layerId);
			}

			FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
		};

		TSharedPtr<SGuiOverlay> overlay;
		TWeakObjectPtr<UGameViewportClient> installedOn;
		TSharedPtr<FGuiInputProcessor> processor;
		FTSTicker::FDelegateHandle ticker;
		FDelegateHandle worldTickStart;

		/// Puts the overlay on the game viewport (again, after a map load cleared it) and the input processor on Slate.
		bool EnsureInstalled(float)
		{
			if (!FSlateApplication::IsInitialized()) return true;
			if (!processor.IsValid())
			{
				processor = MakeShared<FGuiInputProcessor>();
				FSlateApplication::Get().RegisterInputPreProcessor(processor);
			}
			UGameViewportClient* viewport = GEngine ? GEngine->GameViewport : nullptr;
			if (viewport == nullptr) return true;
			if (!overlay.IsValid()) overlay = SNew(SGuiOverlay);
			bool attached = overlay->GetParentWidget().IsValid();
			if (installedOn.Get() != viewport || !attached)
			{
				if (attached && installedOn.IsValid()) installedOn->RemoveViewportWidgetContent(overlay.ToSharedRef());
				// Above the game's own viewport widgets, as Unity's OnGUI draws over everything.
				viewport->AddViewportWidgetContent(overlay.ToSharedRef(), 10000);
				installedOn = viewport;
			}
			return true;
		}

		void EnsureTicker()
		{
			if (!ticker.IsValid())
			{
				ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&EnsureInstalled), 0.0f);
				EnsureInstalled(0);
			}
			if (!worldTickStart.IsValid())
				worldTickStart = FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld*, ELevelTick, float) { Latch(); });
		}
	}

	void GuiHost::Register(const void* owner, std::function<void()> onGui, int order)
	{
		EnsureTicker();
		for (Handler& h : handlers)
			if (h.owner == owner)
			{
				h.onGui = std::move(onGui);
				h.order = order;
				return;
			}
		Handler h;
		h.owner = owner;
		h.onGui = std::move(onGui);
		h.order = order;
		h.serial = nextSerial++;
		handlers.push_back(std::move(h));
		std::stable_sort(handlers.begin(), handlers.end(), [](const Handler& a, const Handler& b) { return a.order < b.order; });
	}

	void GuiHost::Unregister(const void* owner)
	{
		handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [owner](const Handler& h) { return h.owner == owner; }), handlers.end());
	}

	void ScriptedGuiInput::Clear()
	{
		for (int i = 0; i < 3; i++) pressed[i] = released[i] = false;
		wheelNotches = 0;
		keysDown.clear();
	}

	void GuiHost::ScriptedFrame(ScriptedGuiInput& in, float time)
	{
		// The C# track's ImguiHost.Frame with its GuiInput: this frame's input as IMGUI events -- a move or drag, the
		// presses and releases (Unity's click count), the wheel, the keys -- each a Layout pass then its own; then
		// Layout and Repaint. Nothing is drawn (no element list): the renderer only measures and counts.
		if (inFrame) return;
		inFrame = true;
		static Vector2 lastMouse(NAN, NAN);
		static float lastClickTime = -10;
		static Vector2 lastClickAt;
		static int clickCount = 0;

		GUI::SetRenderer(&slateRenderer);
		GUI::BeginFrame();
		GUI::Stats() = GUI::FrameStats();
		input.present = true;
		auto make = [&in](EventType type)
		{
			Event e;
			e.type = e.rawType = type;
			e.mousePosition = in.mouse;
			// US-T52: the held modifiers (Shift-click, Ctrl+A ...), as the C# track's GuiInput.Modifiers.
			int mods = static_cast<int>(in.modifiers);
			e.modifiers = in.modifiers;
			e.shift = (mods & static_cast<int>(EventModifiers::Shift)) != 0;
			e.control = (mods & static_cast<int>(EventModifiers::Control)) != 0;
			e.alt = (mods & static_cast<int>(EventModifiers::Alt)) != 0;
			e.command = (mods & static_cast<int>(EventModifiers::Command)) != 0;
			return e;
		};
		std::vector<Event> events;
		bool anyHeld = in.held[0] || in.held[1] || in.held[2];
		if (!FMath::IsNaN(lastMouse.x) && (in.mouse.x != lastMouse.x || in.mouse.y != lastMouse.y))
		{
			Event move = make(anyHeld ? EventType::MouseDrag : EventType::MouseMove);
			move.delta = Vector2(in.mouse.x - lastMouse.x, in.mouse.y - lastMouse.y);
			move.button = in.held[0] ? 0 : in.held[1] ? 1 : in.held[2] ? 2 : 0;
			events.push_back(move);
		}
		lastMouse = in.mouse;
		for (int b = 0; b < 3; b++)
		{
			if (in.pressed[b])
			{
				float dx = in.mouse.x - lastClickAt.x, dy = in.mouse.y - lastClickAt.y;
				clickCount = time - lastClickTime < 0.5f && FMath::Sqrt(dx * dx + dy * dy) < 5 ? clickCount + 1 : 1;
				lastClickTime = time;
				lastClickAt = in.mouse;
				Event down = make(EventType::MouseDown);
				down.button = b;
				down.clickCount = clickCount;
				events.push_back(down);
			}
			if (in.released[b])
			{
				Event up = make(EventType::MouseUp);
				up.button = b;
				up.clickCount = clickCount;
				events.push_back(up);
			}
		}
		if (in.wheelNotches != 0)
		{
			Event wheel = make(EventType::ScrollWheel);
			wheel.delta = Vector2(0, -in.wheelNotches * 3.0f);
			events.push_back(wheel);
		}
		for (const auto& k : in.keysDown)
		{
			Event key = make(EventType::KeyDown);
			key.keyCode = k.first;
			key.character = k.second;
			events.push_back(key);
		}

		for (Event& e : events)
		{
			Event layout = make(EventType::Layout);
			RunPass(layout);
			EventType raw = e.type;
			RunPass(e);
			if (raw == EventType::MouseUp) GUIUtility::hotControl = 0;
		}
		Event layout = make(EventType::Layout);
		RunPass(layout);
		GUI::DrawnButtons().clear();
		Event repaint = make(EventType::Repaint);
		RunPass(repaint);
		inFrame = false;
	}

	// ---------------------------------------------------------------- GUIUtility.systemCopyBuffer (US-T52)

	cs::String GUIUtility::systemCopyBuffer()
	{
		// Unity's systemCopyBuffer is the system clipboard (the C# track's Clipboard: the Windows clipboard).
		FString text;
		FPlatformApplicationMisc::ClipboardPaste(text);
		return cs::String(TCHAR_TO_UTF8(*text));
	}

	void GUIUtility::SetSystemCopyBuffer(const cs::String& text)
	{
		FPlatformApplicationMisc::ClipboardCopy(*ToF(text));
	}

	void GuiHost::ReleaseNativeTexture(void* native)
	{
		auto it = natives.find(native);
		if (it == natives.end()) return;
		UTexture2D* tex = it->second.texture;
		natives.erase(it);
		// At exit the object system may already be gone (a static Texture2D outlives it).
		if (tex != nullptr && UObjectInitialized() && !GExitPurge) tex->RemoveFromRoot();
	}

	void GuiHost::Shutdown()
	{
		if (ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(ticker);
		ticker.Reset();
		if (worldTickStart.IsValid()) FWorldDelegates::OnWorldTickStart.Remove(worldTickStart);
		worldTickStart.Reset();
		if (overlay.IsValid() && installedOn.IsValid()) installedOn->RemoveViewportWidgetContent(overlay.ToSharedRef());
		overlay.Reset();
		if (processor.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(processor);
		processor.Reset();
		handlers.clear();
	}
}

// ---------------------------------------------------------------- UI::UiInput

namespace SpaceGod::UI
{
	bool UiInput::present() { return UnityCompat::input.present || FSlateApplication::IsInitialized(); }
	bool UiInput::WasPressedThisFrame(KeyCode key) { return UnityCompat::input.pressedFrame.count(static_cast<int>(key)) > 0; }
	bool UiInput::IsPressed(KeyCode key) { return UnityCompat::input.held.count(static_cast<int>(key)) > 0; }
	bool UiInput::IsMousePressed(int button) { return button >= 0 && button < 5 && UnityCompat::input.buttons[button]; }
	bool UiInput::WasMousePressedThisFrame(int button) { return button >= 0 && button < 5 && UnityCompat::input.mousePressedFrame[button]; }

	UnityCompat::Vector2 UiInput::MouseDelta()
	{
		const auto& in = UnityCompat::input;
		return UnityCompat::Vector2(in.deltaFrame.X * in.absToPixel, -in.deltaFrame.Y * in.absToPixel);
	}

	float UiInput::ScrollY() { return UnityCompat::input.wheelFrame * 120.0f; }

	UnityCompat::Vector2 UiInput::MousePosition()
	{
		const auto& in = UnityCompat::input;
		FVector2f p = (in.absMouse - in.overlayAbsTopLeft) * in.absToPixel;
		return UnityCompat::Vector2(p.X, static_cast<float>(UnityCompat::Screen::height) - p.Y);
	}
}
