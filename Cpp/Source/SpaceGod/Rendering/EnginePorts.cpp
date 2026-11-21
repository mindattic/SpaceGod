// US-T432: where a hull's engines actually are (see EnginePorts.h).
#include "Rendering/EnginePorts.h"

#include <cstdlib>
#include <functional>

namespace SpaceGod::Rendering
{
	namespace
	{
		constexpr const char* EngineTag = ":Engine";

		bool IsPort(const cs::String& name) { return name.rfind(EnginePorts::Name, 0) == 0; }

		int EngineOf(const cs::String& name)
		{
			auto at = name.find(EngineTag);
			return at == cs::String::npos ? -1 : std::atoi(name.c_str() + at + 7);
		}

		/// The port's node read in <paramref name="root"/>'s frame: its place, its +Z and its scale through every node between.
		EnginePort Read(const Transform* t, const Transform* root, int engine)
		{
			EnginePort p;
			p.Engine = engine;
			Vector3 at = Vector3::zero(), dir = Vector3::forward();
			float scale = 1;
			while (t != nullptr && t != root)
			{
				at = t->localPosition + t->localRotation * Vector3::Scale(t->localScale, at);
				dir = t->localRotation * dir;
				scale *= t->localScale.x;
				auto up = t->parent.lock();
				t = up.get();
			}
			p.Position = at;
			p.Exhaust = dir.sqrMagnitude() > 1e-8f ? dir.normalized() : Vector3::back();
			p.Radius = Mathf::Abs(scale);
			return p;
		}
	}

	void EnginePorts::Mark(const cs::Ref<Transform>& parent, Vector3 at, Vector3 exhaust, float radius, int engine)
	{
		if (!parent || !(exhaust.sqrMagnitude() > 1e-8f) || !(radius > 0)) return;
		auto go = NewGameObject(engine >= 0 ? cs::Cat(Name, EngineTag, engine) : cs::String(Name));
		go->transform->SetParent(parent, false);
		go->transform->localPosition = at;
		Vector3 e = exhaust.normalized();
		go->transform->localRotation = Quaternion::LookRotation(e, Mathf::Abs(e.y) > 0.9f ? Vector3::forward() : Vector3::up());
		go->transform->localScale = Vector3::one() * radius;
	}

	cs::List<EnginePort> EnginePorts::Of(const cs::Ref<Transform>& root)
	{
		cs::List<EnginePort> ports;
		if (!root) return ports;
		std::function<void(const cs::Ref<Transform>&)> walk = [&](const cs::Ref<Transform>& t)
		{
			for (const auto& child : t->children)
			{
				if (!child) continue;
				auto go = child->gameObject();
				if (go && !go->destroyed && IsPort(go->name)) ports.Add(Read(child.get(), root.get(), EngineOf(go->name)));
				else walk(child);
			}
		};
		walk(root);
		return ports;
	}

	bool EnginePorts::ForEngine(const cs::Ref<Transform>& root, int engine, cs::Ref<Transform>& section, EnginePort& port)
	{
		if (!root || engine < 0) return false;
		bool found = false;
		std::function<void(const cs::Ref<Transform>&)> walk = [&](const cs::Ref<Transform>& t)
		{
			for (const auto& child : t->children)
			{
				if (found || !child) continue;
				auto go = child->gameObject();
				if (go && !go->destroyed && IsPort(go->name))
				{
					if (EngineOf(go->name) != engine) continue;
					section = t;
					port = Read(child.get(), t.get(), engine);
					found = true;
				}
				else walk(child);
			}
		};
		walk(root);
		return found;
	}
}
