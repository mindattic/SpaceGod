// US-T44: CoopInput.cs (US-T43) in C++; see CoopInput.h.

#include "Coop/CoopInput.h"

#include "Coop/CoopWire.h"
#include "Input/InputState.h"

#include <cstring>
#include <stdexcept>

namespace SpaceGod::Coop
{
	namespace
	{
		enum class Kind { Bool, Int, Float, Vec2, Vec3 };

		struct Field
		{
			const char* Name;
			Kind Type;
			std::function<PilotValue(const Input::InputState&)> Get;
			std::function<void(Input::InputState&, const PilotValue&)> Set;
		};

		PilotValue Of(bool v) { PilotValue p; p.B = v; return p; }
		PilotValue Of(int v) { PilotValue p; p.I = v; return p; }
		PilotValue Of(float v) { PilotValue p; p.F[0] = v; return p; }
		PilotValue Of(const Vector2& v) { PilotValue p; p.F[0] = v.x; p.F[1] = v.y; return p; }
		PilotValue Of(const Vector3& v) { PilotValue p; p.F[0] = v.x; p.F[1] = v.y; p.F[2] = v.z; return p; }

		void Put(bool& v, const PilotValue& p) { v = p.B; }
		void Put(int& v, const PilotValue& p) { v = p.I; }
		void Put(float& v, const PilotValue& p) { v = p.F[0]; }
		void Put(Vector2& v, const PilotValue& p) { v = Vector2(p.F[0], p.F[1]); }
		void Put(Vector3& v, const PilotValue& p) { v = Vector3(p.F[0], p.F[1], p.F[2]); }

		template <class T> constexpr Kind KindOf()
		{
			if constexpr (std::is_same_v<T, bool>) return Kind::Bool;
			else if constexpr (std::is_same_v<T, int>) return Kind::Int;
			else if constexpr (std::is_same_v<T, float>) return Kind::Float;
			else if constexpr (std::is_same_v<T, Vector2>) return Kind::Vec2;
			else return Kind::Vec3;
		}

#define SG_FIELD(name) \
		Field{ #name, KindOf<std::decay_t<decltype(std::declval<Input::InputState&>().name)>>(), \
			[](const Input::InputState& s) { return Of(s.name); }, \
			[](Input::InputState& s, const PilotValue& p) { Put(s.name, p); } }

		/// InputState's public bool / int / float / Vector2 / Vector3 fields in ordinal name order (C#: Collect()).
		const std::vector<Field>& Fields()
		{
			static const std::vector<Field> fields = {
				SG_FIELD(ActivateGate), SG_FIELD(Boost), SG_FIELD(ClearFtlCourse), SG_FIELD(ClearScreen), SG_FIELD(Cruise),
				SG_FIELD(CycleBackward), SG_FIELD(CycleFtlTarget), SG_FIELD(CycleSeat), SG_FIELD(CycleShips), SG_FIELD(CycleSky),
				SG_FIELD(DropMine), SG_FIELD(EngageFtl), SG_FIELD(Escape), SG_FIELD(Fire), SG_FIELD(FireEmp), SG_FIELD(Focus),
				SG_FIELD(HailPolity), SG_FIELD(Home), SG_FIELD(JumpToCraft), // US-T106: Shift+/
				SG_FIELD(LockTarget), SG_FIELD(LookDelta), SG_FIELD(LookHeld), SG_FIELD(ManGun),
				SG_FIELD(Manoeuvre), SG_FIELD(MouseDelta), SG_FIELD(MousePosition), SG_FIELD(Move), SG_FIELD(MoveToHost), SG_FIELD(NumberKey), SG_FIELD(Pilot),
				SG_FIELD(PowerKey), // US-T94: the reactor split's arrow keys, an edge
				SG_FIELD(QuickLoad), SG_FIELD(QuickSave), SG_FIELD(ResetTime), SG_FIELD(RightClicked), SG_FIELD(Roll), SG_FIELD(Scroll),
				SG_FIELD(SelectClicked), SG_FIELD(SelectDoubleClicked),
				SG_FIELD(SelfDestructArm), SG_FIELD(SelfDestructHoldS), SG_FIELD(SelfDestructTap), // US-T97: the self-destruct's key
				SG_FIELD(Slow), SG_FIELD(SpawnPractice), SG_FIELD(ThrottleStep),
				SG_FIELD(TimeFaster), SG_FIELD(TimeSlower), SG_FIELD(ToggleBoarding), SG_FIELD(ToggleBounties), SG_FIELD(ToggleCargo),
				SG_FIELD(ToggleContracts), SG_FIELD(ToggleFleet), SG_FIELD(ToggleFlightAssist), SG_FIELD(ToggleFtlAutopilot),
				SG_FIELD(ToggleGalaxyMap), SG_FIELD(ToggleHelp), SG_FIELD(ToggleHud), SG_FIELD(ToggleLabels), SG_FIELD(ToggleLanes),
				SG_FIELD(ToggleMarket), SG_FIELD(ToggleNews), SG_FIELD(ToggleOrbits), SG_FIELD(ToggleOutfit), SG_FIELD(TogglePause),
				SG_FIELD(TogglePicker), SG_FIELD(ToggleReorient), SG_FIELD(ToggleRollPatches), SG_FIELD(ToggleRunHot),
				SG_FIELD(ToggleSatellites), SG_FIELD(ToggleSaves), SG_FIELD(ToggleScale), SG_FIELD(ToggleSilent), SG_FIELD(ToggleSpawn),
				SG_FIELD(ToggleTrade), SG_FIELD(TunePhasers), SG_FIELD(WatchBattle),
			};
			return fields;
		}
#undef SG_FIELD

		/// Held from one message to the next, as a stick stays where it is put (the rest are edges, deltas or picks).
		bool Held(const char* n)
		{
			for (const char* h : { "Move", "Roll", "Boost", "Slow", "Cruise", "Manoeuvre", "Fire", "FireEmp", "LookHeld", "MousePosition" })
				if (std::strcmp(n, h) == 0) return true;
			return false;
		}

		/// Summed over the messages a host frame takes in.
		bool Summed(const char* n) { return std::strcmp(n, "MouseDelta") == 0 || std::strcmp(n, "LookDelta") == 0 || std::strcmp(n, "Scroll") == 0; }

		PilotValue Default(const Field& f)
		{
			PilotValue p;
			if (std::strcmp(f.Name, "NumberKey") == 0) p.I = -1;
			return p;
		}

		cs::String Esc(const cs::String& key)
		{
			if (key.empty()) return "n";
			cs::String s = key;
			for (auto& c : s) if (c == ' ') c = '|';
			return s;
		}

		cs::String Unesc(cs::String s)
		{
			for (auto& c : s) if (c == '|') c = ' ';
			return s;
		}

		void CopyContext(PilotFrame& to, const PilotFrame& from)
		{
			to.Sensitivity = from.Sensitivity;
			to.PanelOpen = from.PanelOpen;
			to.Selected = from.Selected;
			to.Hovered = from.Hovered;
			to.UpFrame = from.UpFrame;
		}
	}

	PilotFrame PilotFrame::Idle()
	{
		PilotFrame p;
		for (const auto& f : Fields()) p.Values.push_back(Default(f));
		return p;
	}

	PilotFrame PilotFrame::Of(const Input::InputState& input, const std::function<float(float)>& lookY)
	{
		PilotFrame p;
		for (const auto& f : Fields())
		{
			PilotValue v = f.Get(input);
			if (std::strcmp(f.Name, "MouseDelta") == 0 && lookY) v.F[1] = lookY(v.F[1]);
			p.Values.push_back(v);
		}
		return p;
	}

	void PilotFrame::ApplyTo(Input::InputState& input, const std::function<float(float)>& lookY) const
	{
		const auto& fields = Fields();
		for (size_t i = 0; i < fields.size(); i++)
		{
			PilotValue v = Values[i];
			if (std::strcmp(fields[i].Name, "MouseDelta") == 0 && lookY) v.F[1] = lookY(v.F[1]);
			fields[i].Set(input, v);
		}
	}

	std::vector<PilotValue> PilotFrame::Snapshot(const Input::InputState& input)
	{
		std::vector<PilotValue> v;
		for (const auto& f : Fields()) v.push_back(f.Get(input));
		return v;
	}

	void PilotFrame::Restore(Input::InputState& input, const std::vector<PilotValue>& snapshot)
	{
		const auto& fields = Fields();
		for (size_t i = 0; i < fields.size(); i++) fields[i].Set(input, snapshot[i]);
	}

	PilotFrame PilotFrame::Compose(const PilotFrame* last, const std::vector<PilotFrame>& since)
	{
		PilotFrame p = Idle();
		const PilotFrame* basis = !since.empty() ? &since.back() : last;
		const auto& fields = Fields();
		if (basis != nullptr)
		{
			CopyContext(p, *basis);
			for (size_t i = 0; i < fields.size(); i++)
				if (Held(fields[i].Name)) p.Values[i] = basis->Values[i];
		}
		for (const auto& m : since)
			for (size_t i = 0; i < fields.size(); i++)
			{
				const auto& f = fields[i];
				if (Held(f.Name)) continue;
				if (Summed(f.Name))
				{
					if (f.Type == Kind::Float) p.Values[i].F[0] = p.Values[i].F[0] + m.Values[i].F[0];
					else if (f.Type == Kind::Vec2)
					{
						// (Vector2)a + (Vector2)b, as Unity adds them: per component in float.
						p.Values[i].F[0] = p.Values[i].F[0] + m.Values[i].F[0];
						p.Values[i].F[1] = p.Values[i].F[1] + m.Values[i].F[1];
					}
				}
				else if (f.Type == Kind::Bool) p.Values[i].B = p.Values[i].B || m.Values[i].B;
				else if (f.Type == Kind::Int)
				{
					if (!(m.Values[i] == Default(f))) p.Values[i] = m.Values[i];
				}
				else p.Values[i] = m.Values[i];
			}
		return p;
	}

	PilotFrame PilotFrame::HeldOnly() const
	{
		PilotFrame p = Idle();
		CopyContext(p, *this);
		const auto& fields = Fields();
		for (size_t i = 0; i < fields.size(); i++)
			if (Held(fields[i].Name)) p.Values[i] = Values[i];
		return p;
	}

	bool PilotFrame::AnyEdge() const
	{
		const auto& fields = Fields();
		for (size_t i = 0; i < fields.size(); i++)
			if (!Held(fields[i].Name) && !(Values[i] == Default(fields[i]))) return true;
		return false;
	}

	cs::String PilotFrame::Write() const
	{
		cs::String s;
		s.reserve(512);
		s += cs::Cat(Wire::F(Sensitivity), " ", PanelOpen ? "1" : "0", " ", Esc(Selected), " ", Esc(Hovered), " ",
			Wire::F(UpFrame.x), ",", Wire::F(UpFrame.y), ",", Wire::F(UpFrame.z), ",", Wire::F(UpFrame.w));
		const auto& fields = Fields();
		for (size_t i = 0; i < fields.size(); i++)
		{
			s += ' ';
			const auto& v = Values[i];
			switch (fields[i].Type)
			{
				case Kind::Bool: s += v.B ? '1' : '0'; break;
				case Kind::Int: s += Wire::I(v.I); break;
				case Kind::Float: s += Wire::F(v.F[0]); break;
				case Kind::Vec2: s += cs::Cat(Wire::F(v.F[0]), ",", Wire::F(v.F[1])); break;
				case Kind::Vec3: s += cs::Cat(Wire::F(v.F[0]), ",", Wire::F(v.F[1]), ",", Wire::F(v.F[2])); break;
			}
		}
		return s;
	}

	PilotFrame PilotFrame::Read(const cs::String& text)
	{
		const auto& fields = Fields();
		auto f = Wire::Split(text, ' ');
		if (f.size() != 5 + fields.size())
			throw std::runtime_error(cs::Cat("pilot frame has ", static_cast<int>(f.size()), " fields, ", static_cast<int>(5 + fields.size()), " expected"));
		PilotFrame p;
		p.Sensitivity = Wire::ParseF(f[0]);
		p.PanelOpen = f[1] == "1";
		p.Selected = Unesc(f[2]);
		p.Hovered = Unesc(f[3]);
		auto q = Wire::Split(f[4], ',');
		p.UpFrame = Quaternion(Wire::ParseF(q.at(0)), Wire::ParseF(q.at(1)), Wire::ParseF(q.at(2)), Wire::ParseF(q.at(3)));
		for (size_t i = 0; i < fields.size(); i++)
		{
			const cs::String& s = f[5 + i];
			PilotValue v;
			switch (fields[i].Type)
			{
				case Kind::Bool: v.B = s == "1"; break;
				case Kind::Int: v.I = Wire::ParseI(s); break;
				case Kind::Float: v.F[0] = Wire::ParseF(s); break;
				case Kind::Vec2:
				case Kind::Vec3:
				{
					auto c = Wire::Split(s, ',');
					v.F[0] = Wire::ParseF(c.at(0));
					v.F[1] = Wire::ParseF(c.at(1));
					if (fields[i].Type == Kind::Vec3) v.F[2] = Wire::ParseF(c.at(2));
					break;
				}
			}
			p.Values.push_back(v);
		}
		return p;
	}
}
