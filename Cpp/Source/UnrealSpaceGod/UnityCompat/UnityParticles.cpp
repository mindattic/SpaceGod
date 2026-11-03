#include "UnityCompat/UnityParticles.h"

namespace SpaceGod::UnityCompat
{
	Color Gradient::Evaluate(float t) const
	{
		auto rgb = [&]() -> Color
		{
			if (colorKeys.empty()) return Color::white();
			if (t <= colorKeys.front().time) return colorKeys.front().color;
			for (size_t i = 1; i < colorKeys.size(); i++)
				if (t <= colorKeys[i].time)
				{
					const auto& a = colorKeys[i - 1];
					const auto& b = colorKeys[i];
					float u = b.time > a.time ? (t - a.time) / (b.time - a.time) : 0;
					return Color::Lerp(a.color, b.color, u);
				}
			return colorKeys.back().color;
		}();
		float alpha = 1;
		if (!alphaKeys.empty())
		{
			alpha = alphaKeys.back().alpha;
			if (t <= alphaKeys.front().time) alpha = alphaKeys.front().alpha;
			else
				for (size_t i = 1; i < alphaKeys.size(); i++)
					if (t <= alphaKeys[i].time)
					{
						const auto& a = alphaKeys[i - 1];
						const auto& b = alphaKeys[i];
						float u = b.time > a.time ? (t - a.time) / (b.time - a.time) : 0;
						alpha = Mathf::Lerp(a.alpha, b.alpha, u);
						break;
					}
		}
		return Color(rgb.r, rgb.g, rgb.b, alpha);
	}

	void ParticleSystem::Play()
	{
		playing = true;
	}

	void ParticleSystem::Stop(bool, ParticleSystemStopBehavior behavior)
	{
		playing = false;
		emitDebt = 0;
		if (behavior == ParticleSystemStopBehavior::StopEmittingAndClear) Clear();
	}

	void ParticleSystem::Clear()
	{
		particles.clear();
		BuildMesh();
	}

	void ParticleSystem::Emit(int count)
	{
		for (int i = 0; i < count && static_cast<int>(particles.size()) < main.maxParticles; i++)
		{
			Particle p;
			float lifeT = Random::value();
			p.lifetime = Mathf::Max(1e-4f, main.startLifetime.Evaluate(0, lifeT));
			p.size = main.startSize.Evaluate(0, Random::value());
			p.random = Random::value();
			p.color = main.startColor;
			float speed = main.startSpeed.Evaluate(0, Random::value());
			if (shape.enabled && shape.shapeType == ParticleSystemShapeType::Cone)
			{
				// Unity's cone: born on a disc of the shape's radius at the base, heading out within the cone's angle,
				// the further from the axis the more splayed (as the cone's walls would send it).
				float a = Random::value() * Mathf::PI * 2;
				float r = Mathf::Sqrt(Random::value());
				Vector3 at(Mathf::Cos(a) * r, Mathf::Sin(a) * r, 0);
				float spread = Mathf::Tan(shape.angle * Mathf::Deg2Rad);
				p.position = at * shape.radius;
				p.velocity = Vector3(at.x * spread, at.y * spread, 1).normalized() * speed;
			}
			else
			{
				p.velocity = Vector3::forward() * speed;
			}
			particles.push_back(p);
		}
	}

	void ParticleSystem::Simulate(float dt)
	{
		if (dt <= 0) return;
		time += dt;
		for (auto& p : particles)
		{
			p.age += dt;
			p.position += p.velocity * dt;
		}
		particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.age >= p.lifetime; }), particles.end());
		if (playing && emission.enabled)
		{
			emitDebt += emission.rateOverTime.Evaluate(0, 0) * dt;
			int n = static_cast<int>(emitDebt);
			emitDebt -= static_cast<float>(n);
			Emit(n);
		}
		BuildMesh();
	}

	void ParticleSystem::BuildMesh()
	{
		std::vector<Vector3> v;
		std::vector<Color> c;
		std::vector<Vector2> uv;
		std::vector<int> tri;
		if (!particles.empty())
		{
			// Billboards face the view: the camera's right and up in the emitter's own frame.
			Quaternion inv = Quaternion::identity();
			if (auto t = transform()) inv = Quaternion::Inverse(t->rotation());
			auto* cam = Camera::main();
			Vector3 right = inv * (cam ? cam->transform->right() : Vector3::right());
			Vector3 up = inv * (cam ? cam->transform->up() : Vector3::up());
			for (const auto& p : particles)
			{
				float life = Mathf::Clamp01(p.age / p.lifetime);
				float size = p.size * (sizeOverLifetime.enabled ? sizeOverLifetime.size.Evaluate(life, p.random) : 1.0f);
				Color col = p.color;
				if (colorOverLifetime.enabled) col = col * colorOverLifetime.color.Evaluate(life);
				Vector3 r = right * (size * 0.5f), u = up * (size * 0.5f);
				int i = static_cast<int>(v.size());
				v.push_back(p.position - r - u);
				v.push_back(p.position + r - u);
				v.push_back(p.position + r + u);
				v.push_back(p.position - r + u);
				for (int k = 0; k < 4; k++) c.push_back(col);
				uv.emplace_back(0, 0);
				uv.emplace_back(1, 0);
				uv.emplace_back(1, 1);
				uv.emplace_back(0, 1);
				// Clockwise as the camera sees it (Unity's front face); the glow material is two-sided anyway.
				tri.insert(tri.end(), { i, i + 2, i + 1, i, i + 3, i + 2 });
			}
		}
		mesh->Clear();
		mesh->MarkDynamic();
		mesh->SetVertices(v);
		mesh->SetColors(c);
		mesh->SetUVs(0, uv);
		mesh->SetTriangles(tri, 0);
	}

	cs::Ref<Component> ParticleSystem::Clone() const
	{
		auto c = cs::New<ParticleSystem>();
		c->main = main;
		c->emission = emission;
		c->shape = shape;
		c->colorOverLifetime = colorOverLifetime;
		c->sizeOverLifetime = sizeOverLifetime;
		return c;
	}

	cs::Ref<Mesh> ParticleSystemRenderer::DrawnMesh() const
	{
		auto ps = GetComponent<ParticleSystem>(gameObject());
		return ps ? ps->mesh : nullptr;
	}

	cs::Ref<Component> ParticleSystemRenderer::Clone() const
	{
		auto c = cs::New<ParticleSystemRenderer>();
		CopyRendererTo(*c);
		c->renderMode = renderMode;
		c->alignment = alignment;
		return c;
	}
}
