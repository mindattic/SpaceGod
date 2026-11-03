// Port of Unity.SpaceGod Runtime/Views/InterstellarView.cs (US-T30).
#include "Views/InterstellarView.h"

#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Views/LineStrip.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "GalacticPosition.h"
#include "Ships.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	cs::Ref<Mesh>& InterstellarView::quad()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	InterstellarView::InterstellarView(const cs::Ref<Galaxy>& galaxy_, const cs::Ref<Transform>& parent)
		: galaxy(galaxy_)
	{
		Root = NewGameObject("Interstellar");
		Root->transform->SetParent(parent, false);
		if (!quad()) quad() = MeshFactory::Quad(); // quad ??= MeshFactory.Quad()

		for (const auto& system : galaxy->Systems)
		{
			bool labelled = false;
			for (const auto& b : system->Catalog->Bodies())
			{
				if (!b->IsStar()) continue;
				auto offsetLy = system->Ephemeris->Helio(b->Index) / Astro::LightYearKm;
				auto c = b->Color;
				float size = Mathf::Clamp(Mathf::Pow(static_cast<float>(b->LuminositySolar), 0.25f), 0.45f, 3.0f);
				// The system's first star carries its name; a companion far enough out to stand apart
				// (Proxima, Epsilon Indi B) its own; a close partner (Alpha Centauri B, Sirius B) none, as it would overlap.
				// C#'s `far` and `near` locals are farOut / nearIn here: Windows' headers define far and near as macros.
				bool farOut = system->Ephemeris->Helio(b->Index).Length() > 500 * Astro::AuKm;
				cs::String label = !labelled ? (b->Name == "Sun" ? cs::String("Sol") : system->Name) : farOut ? b->Name : cs::String();
				auto star = Add(b->Name, system->PositionLy + offsetLy, Color(c.R, c.G, c.B), size, /*system:*/ !labelled || farOut, label);
				if (!labelled) star->Of = system;
				labelled = true;
			}
		}
		for (const auto& n : galaxy->NearbyStars)
		{
			double absMag = n.AbsoluteMagnitude();
			float luminosity = cs::Double::IsNaN(absMag) ? 1e-5f : Mathf::Pow(10, -0.4f * static_cast<float>(absMag - 4.83));
			float size = cs::Double::IsNaN(absMag) ? 0.35f : Mathf::Clamp(Mathf::Pow(luminosity, 0.25f), 0.45f, 3.0f);
			Add(n.Name, n.PositionLy, Color(n.Color.R, n.Color.G, n.Color.B), size, false, n.Name);
		}
		for (const auto& l : galaxy->Network->Links()) links.Add(cs::New<LineStrip>(l->ToString(), Root->transform));
		Root->SetActive(false);
	}

	cs::Ref<InterstellarView::Star> InterstellarView::Add(const cs::String& name, DVec3 ly, Color color, float size, bool system, const cs::String& label)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(Root->transform, false);
		AddComponent<MeshFilter>(go)->sharedMesh = quad();
		auto mr = AddComponent<MeshRenderer>(go);
		auto m = Materials::Create(Materials::Glow(), name);
		m->SetColor("_Color", Color::Lerp(color, Color::white(), 0.25f));
		m->SetFloat("_Intensity", 1.6f);
		m->SetFloat("_Falloff", 2.4f);
		mr->SetSharedMaterial(m);
		mr->shadowCastingMode = ShadowCastingMode::Off;
		auto star = cs::New<Star>();
		star->Name = name;
		star->Label = label;
		star->PositionLy = ly;
		star->Color = color;
		star->Size = size;
		star->System = system;
		star->T = go->transform;
		star->Material = m;
		stars.Add(star);
		return star;
	}

	void InterstellarView::Update(DVec3 cameraLy, Quaternion cameraRotation, float fade)
	{
		if (!Visible()) return;
		for (const auto& s : stars)
		{
			auto d = (s->PositionLy - cameraLy).SwapYZ() * UnitsPerLy;
			auto rel = Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
			s->Relative = rel;
			float dist = Mathf::Max(rel.magnitude(), 1e-3f);
			s->T->localPosition = rel;
			s->T->localRotation = Quaternion::LookRotation(rel / dist, cameraRotation * Vector3::up());
			// Steady angular size, a little larger as the camera closes in.
			float nearIn = Mathf::Clamp(1.6f - dist / 1500.0f, 1.0f, 3.2f);
			s->T->localScale = Vector3::one() * dist * 0.022f * s->Size * nearIn;
			s->Material->SetFloat("_Intensity", 1.6f * fade);
		}

		// The gate links, each its own strip: pale blue when open, red when either end is shut.
		double jd = galaxy->Systems.Count() > 0 ? galaxy->Systems[0]->Ephemeris->Jd : 0;
		auto At = [&](DVec3 ly)
		{
			auto d = (ly - cameraLy).SwapYZ() * UnitsPerLy;
			return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
		};
		auto pair = cs::NewArray<Vector3>(2);
		const auto& net = galaxy->Network->Links();
		// A strip for each link finished since (U.SPC-T97).
		while (links.Count() < net.Count()) links.Add(cs::New<LineStrip>(net[links.Count()]->ToString(), Root->transform));
		for (int i = 0; i < net.Count() && i < links.Count(); i++)
		{
			auto l = net[i];
			bool shut = l->AtA->Blockaded(jd) || l->AtB->Blockaded(jd);
			pair[0] = At(l->A->PositionLy);
			pair[1] = At(l->B->PositionLy);
			// Grey and faint while a ring is out of action, red under blockade, pale blue when open.
			auto c = !l->Working() ? Color(0.5f, 0.5f, 0.5f, 0.25f * fade)
				: shut ? Color(1.0f, 0.35f, 0.3f, 0.55f * fade) : Color(0.45f, 0.85f, 1.0f, 0.4f * fade);
			links[i]->SetPoints(pair, 2, c, 1.0f, 1.0f);
			links[i]->SetTint(Color::white());
			links[i]->Root->transform->localPosition = Vector3::zero();
			links[i]->SetVisible(true);
		}

		// Ships in the gates: a spark each, partway along its link. Then the groups out in open space under their own
		// drives (U.SPC-T412), each where it really is between the stars.
		const auto& inGates = galaxy->Traffic->InGates();
		const auto& cruises = galaxy->DeepSpace->Cruises();
		int shown = inGates.Count() + cruises.Count();
		while (sparks.Count() < shown)
		{
			auto go = NewGameObject("Passage");
			go->transform->SetParent(Root->transform, false);
			AddComponent<MeshFilter>(go)->sharedMesh = quad();
			auto mr = AddComponent<MeshRenderer>(go);
			auto m = Materials::Create(Materials::Glow(), "Passage");
			m->SetFloat("_Falloff", 2.4f);
			mr->SetSharedMaterial(m);
			mr->shadowCastingMode = ShadowCastingMode::Off;
			sparks.Add(std::make_pair(go->transform, m));
		}
		for (int i = 0; i < sparks.Count(); i++)
		{
			auto [t, m] = sparks[i];
			bool on = i < shown && (i < inGates.Count() || cruises[i - inGates.Count()]->Ships.Count() > 0);
			t->gameObject()->SetActive(on);
			if (!on) continue;
			DVec3 ly;
			cs::Ref<Ship> leader;
			if (i < inGates.Count())
			{
				auto p = inGates[i];
				auto from = p->From->Home->PositionLy;
				ly = from + (p->To->Home->PositionLy - from) * p->Progress(jd);
				leader = p->Leader();
			}
			else
			{
				auto c = cruises[i - inGates.Count()];
				ly = c->PositionAt(jd).Ly();
				leader = c->Leader();
			}
			auto rel = At(ly);
			float dist = Mathf::Max(rel.magnitude(), 1e-3f);
			t->localPosition = rel;
			t->localRotation = Quaternion::LookRotation(rel / dist, cameraRotation * Vector3::up());
			t->localScale = Vector3::one() * dist * 0.012f;
			auto fc = leader->Faction->Color;
			m->SetColor("_Color", Color(fc.R, fc.G, fc.B));
			m->SetFloat("_Intensity", 1.8f * fade);
		}
	}
}
