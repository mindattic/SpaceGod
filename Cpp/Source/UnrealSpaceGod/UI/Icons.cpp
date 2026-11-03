#include "UI/Icons.h"

#include "BodyDefinition.h"
#include "Ships.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr int Size = 64;

		cs::Ref<Texture2D> ring, dot, star, diamond, triangle, chevron, carrier, square, hexagon, dart, wedge, needle, broad, scout,
			pennant, arrow, badge, shuttle, breaker, white, prograde, retrograde, blip, lost, singularity;

		/// Vector2.Max (component-wise), which UnityMath.h does not have.
		Vector2 Max(Vector2 a, Vector2 b) { return Vector2(Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y)); }

		float Ring(Vector2 p, float radius, float thickness) { return Mathf::Abs(p.magnitude() - radius) - thickness * 0.5f; }
		float Outline(float d, float thickness) { return Mathf::Abs(d) - thickness * 0.5f; }

		float Box(Vector2 p, Vector2 half)
		{
			auto d = Vector2(Mathf::Abs(p.x) - half.x, Mathf::Abs(p.y) - half.y);
			return Max(d, Vector2::zero()).magnitude() + Mathf::Min(Mathf::Max(d.x, d.y), 0.0f);
		}

		float Triangle(Vector2 p, float r)
		{
			// Equilateral, pointing up.
			const float k = 1.7320508f;
			p.x = Mathf::Abs(p.x) - r;
			p.y = p.y + r / k;
			if (p.x + k * p.y > 0) p = Vector2(p.x - k * p.y, -k * p.x - p.y) / 2;
			p.x -= Mathf::Clamp(p.x, -2 * r, 0.0f);
			return -p.magnitude() * Mathf::Sign(p.y);
		}

		float Chevron(Vector2 p)
		{
			float outer = Triangle(p, 0.85f);
			float inner = Triangle(p + Vector2(0, 0.45f), 0.6f);
			return Mathf::Max(outer, -inner);
		}

		float Hexagon(Vector2 p)
		{
			auto q = Vector2(Mathf::Abs(p.x), Mathf::Abs(p.y));
			float d = Mathf::Max(q.x * 0.866f + q.y * 0.5f, q.y) - 0.62f;
			return Outline(d, 0.16f);
		}

		float StarSdf(Vector2 p)
		{
			float core = p.magnitude() - 0.3f;
			float rays = Mathf::Min(Box(p, Vector2(0.92f, 0.05f)), Box(p, Vector2(0.05f, 0.92f)));
			return Mathf::Min(core, rays);
		}

		/// A dark core with a halo round it, standing well clear: the opposite of StarSdf's sparkle.
		float SingularitySdf(Vector2 p) { return Mathf::Min(Ring(p, 0.78f, 0.14f), p.magnitude() - 0.24f); }

		float CarrierSdf(Vector2 p)
		{
			float body = Outline(Box(p, Vector2(0.85f, 0.42f)), 0.14f);
			float deck = Box(p, Vector2(0.6f, 0.05f));
			return Mathf::Min(body, deck);
		}

		float Ticks(Vector2 p)
		{
			return Mathf::Min(Box(p - Vector2(0, 0.72f), Vector2(0.05f, 0.2f)),
				Mathf::Min(Box(p - Vector2(0.72f, 0), Vector2(0.2f, 0.05f)), Box(p + Vector2(0.72f, 0), Vector2(0.2f, 0.05f))));
		}

		cs::Ref<Texture2D> Solid()
		{
			auto t = cs::New<Texture2D>(1, 1, TextureFormat::RGBA32, false);
			t->hideFlags = HideFlags::DontSave;
			t->SetPixel(0, 0, Color::white());
			t->Apply();
			return t;
		}

		/// Rasterises a signed distance field (negative inside, in [-1,1] icon space) with 1.5 px anti-aliasing.
		cs::Ref<Texture2D> Draw(const cs::Func<float, Vector2>& sdf)
		{
			auto t = cs::New<Texture2D>(Size, Size, TextureFormat::RGBA32, true);
			t->hideFlags = HideFlags::DontSave;
			t->wrapMode = TextureWrapMode::Clamp;
			t->filterMode = FilterMode::Trilinear;
			auto px = cs::NewArray<Color32>(Size * Size);
			float aa = 1.5f * 2.0f / Size;
			for (int y = 0; y < Size; y++)
			{
				for (int x = 0; x < Size; x++)
				{
					auto p = Vector2((x + 0.5f) / Size * 2 - 1, (y + 0.5f) / Size * 2 - 1);
					float a = Mathf::Clamp01(0.5f - sdf(p) / aa);
					px[y * Size + x] = Color32(255, 255, 255, static_cast<uint8_t>(a * 255));
				}
			}
			t->SetPixels32(px);
			t->Apply(true, true);
			return t;
		}

		const cs::Ref<Texture2D>& Lazy(cs::Ref<Texture2D>& slot, const cs::Func<float, Vector2>& sdf)
		{
			if (!slot) slot = Draw(sdf);
			return slot;
		}
	}

	const cs::Ref<Texture2D>& Icons::White()
	{
		if (!white) white = Solid();
		return white;
	}

	const cs::Ref<Texture2D>& Icons::Prograde()
	{
		return Lazy(prograde, [](Vector2 p) { return Mathf::Min(Mathf::Min(Ring(p, 0.42f, 0.12f), p.magnitude() - 0.09f), Ticks(p)); });
	}

	const cs::Ref<Texture2D>& Icons::Retrograde()
	{
		return Lazy(retrograde, [](Vector2 p)
		{
			auto q = Vector2((p.x + p.y) * 0.70710678f, (p.y - p.x) * 0.70710678f);
			float cross = Mathf::Min(Box(q, Vector2(0.045f, 0.38f)), Box(q, Vector2(0.38f, 0.045f)));
			return Mathf::Min(Mathf::Min(Ring(p, 0.42f, 0.12f), cross), Ticks(p));
		});
	}

	const cs::Ref<Texture2D>& Icons::Blip()
	{
		return Lazy(blip, [](Vector2 p)
		{
			float cross = Mathf::Min(Box(p, Vector2(0.05f, 0.3f)), Box(p, Vector2(0.3f, 0.05f)));
			float corners = Mathf::Max(Outline(Box(p, Vector2(0.62f, 0.62f)), 0.1f), -Mathf::Min(Box(p, Vector2(0.3f, 0.8f)), Box(p, Vector2(0.8f, 0.3f))));
			return Mathf::Min(cross, corners);
		});
	}

	const cs::Ref<Texture2D>& Icons::Lost()
	{
		return Lazy(lost, [](Vector2 p)
		{
			const float r = 0.3f, t = 0.15f;
			auto c = Vector2(0, 0.3f);
			auto q = p - c;
			// The hook: the circle round c, open in its lower-left quarter, ending left and at the bottom.
			float hook = q.x < 0 && q.y < 0
				? Mathf::Min((q - Vector2(-r, 0)).magnitude(), (q - Vector2(0, -r)).magnitude()) - t * 0.5f
				: Ring(q, r, t);
			float stem = Box(p - Vector2(0, -0.13f), Vector2(t * 0.5f, 0.13f));
			float point = (p - Vector2(0, -0.58f)).magnitude() - 0.11f;
			return Mathf::Min(Mathf::Min(hook, stem), point);
		});
	}

	cs::Ref<Texture2D> Icons::ForBody(const cs::Ref<BodyDefinition>& body)
	{
		// A Void Base's generator (U.SPC-T372): a dark core with a halo round it, not the ordinary star sparkle.
		if (body->Singularity) return Lazy(singularity, SingularitySdf);
		switch (body->Kind)
		{
			case BodyKind::Star: return Lazy(star, StarSdf);
			case BodyKind::Moon: return Lazy(dot, [](Vector2 p) { return Ring(p, 0.42f, 0.16f); });
			case BodyKind::Station: return Lazy(hexagon, Hexagon);
			default: return Lazy(ring, [](Vector2 p) { return Ring(p, 0.62f, 0.14f); });
		}
	}

	cs::Ref<Texture2D> Icons::ForShip(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Carrier: return Lazy(carrier, CarrierSdf);
			case ShipClass::Battleship: return Lazy(diamond, [](Vector2 p) { return Mathf::Abs(p.x) * 1.25f + Mathf::Abs(p.y) - 0.85f; });
			case ShipClass::Cruiser: return Lazy(triangle, [](Vector2 p) { return Triangle(p, 0.8f); });
			case ShipClass::Destroyer: return Lazy(chevron, Chevron);
			case ShipClass::Miner:
			case ShipClass::OreTug:
			case ShipClass::CaptureTug: return Lazy(hexagon, Hexagon);
			case ShipClass::Courier: return Lazy(dot, [](Vector2 p) { return Ring(p, 0.42f, 0.16f); });
			// A gate picket (U.SPC-T480): the contact blip, a lookout's mark.
			case ShipClass::GatePicket: return Blip();
			case ShipClass::Fighter: return Lazy(dart, [](Vector2 p) { return Triangle(Vector2(p.x * 1.8f, p.y), 0.75f); });
			// The heavy fighter (U.SPC-T467): a broader, blunter dart than the light fighter's.
			case ShipClass::Gunship: return Lazy(wedge, [](Vector2 p) { return Triangle(Vector2(p.x * 1.25f, p.y), 0.8f); });
			// Strike craft (U.SPC-T484): the interceptor a thinner dart, the bomber a broad, deep wedge.
			case ShipClass::Interceptor: return Lazy(needle, [](Vector2 p) { return Triangle(Vector2(p.x * 2.6f, p.y), 0.75f); });
			case ShipClass::Bomber: return Lazy(broad, [](Vector2 p) { return Triangle(Vector2(p.x * 1.1f, p.y * 1.3f), 0.7f); });
			// The recon fighter (U.SPC-T485): the interceptor's needle inside a ring, its sensor sweep.
			case ShipClass::Recon: return Lazy(scout, [](Vector2 p) { return Mathf::Min(Ring(p, 0.8f, 0.1f), Triangle(Vector2(p.x * 3.2f, p.y * 1.3f), 0.75f)); });
			// The command cruiser (U.SPC-T477): a cruiser's triangle inside the ring of its command reach.
			case ShipClass::CommandCruiser: return Lazy(pennant, [](Vector2 p) { return Mathf::Min(Ring(p, 0.82f, 0.1f), Triangle(p * 1.45f, 0.8f)); });
			// The frigate (U.SPC-T475): a slim arrowhead, a strike boat's mark.
			case ShipClass::Frigate: return Lazy(arrow, [](Vector2 p) { return Outline(Triangle(Vector2(p.x * 1.6f, p.y), 0.8f), 0.12f); });
			// The corvette (U.SPC-T476): a destroyer's chevron inside a ring, the law's badge.
			case ShipClass::Corvette: return Lazy(badge, [](Vector2 p) { return Mathf::Min(Ring(p, 0.85f, 0.1f), Chevron(p * 1.5f)); });
			// The boarding shuttle (U.SPC-T486): a stubby box with a bar across it, its docking collar.
			case ShipClass::AssaultShuttle: return Lazy(shuttle, [](Vector2 p) { return Mathf::Min(Box(p, Vector2(0.3f, 0.5f)), Box(p, Vector2(0.55f, 0.08f))); });
			// The gate breaker (U.SPC-T478): a battleship's diamond inside the ring of the gate it breaks.
			case ShipClass::GateBreaker: return Lazy(breaker, [](Vector2 p) { return Mathf::Min(Ring(p, 0.84f, 0.1f), Mathf::Abs(p.x) * 2.0f + Mathf::Abs(p.y) * 1.6f - 0.85f); });
			default: return Lazy(square, [](Vector2 p) { return Outline(Box(p, Vector2(0.62f, 0.62f)), 0.14f); });
		}
	}

	float Icons::PixelSize(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Carrier: return 22;
			case ShipClass::Battleship: return 20;
			// A siege capital, a battleship's weight (U.SPC-T478).
			case ShipClass::GateBreaker: return 20;
			case ShipClass::Cruiser: return 16;
			case ShipClass::Destroyer: return 13;
			case ShipClass::Courier:
			case ShipClass::GatePicket: return 9;
			case ShipClass::Fighter: return 8;
			case ShipClass::Gunship: return 10;
			case ShipClass::Interceptor: return 7;
			case ShipClass::Bomber:
			case ShipClass::AssaultShuttle: return 10;
			case ShipClass::Recon: return 8;
			case ShipClass::CommandCruiser: return 18;
			case ShipClass::Frigate: return 11;
			case ShipClass::Corvette: return 10;
			default: return 12;
		}
	}

	float Icons::PixelSize(const cs::Ref<BodyDefinition>& body)
	{
		switch (body->Kind)
		{
			case BodyKind::Star: return 30;
			case BodyKind::Moon: return 10;
			case BodyKind::DwarfPlanet: return 13;
			case BodyKind::Asteroid: return 11;
			default: return 17;
		}
	}
}
