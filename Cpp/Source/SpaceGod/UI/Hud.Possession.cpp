// US-T446: the HUD's half of possession (declared in Hud.Main.inl): the show's faint wash over the screen, and the possessed
// view's frame, by what the pilot sees with: a pilot's sensors (thin corner brackets), a person's eyes (a helmet visor's dark
// rim), a station's traffic picture (panoramic bands and a bearing scale). Tunables in Views::PossessionTune.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"
#include "Views/PossessionFx.h"

namespace SpaceGod::UI
{
	using Tune = Views::PossessionTune;

	namespace
	{
		constexpr Color FrameColor{0.62f, 0.9f, 1.0f, 1.0f};

		void Fill(const Rect& r, Color c)
		{
			GUI::color = c;
			GUI::DrawTexture(r, Icons::White());
		}

		float Smooth01(float x)
		{
			x = Mathf::Clamp01(x);
			return x * x * (3 - 2 * x);
		}
	}

	void Hud::DrawPossession()
	{
		if (Event::current().type != EventType::Repaint) return;
		const float sw = static_cast<float>(Screen::width), sh = static_cast<float>(Screen::height);
		auto old = GUI::color;
		auto oldContent = GUI::contentColor;

		// ---- the breath: the faintest wash of the energy's colour as it goes in (or comes back out)
		float wash = app->PossessionWash();
		if (wash > 0.001f) Fill(Rect(0, 0, sw, sh), Color(Tune::Energy.r, Tune::Energy.g, Tune::Energy.b, wash * Tune::WashAlpha));

		// ---- the possessed view's frame, eased in with it
		auto kind = app->Perceiving();
		float a = Smooth01(app->PerceptionShown());
		if (kind == UnitySpaceApp::PerceptionKind::God || a < 0.002f)
		{
			GUI::color = old;
			return;
		}
		cs::String caption;
		double reach = app->PerceivedReachKm();
		switch (kind)
		{
			case UnitySpaceApp::PerceptionKind::Eyes:
			{
				// A helmet's visor: the edge of the view darkens and rounds off; nothing but what the eyes make out.
				if (!visorTexture)
				{
					constexpr int n = 128;
					visorTexture = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
					visorTexture->hideFlags = HideFlags::DontSave;
					visorTexture->wrapMode = TextureWrapMode::Clamp;
					visorTexture->filterMode = FilterMode::Bilinear;
					auto px = cs::NewArray<Color32>(n * n);
					for (int y = 0; y < n; y++)
						for (int x = 0; x < n; x++)
						{
							float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
							// A rounded rectangle's distance (the visor's shape), feathered wide.
							float qx = Mathf::Max(Mathf::Abs(u) - 0.55f, 0.0f), qy = Mathf::Max(Mathf::Abs(v) - 0.42f, 0.0f);
							float d = Mathf::Sqrt(qx * qx + qy * qy);
							float k = Smooth01((d - 0.05f) / 0.55f);
							px[y * n + x] = Color32(255, 255, 255, static_cast<uint8_t>(Mathf::Pow(k, 1.3f) * 255));
						}
					visorTexture->SetPixels32(px);
					visorTexture->Apply();
				}
				GUI::color = Color(0.01f, 0.02f, 0.035f, Tune::VisorAlpha * a);
				GUI::DrawTexture(Rect(0, 0, sw, sh), visorTexture);
				caption = "THROUGH THE VISOR  \xC2\xB7  EYES ONLY";
				break;
			}
			case UnitySpaceApp::PerceptionKind::Panorama:
			{
				// Traffic control's wide picture: soft bands top and bottom, a bearing scale along the lower one.
				if (!bandTexture)
				{
					constexpr int n = 64;
					bandTexture = cs::New<Texture2D>(1, n, TextureFormat::RGBA32, false);
					bandTexture->hideFlags = HideFlags::DontSave;
					bandTexture->wrapMode = TextureWrapMode::Clamp;
					bandTexture->filterMode = FilterMode::Bilinear;
					auto px = cs::NewArray<Color32>(n);
					// Row 0 is the bottom (Unity's convention): opaque at the bottom edge, gone by the top.
					for (int y = 0; y < n; y++) px[y] = Color32(255, 255, 255, static_cast<uint8_t>(Mathf::Pow(1 - (y + 0.5f) / n, 1.6f) * 255));
					bandTexture->SetPixels32(px);
					bandTexture->Apply();
				}
				float band = sh * Tune::PanoramaBand;
				GUI::color = Color(0.02f, 0.06f, 0.09f, Tune::PanoramaAlpha * a);
				GUI::DrawTexture(Rect(0, sh - band, sw, band), bandTexture);
				// The top band: the same gradient drawn upside down.
				GUI::DrawTextureWithTexCoords(Rect(0, 0, sw, band), bandTexture, Rect(0, 1, 1, -1));
				// A hairline along each band's inner edge, so the wide frame reads against black space.
				Fill(Rect(0, band, sw, 1), Color(FrameColor.r, FrameColor.g, FrameColor.b, 0.16f * a));
				Fill(Rect(0, sh - band - 1, sw, 1), Color(FrameColor.r, FrameColor.g, FrameColor.b, 0.16f * a));
				// The bearing scale: where the view looks round the system's plane, ticks every 5 degrees, a longer one every 15.
				auto fwd = app->SpaceCamera->Rotation * Vector3::forward();
				float yaw = Mathf::Atan2(fwd.x, fwd.z) * Mathf::Rad2Deg;
				float halfV = Mathf::Max(app->UnityCamera->fieldOfView, 10.0f) * 0.5f * Mathf::Deg2Rad;
				float tanH = Mathf::Tan(halfV) * sw / Mathf::Max(sh, 1.0f);
				float baseY = sh - band * 0.45f;
				for (int b = 0; b < 360; b += 5)
				{
					float off = Mathf::DeltaAngle(yaw, static_cast<float>(b)) * Mathf::Deg2Rad;
					if (Mathf::Abs(off) > 1.4f) continue;
					float x = sw * 0.5f + Mathf::Tan(off) / tanH * sw * 0.5f;
					if (x < 0 || x > sw) continue;
					bool major = b % 15 == 0;
					float h = major ? 9.0f : 4.0f;
					Fill(Rect(x - 0.5f, baseY - h, 1.0f, h), Color(FrameColor.r, FrameColor.g, FrameColor.b, (major ? 0.6f : 0.35f) * a));
					if (b % 45 == 0)
					{
						GUI::color = Color::white(); // (the tint multiplies a label's colour)
						GUI::contentColor = Color(FrameColor.r, FrameColor.g, FrameColor.b, 0.65f * a);
						GUI::Label(Rect(x - 20, baseY + 1, 40, 16), cs::Cat(b < 10 ? "00" : b < 100 ? "0" : "", b), small);
					}
				}
				caption = cs::Cat("TRAFFIC CONTROL  \xC2\xB7  PANORAMIC", reach > 0 ? cs::Cat("  \xC2\xB7  REACH ", RangeText(reach)) : cs::String());
				break;
			}
			default:
			{
				// A pilot's sensors: thin corner brackets round the view, a tick at the middle of each edge.
				Color c(FrameColor.r, FrameColor.g, FrameColor.b, Tune::BracketAlpha * a);
				float in = Tune::BracketInset, arm = Tune::BracketArm, t = 1.5f;
				for (int sx = 0; sx < 2; sx++)
					for (int sy = 0; sy < 2; sy++)
					{
						float x = sx == 0 ? in : sw - in, y = sy == 0 ? in : sh - in;
						Fill(Rect(sx == 0 ? x : x - arm, y - t * 0.5f, arm, t), c);
						Fill(Rect(x - t * 0.5f, sy == 0 ? y : y - arm, t, arm), c);
					}
				Color tick(c.r, c.g, c.b, c.a * 0.8f);
				Fill(Rect(sw * 0.5f - 0.75f, in - 1, 1.5f, 8), tick);
				Fill(Rect(sw * 0.5f - 0.75f, sh - in - 7, 1.5f, 8), tick);
				Fill(Rect(in - 1, sh * 0.5f - 0.75f, 8, 1.5f), tick);
				Fill(Rect(sw - in - 7, sh * 0.5f - 0.75f, 8, 1.5f), tick);
				caption = cs::Cat("THROUGH ITS SENSORS", reach > 0 ? cs::Cat("  \xC2\xB7  REACH ", RangeText(reach)) : cs::String());
				break;
			}
		}
		// The caption, small and quiet: beside the top-left bracket (in the top band, for the panorama), clear of the banners.
		if (!caption.empty())
		{
			GUI::color = Color::white(); // the frame's fills left their tint on GUI::color, which multiplies a label's colour
			auto content = GUIContent(caption);
			auto size = small->CalcSize(content);
			float x = Tune::BracketInset + (kind == UnitySpaceApp::PerceptionKind::Panorama ? 0.0f : Tune::BracketArm + 10);
			float y = kind == UnitySpaceApp::PerceptionKind::Panorama ? sh * Tune::PanoramaBand - size.y - 4 : Tune::BracketInset - size.y * 0.5f;
			auto r = Rect(x, y, size.x, size.y);
			GUI::contentColor = Color(0, 0, 0, 0.6f * Tune::CaptionAlpha * a);
			GUI::Label(Rect(r.x + 1, r.y + 1, r.width, r.height), content, small);
			GUI::contentColor = Color(FrameColor.r, FrameColor.g, FrameColor.b, Tune::CaptionAlpha * a);
			GUI::Label(r, content, small);
		}
		GUI::color = old;
		GUI::contentColor = oldContent;
	}
}
