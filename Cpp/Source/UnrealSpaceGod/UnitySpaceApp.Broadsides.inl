// UnitySpaceApp.Broadsides.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Broadsides.cs; bodies in UnitySpaceApp.Broadsides.cpp.
//
// <summary>
// The broadside game at a capital ship's helm (U.SPC-T425, see <see cref="Broadsides"/>). The trigger still calls the
// volley with the guns that bear where the player is looking (U.SPC-T272); the number keys give the captain's orders: 1
// steps the battery bearing where the player looks, every gun whose fire cone takes that line in (U.SPC-T533), to its
// next order (at will, hold fire, volley), 2 the gun crews' fire spread over the enemy's hull or (again, the default) focused on the patch of it whose shield
// field is weakest (U.SPC-T501), 3 brace, 4 damage control's next focus,
// 5-8 the line into line ahead, line abreast, echelon or a screen, 9 engage at will, 0 every battery back to fire at will.
// The number keys answer a hail first, while one is open, then the boarding panel (B), while that is open.
// Shift+Delete orders the helm assist to roll the hull's weak shield patches away from the heaviest fire (U.SPC-T502,
// <see cref="WeakPatches"/>), handled with the other helm toggles in UpdatePilotBattle.
// </summary>

private:
	void UpdateHelmCommand(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& unit);

	void OrderLine(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& flag, Core::FleetFormation formation);
