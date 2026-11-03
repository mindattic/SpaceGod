#pragma once

// Unity's ParticleSystem as ShipView's exhaust uses it (US-T30): looping, local simulation space with hierarchy
// scaling, a lifetime and speed, a start size between two constants, a start colour, a rate over time, a cone shape,
// colour over lifetime (a two-key gradient with alpha) and size over lifetime (a linear curve), drawn as view-facing
// billboards with the renderer's material. Simulated on the CPU each frame (UnityScene::BeginFrame) into the
// renderer's own quad mesh, in the emitter's local space; the quads face the camera.

#include "UnityCompat/UnityRendering.h"

namespace SpaceGod::UnityCompat
{
	enum class ParticleSystemSimulationSpace : int { Local = 0, World = 1, Custom = 2 };
	enum class ParticleSystemScalingMode : int { Hierarchy = 0, Local = 1, Shape = 2 };
	enum class ParticleSystemShapeType : int { Sphere = 0, Hemisphere = 2, Cone = 4, Box = 5 };
	enum class ParticleSystemRenderMode : int { Billboard = 0, Stretch = 1, HorizontalBillboard = 2, VerticalBillboard = 3, Mesh = 4 };
	enum class ParticleSystemRenderSpace : int { View = 0, World = 1, Local = 2, Facing = 3, Velocity = 4 };
	enum class ParticleSystemStopBehavior : int { StopEmittingAndClear = 0, StopEmitting = 1 };

	struct GradientColorKey
	{
		Color color;
		float time = 0;
		GradientColorKey(Color c, float t) : color(c), time(t) {}
	};

	struct GradientAlphaKey
	{
		float alpha = 1;
		float time = 0;
		GradientAlphaKey(float a, float t) : alpha(a), time(t) {}
	};

	/// UnityEngine.Gradient (blend mode Blend).
	class Gradient
	{
	public:
		void SetKeys(const std::vector<GradientColorKey>& colors, const std::vector<GradientAlphaKey>& alphas)
		{
			colorKeys = colors;
			alphaKeys = alphas;
		}
		Color Evaluate(float t) const;

	private:
		std::vector<GradientColorKey> colorKeys;
		std::vector<GradientAlphaKey> alphaKeys;
	};

	/// AnimationCurve.Linear(t0, v0, t1, v1).
	struct AnimationCurve
	{
		float t0 = 0, v0 = 1, t1 = 1, v1 = 1;
		static AnimationCurve Linear(float timeStart, float valueStart, float timeEnd, float valueEnd) { return { timeStart, valueStart, timeEnd, valueEnd }; }
		float Evaluate(float t) const
		{
			if (t1 == t0) return v0;
			float u = Mathf::Clamp01((t - t0) / (t1 - t0));
			return v0 + (v1 - v0) * u;
		}
	};

	/// ParticleSystem.MinMaxCurve: a constant, two constants (random between), or a curve times a multiplier.
	struct MinMaxCurve
	{
		enum class Mode : int { Constant, TwoConstants, Curve } mode = Mode::Constant;
		float constantMin = 0, constantMax = 0;
		float multiplier = 1;
		AnimationCurve curve;
		MinMaxCurve() = default;
		MinMaxCurve(float c) : mode(Mode::Constant), constantMin(c), constantMax(c) {}
		MinMaxCurve(float min, float max) : mode(Mode::TwoConstants), constantMin(min), constantMax(max) {}
		MinMaxCurve(float m, AnimationCurve c) : mode(Mode::Curve), multiplier(m), curve(c) {}
		/// A value at normalised time t (lifetime fraction) and random lerp r.
		float Evaluate(float t, float r) const
		{
			switch (mode)
			{
				case Mode::TwoConstants: return constantMin + (constantMax - constantMin) * r;
				case Mode::Curve: return multiplier * curve.Evaluate(t);
				default: return constantMin;
			}
		}
	};

	class ParticleSystemRenderer;

	/// UnityEngine.ParticleSystem (the modules used: main, emission, shape, colorOverLifetime, sizeOverLifetime).
	class ParticleSystem final : public Component
	{
	public:
		struct MainModule
		{
			bool loop = true;
			bool playOnAwake = true;
			ParticleSystemSimulationSpace simulationSpace = ParticleSystemSimulationSpace::Local;
			ParticleSystemScalingMode scalingMode = ParticleSystemScalingMode::Local;
			MinMaxCurve startLifetime = 5.0f;
			MinMaxCurve startSpeed = 5.0f;
			MinMaxCurve startSize = 1.0f;
			Color startColor = Color::white();
			int maxParticles = 1000;
		} main;

		struct EmissionModule
		{
			bool enabled = true;
			MinMaxCurve rateOverTime = 10.0f;
		} emission;

		struct ShapeModule
		{
			bool enabled = true;
			ParticleSystemShapeType shapeType = ParticleSystemShapeType::Cone;
			float angle = 25;
			float radius = 1;
		} shape;

		struct ColorOverLifetimeModule
		{
			bool enabled = false;
			Gradient color;
		} colorOverLifetime;

		struct SizeOverLifetimeModule
		{
			bool enabled = false;
			MinMaxCurve size = 1.0f;
		} sizeOverLifetime;

		bool isPlaying() const { return playing; }
		void Play();
		/// Stop(true, StopEmitting) by default: live particles fade out; StopEmittingAndClear removes them now.
		void Stop(bool withChildren = true, ParticleSystemStopBehavior behavior = ParticleSystemStopBehavior::StopEmitting);
		void Clear();
		int particleCount() const { return static_cast<int>(particles.size()); }

		/// Steps the simulation dt real seconds and rebuilds the renderer's billboards for the camera.
		void Simulate(float dt);

		cs::Ref<Component> Clone() const override;

		/// The billboard mesh drawn by this system's ParticleSystemRenderer.
		cs::Ref<Mesh> mesh = cs::New<Mesh>("Particles");

	private:
		struct Particle
		{
			Vector3 position, velocity;
			float age = 0, lifetime = 1, size = 1, random = 0;
			Color color;
		};
		std::vector<Particle> particles;
		bool playing = false;
		float emitDebt = 0;
		float time = 0;
		void Emit(int count);
		void BuildMesh();
	};

	/// UnityEngine.ParticleSystemRenderer: added with the ParticleSystem (Unity adds it as a required component).
	class ParticleSystemRenderer final : public Renderer
	{
	public:
		ParticleSystemRenderMode renderMode = ParticleSystemRenderMode::Billboard;
		ParticleSystemRenderSpace alignment = ParticleSystemRenderSpace::View;
		cs::Ref<Mesh> DrawnMesh() const override;
		cs::Ref<Component> Clone() const override;
	};

	/// `go.AddComponent<ParticleSystem>()`: also adds its ParticleSystemRenderer, as Unity does.
	template <> inline cs::Ref<ParticleSystem> AddComponent<ParticleSystem>(const cs::Ref<GameObject>& go)
	{
		auto c = cs::New<ParticleSystem>();
		c->owner = go;
		auto& bag = SceneRegistry::Get().BagOf(go);
		bag.Components.push_back(c);
		auto r = cs::New<ParticleSystemRenderer>();
		r->owner = go;
		bag.Components.push_back(r);
		return c;
	}
}
