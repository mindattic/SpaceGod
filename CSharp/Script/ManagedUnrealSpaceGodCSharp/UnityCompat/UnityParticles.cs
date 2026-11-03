using System;
using System.Collections.Generic;

namespace UnityEngine
{
    public enum ParticleSystemSimulationSpace { Local = 0, World = 1, Custom = 2 }
    public enum ParticleSystemScalingMode { Hierarchy = 0, Local = 1, Shape = 2 }
    public enum ParticleSystemShapeType { Sphere = 0, Hemisphere = 2, Cone = 4, Box = 5, Circle = 10 }
    public enum ParticleSystemStopBehavior { StopEmittingAndClear = 0, StopEmitting = 1 }
    public enum ParticleSystemRenderMode { Billboard = 0, Stretch = 1, HorizontalBillboard = 2, VerticalBillboard = 3, Mesh = 4, None = 5 }
    public enum ParticleSystemRenderSpace { View = 0, World = 1, Local = 2, Facing = 3, Velocity = 4 }
    public enum ParticleSystemCurveMode { Constant = 0, Curve = 1, TwoCurves = 2, TwoConstants = 3 }

    public struct Keyframe
    {
        public float time, value;
        public Keyframe(float time, float value) { this.time = time; this.value = value; }
    }

    /// <summary>A piecewise-linear AnimationCurve (Unity's tangents are not kept: the ported code only builds Linear ones).</summary>
    public sealed class AnimationCurve
    {
        public Keyframe[] keys;
        public AnimationCurve(params Keyframe[] keys) { this.keys = keys; }
        public static AnimationCurve Linear(float timeStart, float valueStart, float timeEnd, float valueEnd) =>
            new AnimationCurve(new Keyframe(timeStart, valueStart), new Keyframe(timeEnd, valueEnd));
        public float Evaluate(float t)
        {
            if (keys.Length == 0) return 0;
            if (t <= keys[0].time) return keys[0].value;
            for (int i = 1; i < keys.Length; i++)
            {
                if (t <= keys[i].time)
                {
                    var a = keys[i - 1];
                    var b = keys[i];
                    float span = b.time - a.time;
                    return span > 0 ? Mathf.LerpUnclamped(a.value, b.value, (t - a.time) / span) : b.value;
                }
            }
            return keys[keys.Length - 1].value;
        }
    }

    public struct GradientColorKey
    {
        public Color color;
        public float time;
        public GradientColorKey(Color col, float time) { color = col; this.time = time; }
    }

    public struct GradientAlphaKey
    {
        public float alpha, time;
        public GradientAlphaKey(float alpha, float time) { this.alpha = alpha; this.time = time; }
    }

    public sealed class Gradient
    {
        GradientColorKey[] colors = { new GradientColorKey(Color.white, 0), new GradientColorKey(Color.white, 1) };
        GradientAlphaKey[] alphas = { new GradientAlphaKey(1, 0), new GradientAlphaKey(1, 1) };
        public void SetKeys(GradientColorKey[] colorKeys, GradientAlphaKey[] alphaKeys) { colors = colorKeys; alphas = alphaKeys; }
        public Color Evaluate(float t)
        {
            Color c = colors[colors.Length - 1].color;
            if (t <= colors[0].time) c = colors[0].color;
            else
            {
                for (int i = 1; i < colors.Length; i++)
                    if (t <= colors[i].time)
                    {
                        float s = colors[i].time - colors[i - 1].time;
                        c = Color.LerpUnclamped(colors[i - 1].color, colors[i].color, s > 0 ? (t - colors[i - 1].time) / s : 1);
                        break;
                    }
            }
            float a = alphas[alphas.Length - 1].alpha;
            if (t <= alphas[0].time) a = alphas[0].alpha;
            else
            {
                for (int i = 1; i < alphas.Length; i++)
                    if (t <= alphas[i].time)
                    {
                        float s = alphas[i].time - alphas[i - 1].time;
                        a = Mathf.LerpUnclamped(alphas[i - 1].alpha, alphas[i].alpha, s > 0 ? (t - alphas[i - 1].time) / s : 1);
                        break;
                    }
            }
            c.a = a;
            return c;
        }
    }

    /// <summary>
    /// A CPU particle system with the parts of Unity's the ported code configures (ShipView's exhaust: a looping cone
    /// emitter with start lifetime/speed/size/colour, rate over time, colour and size over lifetime, local simulation
    /// space with hierarchy scaling, view-facing billboards). Its renderer builds camera-facing quads in world space each
    /// frame, drawn by UnityBridge like any mesh. Unity's own random sequence is not reproduced (it is not part of the
    /// simulation).
    /// </summary>
    public sealed class ParticleSystem : Component
    {
        public struct MinMaxCurve
        {
            public ParticleSystemCurveMode mode;
            public float constantMin, constantMax, curveMultiplier;
            public AnimationCurve? curve;
            public MinMaxCurve(float constant) { mode = ParticleSystemCurveMode.Constant; constantMin = constantMax = constant; curveMultiplier = 1; curve = null; }
            public MinMaxCurve(float min, float max) { mode = ParticleSystemCurveMode.TwoConstants; constantMin = min; constantMax = max; curveMultiplier = 1; curve = null; }
            public MinMaxCurve(float multiplier, AnimationCurve curve) { mode = ParticleSystemCurveMode.Curve; curveMultiplier = multiplier; this.curve = curve; constantMin = constantMax = 0; }
            public float constant { get => constantMax; set { constantMin = constantMax = value; mode = ParticleSystemCurveMode.Constant; } }
            public static implicit operator MinMaxCurve(float v) => new MinMaxCurve(v);
            internal float Sample(float t, float random) => mode switch
            {
                ParticleSystemCurveMode.Curve => curveMultiplier * (curve?.Evaluate(t) ?? 1),
                ParticleSystemCurveMode.TwoConstants => Mathf.LerpUnclamped(constantMin, constantMax, random),
                _ => constantMax,
            };
        }

        public struct MinMaxGradient
        {
            public Color color;
            public Gradient? gradient;
            public MinMaxGradient(Color c) { color = c; gradient = null; }
            public MinMaxGradient(Gradient g) { color = Color.white; gradient = g; }
            public static implicit operator MinMaxGradient(Color c) => new MinMaxGradient(c);
            public static implicit operator MinMaxGradient(Gradient g) => new MinMaxGradient(g);
            internal Color Sample(float t) => gradient != null ? gradient.Evaluate(t) : color;
        }

        public sealed class MainModule
        {
            public bool loop = true, playOnAwake = true;
            public ParticleSystemSimulationSpace simulationSpace;
            public ParticleSystemScalingMode scalingMode;
            public MinMaxCurve startLifetime = 5, startSpeed = 5, startSize = 1;
            public MinMaxGradient startColor = Color.white;
            public int maxParticles = 1000;
            public float duration = 5;
        }
        public sealed class EmissionModule
        {
            public bool enabled = true;
            public MinMaxCurve rateOverTime = 10;
        }
        public sealed class ShapeModule
        {
            public bool enabled = true;
            public ParticleSystemShapeType shapeType = ParticleSystemShapeType.Cone;
            public float angle = 25, radius = 1;
        }
        public sealed class ColorOverLifetimeModule
        {
            public bool enabled;
            public MinMaxGradient color = Color.white;
        }
        public sealed class SizeOverLifetimeModule
        {
            public bool enabled;
            public MinMaxCurve size = 1;
        }

        public MainModule main { get; } = new MainModule();
        public EmissionModule emission { get; } = new EmissionModule();
        public ShapeModule shape { get; } = new ShapeModule();
        public ColorOverLifetimeModule colorOverLifetime { get; } = new ColorOverLifetimeModule();
        public SizeOverLifetimeModule sizeOverLifetime { get; } = new SizeOverLifetimeModule();

        struct Particle
        {
            public Vector3 Position, Velocity;
            public float Age, Lifetime, Size;
            public Color Color;
        }

        readonly List<Particle> particles = new List<Particle>();
        readonly System.Random rng = new System.Random(17);
        float emitDebt;
        bool emitting;
        public bool isPlaying => emitting;
        public bool isEmitting => emitting;
        public int particleCount => particles.Count;

        internal override void OnAddedInternal()
        {
            if (gameObject.GetComponent<ParticleSystemRenderer>() == null) gameObject.AddComponent<ParticleSystemRenderer>();
            UnityBridge.Register(this);
            if (main.playOnAwake) emitting = true;
        }
        internal override void OnDestroyInternal()
        {
            if (destroyed) return;
            base.OnDestroyInternal();
            UnityBridge.Unregister(this);
        }

        public void Play() => Play(true);
        public void Play(bool withChildren) => emitting = true;
        public void Stop() => Stop(true, ParticleSystemStopBehavior.StopEmitting);
        public void Stop(bool withChildren) => Stop(withChildren, ParticleSystemStopBehavior.StopEmitting);
        public void Stop(bool withChildren, ParticleSystemStopBehavior behavior)
        {
            emitting = false;
            emitDebt = 0;
            if (behavior == ParticleSystemStopBehavior.StopEmittingAndClear) particles.Clear();
        }
        public void Clear() => particles.Clear();

        float Rand() => (float)rng.NextDouble();

        /// <summary>Advances the simulation (UnityBridge, once a frame, for active systems).</summary>
        internal void Simulate(float dt)
        {
            for (int i = particles.Count - 1; i >= 0; i--)
            {
                var p = particles[i];
                p.Age += dt;
                if (p.Age >= p.Lifetime) { particles.RemoveAt(i); continue; }
                p.Position += p.Velocity * dt;
                particles[i] = p;
            }
            if (!emitting || !emission.enabled) return;
            emitDebt += emission.rateOverTime.Sample(0, Rand()) * dt;
            while (emitDebt >= 1f)
            {
                emitDebt -= 1f;
                if (particles.Count >= main.maxParticles) continue;
                // Cone along local +Z: a point on the base disc, heading out within the cone's angle.
                float a = Rand() * Mathf.PI * 2f;
                float r = Mathf.Sqrt(Rand());
                var onDisc = new Vector3(Mathf.Cos(a) * r, Mathf.Sin(a) * r, 0);
                var dir = shape.shapeType == ParticleSystemShapeType.Cone
                    ? (Vector3.forward + onDisc * Mathf.Tan(shape.angle * Mathf.Deg2Rad)).normalized
                    : Random.onUnitSphere;
                particles.Add(new Particle
                {
                    Position = onDisc * shape.radius,
                    Velocity = dir * main.startSpeed.Sample(0, Rand()),
                    Lifetime = Mathf.Max(1e-3f, main.startLifetime.Sample(0, Rand())),
                    Size = main.startSize.Sample(0, Rand()),
                    Color = main.startColor.Sample(Rand()),
                });
            }
        }

        /// <summary>Camera-facing quads in world space for every live particle, into <paramref name="mesh"/>.</summary>
        internal void BuildMesh(Mesh mesh, Vector3 cameraPosition, Quaternion cameraRotation)
        {
            int n = particles.Count;
            var v = new Vector3[n * 4];
            var uv = new Vector2[n * 4];
            var c = new Color[n * 4];
            var t = new int[n * 6];
            var right = cameraRotation * Vector3.right;
            var up = cameraRotation * Vector3.up;
            var scale = transform.lossyScale;
            float hierarchy = main.scalingMode == ParticleSystemScalingMode.Hierarchy ? (Mathf.Abs(scale.x) + Mathf.Abs(scale.y) + Mathf.Abs(scale.z)) / 3f : 1f;
            for (int i = 0; i < n; i++)
            {
                var p = particles[i];
                float life = p.Age / p.Lifetime;
                var world = main.simulationSpace == ParticleSystemSimulationSpace.Local ? transform.TransformPoint(p.Position) : p.Position;
                float size = p.Size * hierarchy * (sizeOverLifetime.enabled ? sizeOverLifetime.size.Sample(life, 0) : 1f) * 0.5f;
                var col = p.Color * (colorOverLifetime.enabled ? colorOverLifetime.color.Sample(life) : Color.white);
                var dx = right * size;
                var dy = up * size;
                v[i * 4] = world - dx - dy; v[i * 4 + 1] = world + dx - dy; v[i * 4 + 2] = world - dx + dy; v[i * 4 + 3] = world + dx + dy;
                uv[i * 4] = new Vector2(0, 0); uv[i * 4 + 1] = new Vector2(1, 0); uv[i * 4 + 2] = new Vector2(0, 1); uv[i * 4 + 3] = new Vector2(1, 1);
                for (int k = 0; k < 4; k++) c[i * 4 + k] = col;
                t[i * 6] = i * 4; t[i * 6 + 1] = i * 4 + 2; t[i * 6 + 2] = i * 4 + 1;
                t[i * 6 + 3] = i * 4 + 1; t[i * 6 + 4] = i * 4 + 2; t[i * 6 + 5] = i * 4 + 3;
            }
            mesh.Clear();
            mesh.vertices = v;
            mesh.uv = uv;
            mesh.colors = c;
            mesh.triangles = t;
        }
    }

    public sealed class ParticleSystemRenderer : Renderer
    {
        public ParticleSystemRenderMode renderMode { get; set; }
        public ParticleSystemRenderSpace alignment { get; set; }
        readonly Mesh particleMesh = new Mesh { name = "Particles" };
        internal override Mesh? DrawMesh => particleMesh;
        internal override bool WorldSpaceGeometry => true;

        /// <summary>Rebuilds the quads for this frame (UnityBridge, before drawing).</summary>
        internal void Rebuild(Vector3 cameraPosition, Quaternion cameraRotation)
        {
            var ps = GetComponent<ParticleSystem>();
            if (ps == null) return;
            if (ps.particleCount == 0 && particleMesh.vertexCount == 0) return;
            ps.BuildMesh(particleMesh, cameraPosition, cameraRotation);
        }
    }
}
