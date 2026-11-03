#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/Materials.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Rendering
{

	/// <summary>
	/// The game's shaders, as the active render pipeline supplies them (U.SPC-T429: <see cref="RenderBackend"/> loads
	/// each from its pipeline's Resources folder, which also guarantees they ship in builds), and the materials made
	/// from them.
	/// </summary>
	class Materials
	{
	public:
		static cs::Ref<Shader> Planet();
		static cs::Ref<Shader> Sun();
		static cs::Ref<Shader> Glow();
		static cs::Ref<Shader> Ring();
		static cs::Ref<Shader> Ship();
		static cs::Ref<Shader> Line();
		static cs::Ref<Shader> Stars();
		static cs::Ref<Shader> GasGiant();
		static cs::Ref<Shader> Asteroids();
		static cs::Ref<Shader> Fx();
		static cs::Ref<Shader> Points();
		static cs::Ref<Shader> Sky();

		static cs::Ref<Material> Create(const cs::Ref<Shader>& shader, const cs::String& name);

		/// <summary>A mesh built for one view alone (a line, an effects mesh): destroyed with that view's system.</summary>
		static cs::Ref<Mesh> Own(const cs::Ref<Mesh>& mesh);

		/// <summary>Destroys one view-owned mesh now (a fighter's trail when it dies).</summary>
		static void DestroyMesh(const cs::Ref<Mesh>& mesh);

		/// <summary>A material cached for every view to share (it outlives any one star system's views).</summary>
		static cs::Ref<Material> MarkShared(const cs::Ref<Material>& m);

		/// <summary>
		/// Destroys the materials used under <paramref name="root"/> that belong only to those views
		/// (DontSave materials outlive their GameObjects otherwise), leaving the shared caches alone.
		/// </summary>
		static void DestroyOwned(const cs::Ref<Transform>& root);

		/// <summary>
		/// The game scene is going (back to the main menu): every material and view mesh made for it goes too, all but
		/// the shared caches, which the next game reuses. Unity destroys neither on a scene change (DontSave, and the
		/// meshes are held here), so each round trip through the menu would otherwise leave a whole system's worth behind.
		/// </summary>
		static void DestroyAllOwned();

	private:
		static cs::Ref<Shader> Load(const cs::String& name);
	};
}
