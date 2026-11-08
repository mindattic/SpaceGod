#pragma once

// US-T245: the main menu's backdrop beyond the turning starfield, and the logo its heading draws.

#include "UnityCompat/UnityRenderingTypes.h"
#include "UnityCompat/UnityTexture.h"

namespace SpaceGod::Views
{
	class BodyView;
}

namespace SpaceGod::UI
{
	/// <summary>
	/// US-T245: life behind the main menu, with no game loaded: Earth hangs low in the right of the view, lit from the
	/// upper left and turning on its tilted axis, drifting slowly against the turning stars as if the view were coasting
	/// past it; and every so often a ship (a procedural hull, a frigate or a freighter, its engines glowing) crosses the
	/// sky from one side to the other. Everything is placed relative to the menu camera each frame, so it stays framed
	/// while the sky turns; the star's light is the menu's own (RenderBackend's lighting, set every frame).
	/// </summary>
	class MenuBackdrop final
	{
	public:
		/// Builds the planet and the ship under <paramref name="parent"/> (the menu's transform).
		explicit MenuBackdrop(const cs::Ref<Transform>& parent);
		~MenuBackdrop();
		MenuBackdrop(const MenuBackdrop&) = delete;
		MenuBackdrop& operator=(const MenuBackdrop&) = delete;

		/// Once a frame (the menu's Update): places the planet, the ship and the light about the camera.
		void Update(Camera* camera, float time);

		/// The menu scene is going: the planet's and the glow's own materials go with it (the ship's is shared).
		void Destroy();

		/// The SpaceGod logo (Content/Brand/logo.png, or the repo's assets/logo.png in a dev tree), its black lines made
		/// white on clear so it reads over the dark sky; null when neither file is there.
		static cs::Ref<Texture2D> LoadLogo();

	private:
		cs::Ref<GameObject> root;
		std::unique_ptr<Views::BodyView> planet;
		cs::Ref<GameObject> ship;
		cs::Ref<MeshFilter> shipMesh;
		cs::Ref<Transform> glow;
		cs::Ref<Material> glowMaterial;

		/// The ship's current pass: when it began, which way it flies (+1 left to right) and which hull it is.
		float passStart = 0;
		float side = 1;
		int pass = -1;

		void StartPass(int index);
	};
}
