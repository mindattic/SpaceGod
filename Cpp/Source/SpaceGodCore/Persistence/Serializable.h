#pragma once
// The one line a serialisable class's header needs (see docs/PERSISTENCE.md): SG_SERIALIZABLE(ClassName) inside the
// class body makes its ObjectGraph registration (SG_SERIALIZE in its .cpp, see Persistence/ObjectGraph.h) a friend,
// so it can name private fields and the private default constructor a load uses (and Graph::Access, which reads the
// class's private static tables a save names and redoes its delegate wiring after a load). Kept apart from ObjectGraph.h so a
// ported header pulls in nothing heavier than this.

namespace SpaceGod::Core::Graph
{
	template <class T> struct Registration;
	/// Reaches what a save needs beyond the fields: Access<0> the private static tables a save names
	/// (Persistence/Registrations/Statics.cpp), Access<1> the delegate wiring a load redoes (Persistence/Registrations/Wiring.cpp).
	template <int Part> struct Access;
}

#define SG_SERIALIZABLE(T) template <int> friend struct ::SpaceGod::Core::Graph::Access; friend struct ::SpaceGod::Core::Graph::Registration<T>
