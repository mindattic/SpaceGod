// Port of Unity.SpaceGod Runtime/Views/FighterViews.cs (US-T30).
#include "Views/FighterViews.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"
#include "Views/LineStrip.h"

#include "Combat/Battle.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"
#include "Combat/War.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	cs::Ref<Material>& FighterViews::glowMaterial()
	{
		static cs::Ref<Material> value;
		return value;
	}

	cs::Ref<Material>& FighterViews::shipMaterial()
	{
		static cs::Ref<Material> value;
		return value;
	}

	cs::Ref<Mesh>& FighterViews::quad()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	FighterViews::FighterViews(UnitySpaceApp* app_, const cs::Ref<Transform>& root_)
		: app(app_), root(root_)
	{
		if (!quad()) quad() = MeshFactory::Quad();
		if (!Alive(glowMaterial()))
		{
			glowMaterial() = Materials::MarkShared(Materials::Create(Materials::Glow(), "FighterGlow"));
			glowMaterial()->SetColor("_Color", Color(0.55f, 0.8f, 1.0f));
			glowMaterial()->SetFloat("_Intensity", 1.6f);
			glowMaterial()->SetFloat("_Falloff", 2.0f);
		}
		if (!shipMaterial()) shipMaterial() = ShipModels::ProceduralMaterial();
	}

	void FighterViews::Update()
	{
		float now = Time::unscaledTime;
		cs::HashSet<cs::Ref<BattleUnit>> live;
		cs::HashSet<cs::Ref<FlightDeck>> liveDecks;
		auto cam = app->UnityCamera->transform;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		for (const auto& b : app->World()->War->Battles)
		{
			for (const auto& u : b->Units)
			{
				if (u->Kind == UnitKind::Ship && u->Present()) DrawDecks(b, u, ppu, liveDecks);
				// A craft down on a flight deck is drawn on it, inside its hull (U.SPC-T518).
				bool onDeck = FlightDecks::OnDeck(u) && !u->Fighter->Deck->Deck->Carrier->Dead && u->Fighter->Deck->Deck->Carrier->Present();
				if (u->Kind != UnitKind::Fighter || !(u->Present() || onDeck)) continue;
				live.Add(u);
				cs::Ref<View> v;
				if (!views.TryGetValue(u, v)) views[u] = v = Create(u);
				auto scene = app->BattleScene(b, u->Pos);
				auto rel = app->Relative(scene);
				// Its model while big enough, then a glow impostor, then nothing below a pixel (U.SPC-T415): a battle
				// seen from across the system is its ships' icons, not a cloud of sparks.
				auto tier = RenderLod::Tier(RenderLod::Fighter(), RenderLod::ApparentPx(app->Scale()->ShipLength(u->LengthKm), rel.magnitude(), ppu));
				bool model = RenderLod::DrawsModel(tier), shown = tier != LodTier::Culled;
				if (v->Hull->activeSelf != model) v->Hull->SetActive(model);
				if (v->Glow->gameObject()->activeSelf != shown) v->Glow->gameObject()->SetActive(shown);
				if (!shown)
				{
					v->Trail->SetVisible(false);
					continue;
				}
				auto f = u->Forward.SwapYZ();
				auto fwd = Vector3(static_cast<float>(f.X), static_cast<float>(f.Y), static_cast<float>(f.Z));
				v->Hull->transform->localPosition = rel;
				if (fwd.sqrMagnitude() > 1e-12f)
				{
					// The unit's own roof (a manned fighter's roll), not world up (U.SPC-T563).
					DVec3 discard, roof; // out _, out var roof
					u->Basis(discard, roof);
					auto up = roof.SwapYZ();
					v->Hull->transform->localRotation = Quaternion::LookRotation(fwd, Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z)));
				}
				v->Hull->transform->localScale = Vector3::one() * static_cast<float>(app->Scale()->ShipLength(u->LengthKm));

				float px = 5.0f * Mathf::Max(rel.magnitude(), 1e-6f) / static_cast<float>(ppu);
				v->Glow->localPosition = rel - fwd.normalized() * static_cast<float>(app->Scale()->ShipLength(u->LengthKm)) * 0.55f;
				v->Glow->SetRotation(Quaternion::LookRotation(v->Glow->position() - cam->position(), cam->up()));
				v->Glow->localScale = Vector3::one() * Mathf::Max(px, static_cast<float>(app->Scale()->ShipLength(u->LengthKm)) * 0.8f);

				if (now >= v->NextSample)
				{
					v->History.Add(u->Pos);
					if (v->History.Count() > TrailLength) v->History.RemoveAt(0);
					v->NextSample = now + SampleSeconds;
				}
				int n = v->History.Count();
				if (n >= 2)
				{
					for (int i = 0; i < n; i++) trailPoints[i] = app->Relative(app->BattleScene(b, v->History[i]));
					trailPoints[n - 1] = rel;
					auto c = u->Faction->Color;
					v->Trail->SetPoints(trailPoints, n, Color(c.R, c.G, c.B, 0.55f), 0.0f, 0.8f);
					v->Trail->SetTint(Color::white());
					v->Trail->Root->transform->localPosition = Vector3::zero();
					v->Trail->SetVisible(true);
				}
			}
		}
		cs::List<cs::Ref<BattleUnit>> gone;
		for (auto kv : views)
			if (!live.Contains(kv.first)) gone.Add(kv.first);
		for (const auto& u : gone)
		{
			auto v = views.At(u);
			Destroy(v->Hull);
			Destroy(v->Glow->gameObject());
			v->Trail->Dispose();
			views.Remove(u);
		}
		cs::List<cs::Ref<FlightDeck>> closed;
		for (auto kv : deckViews)
			if (!liveDecks.Contains(kv.first)) closed.Add(kv.first);
		for (const auto& d : closed)
		{
			auto dv = deckViews.At(d);
			dv->Stern->Dispose();
			dv->Bow->Dispose();
			dv->Path->Dispose();
			deckViews.Remove(d);
		}
	}

	void FighterViews::DrawDecks(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, double ppu, cs::HashSet<cs::Ref<FlightDeck>>& liveDecks)
	{
		const auto& decks = FlightDecks::Of(carrier);
		if (decks.Count() == 0) return;
		auto rel = app->Relative(app->BattleScene(b, carrier->Pos));
		auto tier = RenderLod::Tier(FlightDecks::Lod(), RenderLod::ApparentPx(app->Scale()->ShipLength(carrier->LengthKm), rel.magnitude(), ppu));
		bool shown = RenderLod::DrawsModel(tier);
		double w = cs::Math::Max(MouthMinKm, carrier->LengthKm * MouthHalfWidth), h = cs::Math::Max(MouthMinKm * 0.5, carrier->LengthKm * MouthHalfHeight);
		for (const auto& d : decks)
		{
			liveDecks.Add(d);
			cs::Ref<DeckView> dv;
			if (!deckViews.TryGetValue(d, dv))
			{
				dv = cs::New<DeckView>();
				dv->Stern = cs::New<LineStrip>("Deck stern", root);
				dv->Bow = cs::New<LineStrip>("Deck bow", root);
				dv->Path = cs::New<LineStrip>("Glide path", root);
				deckViews[d] = dv;
			}
			dv->Bow->SetVisible(shown);
			dv->Stern->SetVisible(shown);
			dv->Path->SetVisible(false);
			if (!shown) continue;
			auto color = d->Open() ? DeckOpen : DeckClosed;
			Frame(b, d, d->SternAlong, w, h, dv->Stern, color);
			Frame(b, d, d->BowAlong, w, h, dv->Bow, color);
			if (d->Open() && d->Final != nullptr && d->Final->Present())
			{
				trailPoints[0] = app->Relative(app->BattleScene(b, d->Gate()));
				trailPoints[1] = app->Relative(app->BattleScene(b, d->Stern()));
				dv->Path->SetPoints(trailPoints, 2, color, 0.15f, 0.8f);
				dv->Path->SetVisible(true);
			}
		}
	}

	void FighterViews::Frame(const cs::Ref<Battle>& b, const cs::Ref<FlightDeck>& d, double along, double w, double h, const cs::Ref<LineStrip>& strip, Color color)
	{
		framePoints[0] = app->Relative(app->BattleScene(b, d->At(along, DVec3(-w, -h, 0))));
		framePoints[1] = app->Relative(app->BattleScene(b, d->At(along, DVec3(w, -h, 0))));
		framePoints[2] = app->Relative(app->BattleScene(b, d->At(along, DVec3(w, h, 0))));
		framePoints[3] = app->Relative(app->BattleScene(b, d->At(along, DVec3(-w, h, 0))));
		framePoints[4] = framePoints[0];
		strip->SetPoints(framePoints, 5, color);
		strip->Root->transform->localPosition = Vector3::zero();
	}

	cs::Ref<FighterViews::View> FighterViews::Create(const cs::Ref<BattleUnit>& u)
	{
		auto v = cs::New<View>();
		// Drawn as its own hull class (U.SPC-T484): interceptor, fighter, bomber.
		auto cls = FighterCraft::Of(u->Fighter->Role)->Class;
		v->Hull = NewGameObject(Core::ToString(cls));
		v->Hull->transform->SetParent(root, false);
		// Ultimate Spaceships Creator's model of its class when that pack is there (US-T142), else the Star Sparrow kit's
		// (US-T110): parts under the hull, which is placed and scaled as before; otherwise the procedural craft.
		cs::List<cs::Ref<Renderer>> parts;
		cs::Ref<Transform> stern, bow;
		auto layout = Rendering::StarSparrowKit::LayoutOf(cls, nullptr);
		if (!Rendering::UscKit::Build(cls, CivilCraft::None, 0, u->Faction, layout, v->Hull->transform, parts, stern, bow)
			&& !Rendering::StarSparrowKit::Build(cls, CivilCraft::None, 0, u->Faction, layout, v->Hull->transform, parts, stern, bow))
		{
			AddComponent<MeshFilter>(v->Hull)->sharedMesh = ShipMeshFactory::GetFighter(u->Faction, cls);
			auto mr = AddComponent<MeshRenderer>(v->Hull);
			mr->SetSharedMaterial(shipMaterial());
			mr->shadowCastingMode = ShadowCastingMode::Off;
		}

		auto glow = NewGameObject("Engine");
		glow->transform->SetParent(root, false);
		AddComponent<MeshFilter>(glow)->sharedMesh = quad();
		auto gr = AddComponent<MeshRenderer>(glow);
		gr->SetSharedMaterial(glowMaterial());
		gr->shadowCastingMode = ShadowCastingMode::Off;
		v->Glow = glow->transform;

		v->Trail = cs::New<LineStrip>("Trail", root);
		v->Trail->SetVisible(false);
		return v;
	}
}
