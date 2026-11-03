#pragma once
// The one line a serialisable class's header needs (see docs/PERSISTENCE.md): SG_SERIALIZABLE(ClassName) inside the
// class body makes its ObjectGraph registration (SG_SERIALIZE in its .cpp, see Persistence/ObjectGraph.h) a friend,
// so it can name private fields and the private default constructor a load uses. Kept apart from ObjectGraph.h so a
// ported header pulls in nothing heavier than this.

namespace SpaceGod::Core::Graph
{
	template <class T> struct Registration;
}

#define SG_SERIALIZABLE(T) friend struct ::SpaceGod::Core::Graph::Registration<T>
