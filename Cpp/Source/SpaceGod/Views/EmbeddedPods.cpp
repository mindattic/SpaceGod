// Port of Unity.SpaceGod Runtime/Views/EmbeddedPods.cs (US-T30).
#include "Views/EmbeddedPods.h"

#include "UnitySpaceApp.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"

#include "Combat/Battle.h"
#include "Combat/BreacherBays.h"
#include "Combat/War.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	EmbeddedPods::EmbeddedPods(UnitySpaceApp* app_, const cs::Ref<Transform>& root_)
		: app(app_), root(root_)
	{
	}

	void EmbeddedPods::Update()
	{
		cs::HashSet<cs::Ref<Embedded>> live;
		cs::HashSet<cs::Ref<Missile>> flying;
		auto war = app->World()->War;
		for (const auto& b : war->Battles)
		{
			Track(b, live);
			TrackRounds(b, flying);
		}
		for (const auto& b : war->Ended) Track(b, live);
		cs::List<cs::Ref<Embedded>> gone;
		for (auto kv : pods)
			if (!live.Contains(kv.first) || !Alive(kv.second)) gone.Add(kv.first);
		for (const auto& e : gone)
		{
			if (Alive(pods.At(e))) Destroy(pods.At(e));
			pods.Remove(e);
		}
		cs::List<cs::Ref<Missile>> landed;
		for (auto kv : rounds)
			if (!flying.Contains(kv.first) || !Alive(kv.second)) landed.Add(kv.first);
		for (const auto& m : landed)
		{
			if (Alive(rounds.At(m))) Destroy(rounds.At(m));
			rounds.Remove(m);
		}
	}

	void EmbeddedPods::Track(const cs::Ref<Battle>& b, cs::HashSet<cs::Ref<Embedded>>& live)
	{
		for (const auto& e : b->Embedded)
		{
			if (e->Unit->Dead || (e->Unit->Ship != nullptr && e->Unit->Ship->Destroyed)) continue;
			auto parent = app->UnitTransform(e->Unit);
			if (parent == nullptr) continue;
			live.Add(e);
			cs::Ref<GameObject> go;
			if (pods.TryGetValue(e, go) && Alive(go) && go->transform->parent.lock() == parent) continue;
			if (Alive(go)) Destroy(go);
			pods[e] = Create(e, parent, b);
		}
	}

	void EmbeddedPods::TrackRounds(const cs::Ref<Battle>& b, cs::HashSet<cs::Ref<Missile>>& flying)
	{
		if (root == nullptr) return;
		float now = Time::unscaledTime;
		for (const auto& m : b->Missiles)
		{
			if (m->Gone || m->Kind != MissileKind::Breacher) continue;
			cs::Ref<Faction> owner = m->Owner != nullptr ? m->Owner : (m->Side >= 0 && m->Side < 2 ? b->SideLeaders[m->Side] : nullptr);
			if (owner == nullptr) continue;
			flying.Add(m);
			cs::Ref<GameObject> go;
			if (!rounds.TryGetValue(m, go) || !Alive(go))
				rounds[m] = go = CreateRound(owner);
			auto t = go->transform;
			t->localPosition = app->Relative(app->BattleScene(b, m->Pos));
			auto v = m->Vel.SwapYZ();
			auto track = Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z));
			if (track.sqrMagnitude() < 1e-12f) track = Vector3::forward();
			track.Normalize();
			auto along = Quaternion::LookRotation(track, Mathf::Abs(track.y) > 0.95f ? Vector3::right() : Vector3::up());
			t->localRotation = m->Adrift ? along * Quaternion::Euler(now * 40.0f, now * 23.0f, now * 11.0f) : along;
		}
	}

	cs::Ref<GameObject> EmbeddedPods::CreateRound(const cs::Ref<Faction>& owner)
	{
		auto go = NewGameObject("Breacher round");
		go->transform->SetParent(root, false);
		AddComponent<MeshFilter>(go)->sharedMesh = ShipMeshFactory::GetBreacherRound(owner);
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		go->transform->localScale = Vector3::one() * static_cast<float>(app->Scale()->ShipLength(BreacherBays::RoundLengthKm));
		return go;
	}

	cs::Ref<GameObject> EmbeddedPods::Create(const cs::Ref<Embedded>& e, const cs::Ref<Transform>& parent, const cs::Ref<Battle>& b)
	{
		cs::Ref<Faction> owner = e->Side >= 0 && e->Side < 2 ? b->SideLeaders[e->Side] : e->Unit->Faction;
		auto go = NewGameObject(e->Breacher ? "Breacher round" : "Boarding pod");
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = e->Breacher ? ShipMeshFactory::GetBreacherRound(owner != nullptr ? owner : e->Unit->Faction)
			: ShipMeshFactory::GetPod(owner != nullptr ? owner : e->Unit->Faction);
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		// Pods are drawn a little large so they read on a capital ship: a few percent of its length.
		// A Breacher round is drawn at its own length, a good deal bigger than a pod.
		float size = e->Breacher
			? Mathf::Clamp(static_cast<float>(BreacherBays::RoundLengthKm) / Mathf::Max(static_cast<float>(e->Unit->LengthKm), 0.01f), 0.08f, 0.35f)
			: Mathf::Clamp(0.025f / Mathf::Max(static_cast<float>(e->Unit->LengthKm), 0.01f), 0.04f, 0.15f);
		auto inward = Vector3(static_cast<float>(e->Inward.X), static_cast<float>(e->Inward.Y), static_cast<float>(e->Inward.Z));
		if (inward.sqrMagnitude() < 1e-6f) inward = Vector3::forward();
		inward.Normalize();
		auto at = Vector3(static_cast<float>(e->Local.X), static_cast<float>(e->Local.Y), static_cast<float>(e->Local.Z));
		// The drill tip is buried: the pod's centre sits half a pod out along the way it came in.
		go->transform->localPosition = at - inward * size * 0.25f;
		go->transform->localRotation = Quaternion::LookRotation(inward, Mathf::Abs(inward.y) > 0.95f ? Vector3::right() : Vector3::up());
		go->transform->localScale = Vector3::one() * size;
		return go;
	}
}
