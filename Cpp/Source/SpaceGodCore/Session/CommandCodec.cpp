// Port of Unity.SpaceGod Core/Session/CommandCodec.cs (US-T21, see docs/MIGRATION.md).

#include "Session/CommandCodec.h"
#include "Session/Commands.h"

namespace SpaceGod::Core
{
	cs::String CommandCodec::Write(const cs::Ref<IGameCommand>& command)
	{
		if (auto t = cs::As<TakeSeatCommand>(command)) return cs::Cat("take ", Int(t->Player().Seat), " ", Int(t->Seat));
		if (auto r = cs::As<ReleaseSeatCommand>(command)) return cs::Cat("release ", Int(r->Player().Seat));
		if (auto h = cs::As<MoveToHostCommand>(command)) return cs::Cat("host ", Int(h->Player().Seat));
		if (auto c = cs::As<SeatControlCommand>(command))
		{
			const auto& i = c->Input;
			// 8: the manoeuvre key (US-T87), a turn-and-burn in a fighter seat.
			// 16 and up: the reactor split asked for plus one (US-T94, 0 for no change), so older lines read as no change.
			// 2048 and 4096: the self-destruct asked for (US-T97, 1 arm, 2 abort), above the split's seven bits.
			// 8192 and up: the helm ability ordered (US-T106, five bits, 0 for none).
			// 262144 and 524288: the sensor and power command (US-T93, 1 ping, 2 go dark, 3 power up, 0 for none).
			// 1048576: drift held (US-T165).
			// 2097152: the jump jets held (US-T83).
			int flags = (i.Primary ? 1 : 0) | (i.Secondary ? 2 : 0) | (i.Boost ? 4 : 0) | (i.Manoeuvre ? 8 : 0)
				| ((i.PowerSplit >= 0 ? i.PowerSplit + 1 : 0) << 4) | ((i.SelfDestruct & 3) << 11) | ((i.HelmAbility & 31) << 13)
				| ((i.Sensor & 3) << 18) | (i.Drift ? 1 << 20 : 0) | (i.JumpJets ? 1 << 21 : 0);
			return cs::Cat("control ", Int(c->Player().Seat), " ", Num(i.Aim.X), " ", Num(i.Aim.Y), " ", Num(i.Aim.Z), " ",
				Num(i.Throttle), " ", Num(i.StrafeRight), " ", Num(i.StrafeUp), " ", Num(i.Roll), " ", Int(flags));
		}
		// US-T137: something said over comms, to a hull's id or -(settlement id + 1).
		if (auto m = cs::As<CommsCommand>(command)) return cs::Cat("comms ", Int(m->Player().Seat), " ", Int(m->Target), " ", Int(m->Verb));
		// C#: throw new ArgumentException("No codec for " + (command?.GetType().Name ?? "null"), nameof(command));
		throw cs::ArgumentException(cs::Cat("No codec for ", command ? GameCommandTypeName(*command) : cs::String("null"), " (Parameter 'command')"));
	}

	cs::Ref<IGameCommand> CommandCodec::Read(const cs::String& line)
	{
		// C#: (line ?? "").Trim().Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)
		cs::List<cs::String> f;
		for (const auto& part : cs::Strings::Split(cs::Strings::Trim(line), ' '))
			if (!part.empty()) f.Add(part);
		if (f.Count() < 2) throw cs::FormatException(cs::Cat("Not a command: '", line, "'"));
		auto p = PlayerId(ParseInt(f[1]));
		if (f[0] == "take")
		{
			Need(f, 3, line);
			return cs::New<TakeSeatCommand>(p, ParseInt(f[2]));
		}
		if (f[0] == "release") return cs::New<ReleaseSeatCommand>(p);
		if (f[0] == "host") return cs::New<MoveToHostCommand>(p);
		if (f[0] == "control")
		{
			Need(f, 10, line);
			int flags = ParseInt(f[9]);
			// Each field parsed in C#'s order (the object initialiser runs left to right), so a bad field throws the same error.
			double aimX = ParseNum(f[2]);
			double aimY = ParseNum(f[3]);
			double aimZ = ParseNum(f[4]);
			EmbodimentInput input;
			input.Aim = DVec3(aimX, aimY, aimZ);
			input.Throttle = ParseNum(f[5]);
			input.StrafeRight = ParseNum(f[6]);
			input.StrafeUp = ParseNum(f[7]);
			input.Roll = ParseNum(f[8]);
			input.Primary = (flags & 1) != 0;
			input.Secondary = (flags & 2) != 0;
			input.Boost = (flags & 4) != 0;
			input.Manoeuvre = (flags & 8) != 0;
			input.PowerSplit = ((flags >> 4) & 0x7F) - 1;
			input.SelfDestruct = (flags >> 11) & 3; // US-T97
			input.HelmAbility = (flags >> 13) & 31; // US-T106
			input.Sensor = (flags >> 18) & 3; // US-T93
			input.Drift = (flags & (1 << 20)) != 0; // US-T165
			input.JumpJets = (flags & (1 << 21)) != 0; // US-T83

			return cs::New<SeatControlCommand>(p, input);
		}
		if (f[0] == "comms")
		{
			Need(f, 4, line);
			return cs::New<CommsCommand>(p, ParseInt(f[2]), ParseInt(f[3])); // US-T137
		}
		throw cs::FormatException(cs::Cat("Unknown command '", f[0], "'"));
	}

	cs::String CommandCodec::WriteLog(const cs::List<cs::Ref<IGameCommand>>& commands)
	{
		cs::StringBuilder sb;
		for (const auto& c : commands) sb.Append(Write(c)).Append('\n');
		return sb.ToString();
	}

	cs::List<cs::Ref<IGameCommand>> CommandCodec::ReadLog(const cs::String& text)
	{
		cs::List<cs::Ref<IGameCommand>> list;
		// C#: StringReader.ReadLine over (text ?? ""): lines end at "\n", "\r" or "\r\n"; a final line needs no ending.
		size_t i = 0, n = text.size();
		while (i < n)
		{
			size_t start = i;
			while (i < n && text[i] != '\n' && text[i] != '\r') i++;
			cs::String line = text.substr(start, i - start);
			if (i < n)
			{
				if (text[i] == '\r' && i + 1 < n && text[i + 1] == '\n') i += 2;
				else i++;
			}
			if (cs::Strings::Trim(line).size() > 0) list.Add(Read(line));
		}
		return list;
	}

	void CommandCodec::Need(const cs::List<cs::String>& f, int n, const cs::String& line)
	{
		if (f.Count() < n) throw cs::FormatException(cs::Cat("Too few fields: '", line, "'"));
	}

	cs::String CommandCodec::Int(int v) { return cs::Str(v); }
	cs::String CommandCodec::Num(double v) { return cs::Fmt(v, "R"); }
	int CommandCodec::ParseInt(const cs::String& s) { return cs::Int::Parse(s); }
	// (cs::Double::Parse also takes ',' group separators, which NumberStyles.Float refuses: only malformed input differs.)
	double CommandCodec::ParseNum(const cs::String& s) { return cs::Double::Parse(s); }
}
