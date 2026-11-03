#pragma once
// Port of Unity.SpaceGod Core/Persistence/ObjectGraph.cs (US-T20).
//
// The C# original serialises the whole World by .NET reflection: every instance field of every reachable object,
// listed per type by name, base class first and each class's own fields in ordinal name order. C++ has no
// reflection, so every serialisable type declares its fields once, in its .cpp, with SG_SERIALIZE (below); the
// registry turns those declarations into exactly the type table, entry table and payload the C# writer produces,
// so a save written by either build reads in the other (see docs/PERSISTENCE.md for what each class must declare,
// what .NET names its fields, and the limits: delegates, closures and a few runtime types).
//
// Layout of this header:
//   ObjectGraph            the C# static class: Write / Read / ToBytes / FromBytes / Clone
//   Graph::NetType         a .NET type as the save names it (interned)
//   Graph::ClassInfo       a registered C++ type: its .NET name, its fields, how to make one
//   Graph::Writer/Reader   what a field codec sees while a graph is written or read
//   Graph::Codec<T>        how one C++ field type maps onto .NET's (primitives, strings, enums, structs, cs::Ref,
//                          cs::List / Dictionary / HashSet / Queue / Stack, Nullable, tuples, DateTime, Random, ...)
//   SG_SERIALIZE & co.     the registration macros

#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <ostream>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	/// System.IO.InvalidDataException (not in Cs.h).
	struct InvalidDataException : cs::Exception { using cs::Exception::Exception; };
	/// System.IO.EndOfStreamException (not in Cs.h).
	struct EndOfStreamException : cs::IOException { using cs::IOException::IOException; };

	namespace Graph
	{
		struct ClassInfo;
		struct Node;
		class Writer;
		class Reader;
		class StaticWalker;

		// ---------------------------------------------------------------- shared type model (as the C#)

		enum class Kind : uint8_t { Primitive, Enum, Nullable, DateTime, TimeSpan, Decimal, Struct, String, Array, List, Dictionary, HashSet, Queue, Stack, Delegate, Object };

		enum class EntryKind : uint8_t { Object, String, Array, Collection, Delegate, External, EmptyArray };

		/// System.TypeCode, for primitives and enums' underlying types.
		enum class TypeCode : uint8_t { Empty = 0, Object = 1, Boolean = 3, Char = 4, SByte = 5, Byte = 6, Int16 = 7, UInt16 = 8, Int32 = 9, UInt32 = 10, Int64 = 11, UInt64 = 12, Single = 13, Double = 14, Decimal = 15, DateTime = 16, String = 18 };

		struct NetType;

		/// One field of a .NET type as its save lists it: "DeclaringType:name" and the field's declared type.
		struct FieldDecl
		{
			cs::String Key;
			const NetType* Type = nullptr;
		};

		/// <summary>
		/// A .NET type, named as the save's type table names it: tag 0 a plain type (FullName + assembly), tag 1 an
		/// array of Element, tag 2 a constructed generic (the definition's FullName + assembly, and Args). Interned:
		/// one instance per distinct type, compared by pointer. Kind and Shape (the field list a struct or class
		/// writes) are filled in when the type is first interned by whoever knows them (a codec or the registry).
		/// </summary>
		struct SPACEGODCORE_API NetType
		{
			uint8_t Tag = 0;
			cs::String FullName, Assembly;
			int Rank = 1;
			bool Vector = true;
			const NetType* Element = nullptr;
			std::vector<const NetType*> Args;

			Kind TypeKind = Kind::Object;
			TypeCode Code = TypeCode::Empty;
			/// For a struct or class: its fields in save order (base first, then by ordinal name). Empty function = none known.
			std::function<std::vector<FieldDecl>()> Shape;
			/// The registered C++ type, if any (classes and structs declared with SG_SERIALIZE / SG_STRUCT).
			const ClassInfo* Class = nullptr;

			cs::String Describe() const;
		};

		/// The assembly names written for .NET's own types and for Core's (the reader ignores them, as the C# reader
		/// falls back to any loaded assembly; see docs/PERSISTENCE.md).
		SPACEGODCORE_API extern const char* CoreLibAssembly;     // "System.Private.CoreLib"
		SPACEGODCORE_API extern const char* CollectionsAssembly; // "System.Collections" (Queue`1, Stack`1 on .NET 5+)
		SPACEGODCORE_API extern const char* CoreAssembly;        // "Unity.SpaceGod.Core" (Unity's asmdef calls it UnitySpace.Core)

		SPACEGODCORE_API const NetType* Plain(const cs::String& fullName, const cs::String& assembly, Kind kind, TypeCode code = TypeCode::Empty);
		SPACEGODCORE_API const NetType* ArrayOf(const NetType* element);
		SPACEGODCORE_API const NetType* Generic(const cs::String& definition, const cs::String& assembly, std::vector<const NetType*> args, Kind kind);
		/// Sets the field list of an interned struct/class type that has no ClassInfo (System.Random's insides, tuples).
		SPACEGODCORE_API void SetShape(const NetType* t, std::function<std::vector<FieldDecl>()> shape);

		SPACEGODCORE_API const NetType* Primitive(TypeCode code);
		SPACEGODCORE_API const NetType* StringType();
		SPACEGODCORE_API const NetType* ObjectType();

		// ---------------------------------------------------------------- the registry

		/// One registered field: its .NET name and key, its declared .NET type, and how to write, read and walk it
		/// on an object (the void* is the most-derived object, adjusted to the declaring class by the registry).
		struct FieldInfo
		{
			cs::String Name;
			cs::String Key;
			std::function<const NetType*()> Type;
			std::function<void(Writer&, void*)> Write;
			std::function<void(Reader&, const struct Value&, void*)> Read;
			std::function<void(StaticWalker&, void*, const cs::String&, int)> Walk;
		};

		/// <summary>A registered C++ type: a class (held by cs::Ref) or a struct (held by value).</summary>
		struct SPACEGODCORE_API ClassInfo
		{
			cs::String FullName;      // .NET FullName: "Unity.SpaceGod.Core.Law", nested "Outer+Inner"
			cs::String Name;          // .NET Type.Name, the field keys' declaring-type part: "Law", "Inner"
			bool IsStruct = false;
			bool IsAbstract = false;
			/// Not Core's own (System.Random): the static walk does not look inside one (as the C# walk skips other assemblies' non-generic classes).
			bool External = false;
			std::type_index Cpp = typeid(void);
			const NetType* Type = nullptr;

			/// This class's own fields as registered (any order: Finalize sorts them).
			std::vector<FieldInfo> Own;
			/// The base class (fields come first) and how to adjust a pointer to this type into one to it.
			std::type_index BaseCpp = typeid(void);
			void* (*ToBase)(void*) = nullptr;
			/// Every C++ type a pointer to this one converts to (itself, its base chain, its interfaces).
			std::vector<std::pair<std::type_index, void* (*)(void*)>> DirectUpcasts;

			std::function<cs::Ref<void>()> Make;           // a class: a new, default-constructed instance
			std::function<void(void*)> Loaded;             // optional: after the whole graph is read (rewire delegates here)

			// ---- filled by Finalize (first Write/Read)
			std::vector<FieldInfo> Fields;                 // the full chain, in save order
			const ClassInfo* Base = nullptr;

			/// A pointer to this type (as void*) converted to <paramref name="target"/>, or null if it does not convert.
			void* Upcast(void* p, std::type_index target) const;
			std::vector<FieldDecl> Shape() const;
		};

		SPACEGODCORE_API ClassInfo& Declare(std::type_index cpp, const cs::String& fullName, bool isStruct);
		SPACEGODCORE_API const ClassInfo* Find(std::type_index cpp);
		SPACEGODCORE_API const ClassInfo* FindByName(const cs::String& fullName);
		/// Finishes every registration (field order, base chains). Called by Write/Read; safe to call again.
		SPACEGODCORE_API void Finalize();

		/// A registered enum: its .NET name and underlying type (Int32 unless the C# enum names another).
		SPACEGODCORE_API void DeclareEnum(std::type_index cpp, const cs::String& fullName, TypeCode underlying);
		SPACEGODCORE_API const NetType* EnumType(std::type_index cpp);
		/// A delegate type declared in Core (`public delegate ...`), so the reader knows it is one.
		SPACEGODCORE_API void DeclareDelegateType(const cs::String& fullName);

		/// A static field of a Core type (see docs/PERSISTENCE.md, "Statics"): objects reachable from it are written by
		/// name, not copied. <paramref name="walk"/> walks its value from the key "Full.Type.Name::field".
		SPACEGODCORE_API void DeclareStatic(const cs::String& typeFullName, const cs::String& fieldName, std::function<void(StaticWalker&, const cs::String&)> walk);
		/// Every static key this build knows, in walk order (to diff against the C# build's).
		SPACEGODCORE_API cs::List<cs::String> StaticKeys();

		// ---------------------------------------------------------------- byte streams (BinaryWriter / BinaryReader)

		class SPACEGODCORE_API ByteWriter
		{
		public:
			std::vector<uint8_t> Bytes;
			void U8(uint8_t v) { Bytes.push_back(v); }
			void Bool(bool v) { U8(v ? 1 : 0); }
			void I16(int16_t v) { Raw(&v, 2); }
			void U16(uint16_t v) { Raw(&v, 2); }
			void I32(int32_t v) { Raw(&v, 4); }
			void U32(uint32_t v) { Raw(&v, 4); }
			void I64(int64_t v) { Raw(&v, 8); }
			void U64(uint64_t v) { Raw(&v, 8); }
			void F32(float v) { Raw(&v, 4); }
			void F64(double v) { Raw(&v, 8); }
			/// BinaryWriter.Write(string): a 7-bit encoded byte length, then UTF-8.
			void Str(const cs::String& s);
			/// ObjectGraph's WriteVar: zigzag, then 7 bits a byte.
			void Var(int value);
			void Append(const std::vector<uint8_t>& b) { Bytes.insert(Bytes.end(), b.begin(), b.end()); }
		private:
			void Raw(const void* p, size_t n); // little-endian (every target Core builds for is)
		};

		class SPACEGODCORE_API ByteReader
		{
		public:
			ByteReader(const uint8_t* data, size_t size) : p(data), n(size) {}
			uint8_t U8();
			bool Bool() { return U8() != 0; }
			int16_t I16() { int16_t v; Raw(&v, 2); return v; }
			uint16_t U16() { uint16_t v; Raw(&v, 2); return v; }
			int32_t I32() { int32_t v; Raw(&v, 4); return v; }
			uint32_t U32() { uint32_t v; Raw(&v, 4); return v; }
			int64_t I64() { int64_t v; Raw(&v, 8); return v; }
			uint64_t U64() { uint64_t v; Raw(&v, 8); return v; }
			float F32() { float v; Raw(&v, 4); return v; }
			double F64() { double v; Raw(&v, 8); return v; }
			cs::String Str();
			int Var();
			size_t Position() const { return pos; }
			size_t Remaining() const { return n - pos; }
		private:
			void Raw(void* out, size_t k);
			const uint8_t* p; size_t n; size_t pos = 0;
		};

		// ---------------------------------------------------------------- values as read

		/// One value as the save holds it, before it is bound to a C++ field.
		struct Value
		{
			enum class Tag : uint8_t { Prim, Null, Ref, Struct, Boxed };
			Tag ValueTag = Tag::Null;
			TypeCode Code = TypeCode::Empty;
			union { int64_t I; uint64_t U; double D; float F; } Prim{};
			int Id = 0;                         // Ref: the entry id
			const struct RType* Type = nullptr; // Struct / Boxed / Prim: the saved type
			std::vector<Value> Items;           // Struct: fields in saved order; Boxed: [0]; Decimal: 4 Int32
		};

		/// A type as the save describes it, resolved against this build.
		struct RType
		{
			uint8_t Tag = 0;
			cs::String FullName, Assembly;
			int Rank = 1;
			bool Vector = true;
			int ElementId = -1;
			std::vector<int> ArgIds;
			const RType* Element = nullptr;
			const RType* Key = nullptr;
			const RType* Item = nullptr;
			std::vector<const RType*> Args;
			Kind TypeKind = Kind::Object;
			TypeCode Code = TypeCode::Empty;
			bool HasFields = false;
			std::vector<std::pair<cs::String, const RType*>> Fields; // saved key and declared type
			const ClassInfo* Class = nullptr;
			std::vector<int> FieldMap; // saved field index -> index in Class->Fields, or -1 (gone in this build)
			int IndexOfField(const cs::String& key) const;
			cs::String Describe() const;
		};

		/// One entry of the save's table, with its payload once read.
		struct Node
		{
			EntryKind Entry = EntryKind::Object;
			const RType* Type = nullptr;
			cs::String Str;                     // String; External: the static key
			std::vector<int> Lengths;           // Array
			int Comparer = 0;                   // Collection (dictionary / set)
			std::vector<Value> Fields;          // Object
			std::vector<Value> Items;           // Array / List / Queue / Stack / HashSet; Dictionary values
			std::vector<Value> Keys;            // Dictionary keys
			// Delegate: (declaring type, method name, parameter types, target id) per call (kept for SG_LOADED hooks).
			struct Call { const RType* Declaring = nullptr; cs::String Method; std::vector<const RType*> Params; int Target = 0; };
			std::vector<Call> Calls;
			// Bound C++ object (Object entries of registered classes; External entries).
			cs::Ref<void> Obj;
			const ClassInfo* Class = nullptr;
			const void* StaticAddr = nullptr;
			std::type_index StaticCpp = typeid(void);
			// A collection shared through cs::Ref<C>, made on first use.
			cs::Ref<void> Shared;
			std::type_index SharedCpp = typeid(void);
		};

		// ---------------------------------------------------------------- writer and reader (what codecs see)

		class SPACEGODCORE_API Writer
		{
		public:
			ByteWriter& Payload() { return payload; }

			/// The id of an object (0 for null), giving it one, and its entry, the first time it is seen.
			int RefObject(void* mostDerived, const ClassInfo* cls);
			/// The id of a string (empty is null, 0), shared by value.
			int RefString(const cs::String& s, bool emptyIsNull = true);
			/// The id of a collection held at <paramref name="addr"/> (a C# List, Dictionary, HashSet, Queue, Stack or
			/// array, by its address in C++). <paramref name="content"/> writes its payload when its turn comes.
			int RefCollection(const void* addr, std::type_index cpp, const NetType* type, int comparer, std::function<void(Writer&)> content);
			int RefArray(const void* addr, std::type_index cpp, const NetType* type, int length, std::function<void(Writer&)> content);
			/// One id per empty array type (C# reads them back as Array.Empty).
			int RefEmptyArray(const NetType* type);
			/// An object with no ClassInfo, written by a codec (System.Random): <paramref name="content"/> writes its fields.
			int RefCustomObject(const void* addr, std::type_index cpp, const NetType* type, std::function<void(Writer&)> content);

			int Meta(const NetType* t);

			// ---- used by ObjectGraph::Write
			int WriteAll(void* root, const ClassInfo* cls);
			void WriteTypes(ByteWriter& w) const;
			ByteWriter Table;
			int EntryCount = 0, PayloadCount = 0;

		private:
			void BeginEntry(int id, EntryKind kind);

			ByteWriter payload;
			std::vector<const NetType*> types;
			std::vector<std::vector<FieldDecl>> shapes;
			std::vector<bool> hasShape;
			std::unordered_map<const NetType*, int> typeOf;
			std::unordered_map<const void*, int> ids;                       // objects, by most-derived address
			std::unordered_map<std::string, int> collectionIds;             // collections, by address + C++ type
			std::unordered_map<cs::String, int> strings;
			std::unordered_map<const NetType*, int> emptyIds;
			std::vector<std::pair<int, std::function<void(Writer&)>>> pending;
			size_t pendingHead = 0;
			int nextId = 1;
		};

		class SPACEGODCORE_API Reader
		{
		public:
			const Node& NodeOf(int id) const;
			Node& MutableNode(int id);

			/// The C++ object an Object or External entry stands for, converted to <paramref name="target"/>; null if it
			/// has none (an unregistered type: a closure) or does not convert.
			cs::Ref<void> ObjectAs(int id, std::type_index target, void*& converted);

			// ---- used by ObjectGraph::Read
			cs::Ref<void> ReadAll(ByteReader& r, const ClassInfo*& rootClass);

		private:
			void ReadTypes(ByteReader& r);
			Value ReadValue(ByteReader& r, const RType* m);
			void ReadContent(ByteReader& r, Node& n);
			std::vector<RType> types;
			std::vector<Node> nodes;
		};

		/// The static-field walk (C#'s Statics.Walk): names every object reachable from Core's statics.
		class SPACEGODCORE_API StaticWalker
		{
		public:
			/// Records a reference-type value (an object or a collection) under <paramref name="key"/>; false if it already
			/// has a name (the walk then stops there, as the C# does).
			bool Record(const void* addr, std::type_index cpp, const cs::String& key, cs::Ref<void> obj, const ClassInfo* cls);
			static constexpr int MaxDepth = 24;
		};

		// ---------------------------------------------------------------- codecs

		SPACEGODCORE_API void WriteStructFields(Writer& w, const ClassInfo* c, void* p);
		SPACEGODCORE_API void ReadStructFields(Reader& r, const ClassInfo* c, const Value& v, void* p);
		SPACEGODCORE_API void WalkStructFields(StaticWalker& s, const ClassInfo* c, void* p, const cs::String& key, int depth);
		SPACEGODCORE_API void WalkObjectFields(StaticWalker& s, const ClassInfo* c, void* p, const cs::String& key, int depth);

		/// <summary>
		/// How a C++ field type is written and read. The primary template is a registered struct held by value
		/// (SG_STRUCT: its fields inline); the specialisations below cover everything else.
		/// </summary>
		template <class T, class = void> struct Codec
		{
			static const ClassInfo* Info()
			{
				const ClassInfo* c = Find(typeid(T));
				if (!c || !c->IsStruct) throw cs::NotSupportedException(cs::Cat("Cannot save a ", typeid(T).name(), ": not registered (SG_STRUCT)"));
				return c;
			}
			static const NetType* Type() { return Info()->Type; }
			static void Write(Writer& w, T& v) { WriteStructFields(w, Info(), &v); }
			static void Read(Reader& r, const Value& v, T& out) { ReadStructFields(r, Info(), v, &out); }
			static void Walk(StaticWalker& s, T& v, const cs::String& key, int depth)
			{
				if (const ClassInfo* c = Find(typeid(T))) WalkStructFields(s, c, &v, key, depth);
			}
		};

		template <class T> using Bare = std::remove_cv_t<std::remove_reference_t<T>>;

		// Primitives.
		template <class T> constexpr TypeCode PrimitiveCode()
		{
			if constexpr (std::is_same_v<T, bool>) return TypeCode::Boolean;
			else if constexpr (std::is_same_v<T, char> || std::is_same_v<T, char16_t> || std::is_same_v<T, wchar_t>) return TypeCode::Char;
			else if constexpr (std::is_same_v<T, float>) return TypeCode::Single;
			else if constexpr (std::is_same_v<T, double>) return TypeCode::Double;
			else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return sizeof(T) == 1 ? TypeCode::SByte : sizeof(T) == 2 ? TypeCode::Int16 : sizeof(T) == 4 ? TypeCode::Int32 : TypeCode::Int64;
			else return sizeof(T) == 1 ? TypeCode::Byte : sizeof(T) == 2 ? TypeCode::UInt16 : sizeof(T) == 4 ? TypeCode::UInt32 : TypeCode::UInt64;
		}
		SPACEGODCORE_API void WritePrimitive(ByteWriter& w, TypeCode code, int64_t i, uint64_t u, double d);
		SPACEGODCORE_API Value ReadPrimitive(ByteReader& r, TypeCode code);

		template <class T> struct Codec<T, std::enable_if_t<std::is_arithmetic_v<T>>>
		{
			static const NetType* Type() { return Primitive(PrimitiveCode<T>()); }
			static void Write(Writer& w, T& v)
			{
				if constexpr (std::is_floating_point_v<T>) WritePrimitive(w.Payload(), PrimitiveCode<T>(), 0, 0, static_cast<double>(v));
				else if constexpr (std::is_signed_v<T>) WritePrimitive(w.Payload(), PrimitiveCode<T>(), static_cast<int64_t>(v), 0, 0);
				else WritePrimitive(w.Payload(), PrimitiveCode<T>(), 0, static_cast<uint64_t>(v), 0);
			}
			static void Read(Reader&, const Value& v, T& out)
			{
				// As C#'s Fits: a value of another primitive type does not go into the field.
				if (v.ValueTag != Value::Tag::Prim || v.Code != PrimitiveCode<T>()) return;
				if constexpr (std::is_same_v<T, float>) out = v.Prim.F;
				else if constexpr (std::is_same_v<T, double>) out = v.Prim.D;
				else if constexpr (std::is_same_v<T, bool>) out = v.Prim.U != 0;
				else if constexpr (std::is_signed_v<T>) out = static_cast<T>(v.Prim.I);
				else out = static_cast<T>(v.Prim.U);
			}
			static void Walk(StaticWalker&, T&, const cs::String&, int) {}
		};

		// Enums (declared with SG_ENUM).
		template <class T> struct Codec<T, std::enable_if_t<std::is_enum_v<T>>>
		{
			using U = std::underlying_type_t<T>;
			static const NetType* Type() { return EnumType(typeid(T)); }
			static void Write(Writer& w, T& v)
			{
				TypeCode c = Type()->Code;
				if (c == TypeCode::UInt64 || c == TypeCode::UInt32 || c == TypeCode::UInt16 || c == TypeCode::Byte) WritePrimitive(w.Payload(), c, 0, static_cast<uint64_t>(static_cast<U>(v)), 0);
				else WritePrimitive(w.Payload(), c, static_cast<int64_t>(static_cast<U>(v)), 0, 0);
			}
			static void Read(Reader&, const Value& v, T& out)
			{
				// Enum.ToObject(savedType, ...): it fits only if the saved enum is this one.
				if (v.ValueTag != Value::Tag::Prim || !v.Type || v.Type->TypeKind != Kind::Enum || v.Type->FullName != Type()->FullName) return;
				TypeCode c = v.Code;
				bool unsignedCode = c == TypeCode::UInt64 || c == TypeCode::UInt32 || c == TypeCode::UInt16 || c == TypeCode::Byte;
				out = static_cast<T>(unsignedCode ? static_cast<U>(v.Prim.U) : static_cast<U>(v.Prim.I));
			}
			static void Walk(StaticWalker&, T&, const cs::String&, int) {}
		};

		// Strings: a C# null string is an empty cs::String (docs/MIGRATION.md). EmptyIsNull writes "" as null.
		template <bool EmptyIsNull> struct StringCodec
		{
			static const NetType* Type() { return StringType(); }
			static void Write(Writer& w, cs::String& v) { w.Payload().Var(w.RefString(v, EmptyIsNull)); }
			static void Read(Reader& r, const Value& v, cs::String& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out.clear(); return; }
				if (v.ValueTag != Value::Tag::Ref) return;
				const Node& n = r.NodeOf(v.Id);
				if (n.Entry == EntryKind::String) out = n.Str;
			}
			static void Walk(StaticWalker&, cs::String&, const cs::String&, int) {}
		};
		template <> struct Codec<cs::String> : StringCodec<true> {};

		// cs::Nullable<T>: System.Nullable`1.
		template <class T> struct Codec<cs::Nullable<T>>
		{
			static const NetType* Type() { return Generic("System.Nullable`1", CoreLibAssembly, { Codec<T>::Type() }, Kind::Nullable); }
			static void Write(Writer& w, cs::Nullable<T>& v)
			{
				w.Payload().Bool(v.has_value());
				if (v.has_value()) Codec<T>::Write(w, *v);
			}
			static void Read(Reader& r, const Value& v, cs::Nullable<T>& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out.reset(); return; }
				T x{};
				if (out.has_value()) x = *out;
				Codec<T>::Read(r, v, x);
				out = x;
			}
			static void Walk(StaticWalker& s, cs::Nullable<T>& v, const cs::String& key, int depth) { if (v.has_value()) Codec<T>::Walk(s, *v, key, depth); }
		};

		// A registered class held by cs::Ref (polymorphic: the object's own registered type is written).
		template <class T> struct ObjectCodecBase
		{
			static const ClassInfo* ClassOf(T* p)
			{
				const ClassInfo* c;
				if constexpr (std::is_polymorphic_v<T>) c = Find(typeid(*p));
				else c = Find(typeid(T));
				if (!c) throw cs::NotSupportedException(cs::Cat("Cannot save a ", typeid(*p).name(), ": not registered (SG_SERIALIZE)"));
				return c;
			}
			static void* MostDerived(T* p)
			{
				if constexpr (std::is_polymorphic_v<T>) return dynamic_cast<void*>(p);
				else return static_cast<void*>(p);
			}
			static const NetType* Type()
			{
				const ClassInfo* c = Find(typeid(T));
				if (!c) throw cs::NotSupportedException(cs::Cat("Cannot save a field of type ", typeid(T).name(), ": not registered (SG_SERIALIZE / SG_INTERFACE)"));
				return c->Type;
			}
			static void WritePtr(Writer& w, T* p)
			{
				if (!p) { w.Payload().Var(0); return; }
				w.Payload().Var(w.RefObject(MostDerived(p), ClassOf(p)));
			}
			static cs::Ref<T> ReadRef(Reader& r, const Value& v, bool& fits)
			{
				fits = true;
				if (v.ValueTag == Value::Tag::Null) return nullptr;
				fits = false;
				if (v.ValueTag != Value::Tag::Ref) return nullptr;
				void* converted = nullptr;
				cs::Ref<void> obj = r.ObjectAs(v.Id, typeid(T), converted);
				if (!obj || !converted) return nullptr;
				fits = true;
				return cs::Ref<T>(obj, static_cast<T*>(converted));
			}
			static void WalkPtr(StaticWalker& s, const cs::Ref<T>& p, const cs::String& key, int depth)
			{
				if (!p || depth > StaticWalker::MaxDepth) return;
				const ClassInfo* c;
				if constexpr (std::is_polymorphic_v<T>) c = Find(typeid(*p));
				else c = Find(typeid(T));
				if (!c) return; // not something a save can refer to
				void* d = MostDerived(p.get());
				if (!s.Record(d, c->Cpp, key, cs::Ref<void>(p, d), c)) return;
				WalkObjectFields(s, c, d, key, depth);
			}
		};

		template <class T> struct Codec<cs::Ref<T>> : ObjectCodecBase<T>
		{
			static void Write(Writer& w, cs::Ref<T>& v) { ObjectCodecBase<T>::WritePtr(w, v.get()); }
			static void Read(Reader& r, const Value& v, cs::Ref<T>& out) { bool fits; auto p = ObjectCodecBase<T>::ReadRef(r, v, fits); if (fits) out = p; }
			static void Walk(StaticWalker& s, cs::Ref<T>& v, const cs::String& key, int depth) { ObjectCodecBase<T>::WalkPtr(s, v, key, depth); }
		};
		// A raw pointer to an object something else owns (e.g. a subsystem's World*): written as the reference it is.
		template <class T> struct Codec<T*> : ObjectCodecBase<T>
		{
			static void Write(Writer& w, T*& v) { ObjectCodecBase<T>::WritePtr(w, v); }
			static void Read(Reader& r, const Value& v, T*& out) { bool fits; auto p = ObjectCodecBase<T>::ReadRef(r, v, fits); if (fits) out = p.get(); }
			static void Walk(StaticWalker&, T*&, const cs::String&, int) {}
		};
		template <class T> struct Codec<std::weak_ptr<T>> : ObjectCodecBase<T>
		{
			static void Write(Writer& w, std::weak_ptr<T>& v) { auto p = v.lock(); ObjectCodecBase<T>::WritePtr(w, p.get()); }
			static void Read(Reader& r, const Value& v, std::weak_ptr<T>& out) { bool fits; auto p = ObjectCodecBase<T>::ReadRef(r, v, fits); if (fits) out = p; }
			static void Walk(StaticWalker&, std::weak_ptr<T>&, const cs::String&, int) {}
		};

		// ---- collections (C# reference types: each its own entry; a by-value member is identified by its address)

		/// How a dictionary or set compares its keys, as the save codes it (0 default, 1 Ordinal, 2 OrdinalIgnoreCase).
		template <class H> constexpr int ComparerCode() { return std::is_same_v<H, cs::OrdinalIgnoreCaseComparer> ? 2 : 0; }

		template <class C> struct CollectionTraits;
		template <class T> struct CollectionTraits<cs::List<T>>
		{
			static const NetType* Type() { return Generic("System.Collections.Generic.List`1", CoreLibAssembly, { Codec<T>::Type() }, Kind::List); }
			static constexpr int Comparer = 0;
			static void Content(Writer& w, cs::List<T>& c)
			{
				w.Payload().Var(c.Count());
				for (auto& x : c) Codec<T>::Write(w, static_cast<T&>(x));
			}
			static void Fill(Reader& r, const Node& n, cs::List<T>& c)
			{
				c.Clear();
				for (const auto& v : n.Items) { T x{}; Codec<T>::Read(r, v, x); c.Add(x); }
			}
			static void Walk(StaticWalker& s, cs::List<T>& c, const cs::String& key, int depth)
			{
				int i = 0;
				for (auto& x : c) Codec<T>::Walk(s, static_cast<T&>(x), cs::Cat(key, "[", i++, "]"), depth + 1);
			}
		};
		template <class T> struct CollectionTraits<cs::Queue<T>>
		{
			static const NetType* Type() { return Generic("System.Collections.Generic.Queue`1", CollectionsAssembly, { Codec<T>::Type() }, Kind::Queue); }
			static constexpr int Comparer = 0;
			static void Content(Writer& w, cs::Queue<T>& c)
			{
				std::vector<T> items(c.begin(), c.end());
				w.Payload().Var(static_cast<int>(items.size()));
				for (auto& x : items) Codec<T>::Write(w, x);
			}
			static void Fill(Reader& r, const Node& n, cs::Queue<T>& c)
			{
				c.Clear();
				for (const auto& v : n.Items) { T x{}; Codec<T>::Read(r, v, x); c.Enqueue(x); }
			}
			static void Walk(StaticWalker& s, cs::Queue<T>& c, const cs::String& key, int depth)
			{
				int i = 0;
				std::vector<T> items(c.begin(), c.end());
				for (auto& x : items) Codec<T>::Walk(s, x, cs::Cat(key, "[", i++, "]"), depth + 1);
			}
		};
		template <class T> struct CollectionTraits<cs::Stack<T>>
		{
			static const NetType* Type() { return Generic("System.Collections.Generic.Stack`1", CollectionsAssembly, { Codec<T>::Type() }, Kind::Stack); }
			static constexpr int Comparer = 0;
			// cs::Stack can't be enumerated (Cs.h gap), so a copy is popped: top first, as C# enumerates a Stack.
			static std::vector<T> Items(const cs::Stack<T>& c) { cs::Stack<T> copy = c; std::vector<T> items; while (copy.Count() > 0) items.push_back(copy.Pop()); return items; }
			static void Content(Writer& w, cs::Stack<T>& c)
			{
				auto items = Items(c);
				w.Payload().Var(static_cast<int>(items.size()));
				for (auto& x : items) Codec<T>::Write(w, x);
			}
			static void Fill(Reader& r, const Node& n, cs::Stack<T>& c)
			{
				c.Clear();
				std::vector<T> items;
				for (const auto& v : n.Items) { T x{}; Codec<T>::Read(r, v, x); items.push_back(x); }
				for (size_t i = items.size(); i-- > 0;) c.Push(items[i]);
			}
			static void Walk(StaticWalker& s, cs::Stack<T>& c, const cs::String& key, int depth)
			{
				int i = 0;
				auto items = Items(c);
				for (auto& x : items) Codec<T>::Walk(s, x, cs::Cat(key, "[", i++, "]"), depth + 1);
			}
		};
		template <class T, class H, class E> struct CollectionTraits<cs::HashSet<T, H, E>>
		{
			static const NetType* Type() { return Generic("System.Collections.Generic.HashSet`1", CoreLibAssembly, { Codec<T>::Type() }, Kind::HashSet); }
			static constexpr int Comparer = ComparerCode<H>();
			static void Content(Writer& w, cs::HashSet<T, H, E>& c)
			{
				// Compacted as it is written (cleared and refilled in its own order), as the C# writer does, so the
				// original and the copy keep enumerating in step after later removals.
				std::vector<T> members;
				for (const auto& x : c) members.push_back(x);
				c.Clear();
				for (auto& x : members) c.Add(x);
				w.Payload().Var(static_cast<int>(members.size()));
				for (auto& x : members) Codec<T>::Write(w, x);
			}
			static void Fill(Reader& r, const Node& n, cs::HashSet<T, H, E>& c)
			{
				c.Clear();
				for (const auto& v : n.Items) { T x{}; Codec<T>::Read(r, v, x); c.Add(x); }
			}
			static void Walk(StaticWalker& s, cs::HashSet<T, H, E>& c, const cs::String& key, int depth)
			{
				int i = 0;
				std::vector<T> members;
				for (const auto& x : c) members.push_back(x);
				for (auto& x : members) Codec<T>::Walk(s, x, cs::Cat(key, "[", i++, "]"), depth + 1);
			}
		};
		template <class K, class V, class H, class E> struct CollectionTraits<cs::Dictionary<K, V, H, E>>
		{
			static const NetType* Type() { return Generic("System.Collections.Generic.Dictionary`2", CoreLibAssembly, { Codec<K>::Type(), Codec<V>::Type() }, Kind::Dictionary); }
			static constexpr int Comparer = ComparerCode<H>();
			static void Content(Writer& w, cs::Dictionary<K, V, H, E>& d)
			{
				std::vector<K> keys;
				std::vector<V> values;
				for (auto [k, v] : d) { keys.push_back(k); values.push_back(v); }
				d.Clear();
				for (size_t i = 0; i < keys.size(); i++) d.Add(keys[i], values[i]);
				w.Payload().Var(static_cast<int>(keys.size()));
				for (size_t i = 0; i < keys.size(); i++)
				{
					Codec<K>::Write(w, keys[i]);
					Codec<V>::Write(w, values[i]);
				}
			}
			static void Fill(Reader& r, const Node& n, cs::Dictionary<K, V, H, E>& d)
			{
				d.Clear();
				for (size_t i = 0; i < n.Keys.size(); i++)
				{
					K k{};
					V v{};
					Codec<K>::Read(r, n.Keys[i], k);
					Codec<V>::Read(r, n.Items[i], v);
					d.Add(k, v);
				}
			}
			static void Walk(StaticWalker& s, cs::Dictionary<K, V, H, E>& d, const cs::String& key, int depth)
			{
				int i = 0;
				std::vector<K> keys;
				std::vector<V> values;
				for (auto [k, v] : d) { keys.push_back(k); values.push_back(v); }
				for (size_t j = 0; j < keys.size(); j++)
				{
					Codec<K>::Walk(s, keys[j], cs::Cat(key, "{k", i, "}"), depth + 1);
					Codec<V>::Walk(s, values[j], cs::Cat(key, "{", i, "}"), depth + 1);
					i++;
				}
			}
		};

		SPACEGODCORE_API bool WalkCollection(StaticWalker& s, const void* addr, std::type_index cpp, const cs::String& key, int depth);
		/// The static collection an External entry names, if it is a <paramref name="cpp"/>; else null.
		SPACEGODCORE_API const void* StaticCollection(Reader& r, int id, std::type_index cpp);

		template <class C> struct CollectionCodec
		{
			using Tr = CollectionTraits<C>;
			static const NetType* Type() { return Tr::Type(); }
			static int Ref(Writer& w, C& c)
			{
				C* p = &c;
				return w.RefCollection(p, typeid(C), Tr::Type(), Tr::Comparer, [p](Writer& wr) { Tr::Content(wr, *p); });
			}
			static bool Fill(Reader& r, const Value& v, C& out)
			{
				if (v.ValueTag != Value::Tag::Ref) return false;
				const Node& n = r.NodeOf(v.Id);
				if (n.Entry == EntryKind::External)
				{
					if (auto s = static_cast<const C*>(StaticCollection(r, v.Id, typeid(C)))) { out = *s; return true; }
					return false;
				}
				if (n.Entry != EntryKind::Collection || !n.Type || n.Type->TypeKind != Tr::Type()->TypeKind) return false;
				Tr::Fill(r, n, out);
				return true;
			}
			static void Walk(StaticWalker& s, C& c, const cs::String& key, int depth)
			{
				if (depth > StaticWalker::MaxDepth) return;
				if (!s.Record(&c, typeid(C), key, nullptr, nullptr)) return;
				Tr::Walk(s, c, key, depth);
			}
		};

		// By value (a C# List field the C++ port holds as a member): null loads as empty.
		template <class T> struct Codec<cs::List<T>> : CollectionCodec<cs::List<T>>
		{
			static void Write(Writer& w, cs::List<T>& v) { w.Payload().Var(CollectionCodec<cs::List<T>>::Ref(w, v)); }
			static void Read(Reader& r, const Value& v, cs::List<T>& out) { if (v.ValueTag == Value::Tag::Null) out.Clear(); else CollectionCodec<cs::List<T>>::Fill(r, v, out); }
		};
		template <class T> struct Codec<cs::Queue<T>> : CollectionCodec<cs::Queue<T>>
		{
			static void Write(Writer& w, cs::Queue<T>& v) { w.Payload().Var(CollectionCodec<cs::Queue<T>>::Ref(w, v)); }
			static void Read(Reader& r, const Value& v, cs::Queue<T>& out) { if (v.ValueTag == Value::Tag::Null) out.Clear(); else CollectionCodec<cs::Queue<T>>::Fill(r, v, out); }
		};
		template <class T> struct Codec<cs::Stack<T>> : CollectionCodec<cs::Stack<T>>
		{
			static void Write(Writer& w, cs::Stack<T>& v) { w.Payload().Var(CollectionCodec<cs::Stack<T>>::Ref(w, v)); }
			static void Read(Reader& r, const Value& v, cs::Stack<T>& out) { if (v.ValueTag == Value::Tag::Null) out.Clear(); else CollectionCodec<cs::Stack<T>>::Fill(r, v, out); }
		};
		template <class T, class H, class E> struct Codec<cs::HashSet<T, H, E>> : CollectionCodec<cs::HashSet<T, H, E>>
		{
			static void Write(Writer& w, cs::HashSet<T, H, E>& v) { w.Payload().Var(CollectionCodec<cs::HashSet<T, H, E>>::Ref(w, v)); }
			static void Read(Reader& r, const Value& v, cs::HashSet<T, H, E>& out) { if (v.ValueTag == Value::Tag::Null) out.Clear(); else CollectionCodec<cs::HashSet<T, H, E>>::Fill(r, v, out); }
		};
		template <class K, class V, class H, class E> struct Codec<cs::Dictionary<K, V, H, E>> : CollectionCodec<cs::Dictionary<K, V, H, E>>
		{
			static void Write(Writer& w, cs::Dictionary<K, V, H, E>& v) { w.Payload().Var(CollectionCodec<cs::Dictionary<K, V, H, E>>::Ref(w, v)); }
			static void Read(Reader& r, const Value& v, cs::Dictionary<K, V, H, E>& out) { if (v.ValueTag == Value::Tag::Null) out.Clear(); else CollectionCodec<cs::Dictionary<K, V, H, E>>::Fill(r, v, out); }
		};

		// Shared (cs::Ref<C>, where C# shares one collection between objects): one C++ collection per saved id.
		SPACEGODCORE_API cs::Ref<void> SharedCollection(Reader& r, int id, std::type_index cpp, const std::function<cs::Ref<void>()>& make, const std::function<void(void*)>& fill);
		template <class C> struct SharedCollectionCodec
		{
			using Tr = CollectionTraits<C>;
			static const NetType* Type() { return Tr::Type(); }
			static void Write(Writer& w, cs::Ref<C>& v) { w.Payload().Var(v ? CollectionCodec<C>::Ref(w, *v) : 0); }
			static void Read(Reader& r, const Value& v, cs::Ref<C>& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out = nullptr; return; }
				if (v.ValueTag != Value::Tag::Ref) return;
				auto p = SharedCollection(r, v.Id, typeid(C), [] { return cs::Ref<void>(cs::New<C>()); },
					[&r, &v](void* c) { CollectionCodec<C>::Fill(r, v, *static_cast<C*>(c)); });
				if (p) out = std::static_pointer_cast<C>(p);
			}
			static void Walk(StaticWalker& s, cs::Ref<C>& v, const cs::String& key, int depth) { if (v) CollectionCodec<C>::Walk(s, *v, key, depth); }
		};
		template <class T> struct Codec<cs::Ref<cs::List<T>>> : SharedCollectionCodec<cs::List<T>> {};
		template <class T> struct Codec<cs::Ref<cs::Queue<T>>> : SharedCollectionCodec<cs::Queue<T>> {};
		template <class T> struct Codec<cs::Ref<cs::Stack<T>>> : SharedCollectionCodec<cs::Stack<T>> {};
		template <class T, class H, class E> struct Codec<cs::Ref<cs::HashSet<T, H, E>>> : SharedCollectionCodec<cs::HashSet<T, H, E>> {};
		template <class K, class V, class H, class E> struct Codec<cs::Ref<cs::Dictionary<K, V, H, E>>> : SharedCollectionCodec<cs::Dictionary<K, V, H, E>> {};

		// A C# `T[]` (cs::List<T> by value, declared with SG_ARRAY): an Array entry, rank 1; empty is Array.Empty.
		template <class T> struct ArrayCodec
		{
			static const NetType* Type() { return ArrayOf(Codec<T>::Type()); }
			static void Write(Writer& w, cs::List<T>& v)
			{
				if (v.Count() == 0) { w.Payload().Var(w.RefEmptyArray(Type())); return; }
				cs::List<T>* p = &v;
				w.Payload().Var(w.RefArray(p, typeid(cs::List<T>), Type(), v.Count(), [p](Writer& wr) { for (auto& x : *p) Codec<T>::Write(wr, static_cast<T&>(x)); }));
			}
			static void Read(Reader& r, const Value& v, cs::List<T>& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out.Clear(); return; }
				if (v.ValueTag != Value::Tag::Ref) return;
				const Node& n = r.NodeOf(v.Id);
				if (n.Entry == EntryKind::EmptyArray) { out.Clear(); return; }
				if (n.Entry == EntryKind::External)
				{
					if (auto s = static_cast<const cs::List<T>*>(StaticCollection(r, v.Id, typeid(cs::List<T>)))) out = *s;
					return;
				}
				if (n.Entry != EntryKind::Array) return;
				out.Clear();
				for (const auto& item : n.Items) { T x{}; Codec<T>::Read(r, item, x); out.Add(x); }
			}
			static void Walk(StaticWalker& s, cs::List<T>& v, const cs::String& key, int depth)
			{
				if (depth > StaticWalker::MaxDepth) return;
				if (!s.Record(&v, typeid(cs::List<T>), key, nullptr, nullptr)) return;
				if constexpr (std::is_arithmetic_v<T>) return; // a primitive array: nothing inside to name
				else
				{
					int i = 0;
					for (auto& x : v) Codec<T>::Walk(s, static_cast<T&>(x), cs::Cat(key, "[", i++, "]"), depth + 1);
				}
			}
		};
		template <class T> struct SharedArrayCodec
		{
			static const NetType* Type() { return ArrayCodec<T>::Type(); }
			static void Write(Writer& w, cs::Ref<cs::List<T>>& v) { if (!v) w.Payload().Var(0); else ArrayCodec<T>::Write(w, *v); }
			static void Read(Reader& r, const Value& v, cs::Ref<cs::List<T>>& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out = nullptr; return; }
				if (v.ValueTag != Value::Tag::Ref) return;
				auto p = SharedCollection(r, v.Id, typeid(cs::List<T>), [] { return cs::Ref<void>(cs::New<cs::List<T>>()); },
					[&r, &v](void* c) { ArrayCodec<T>::Read(r, v, *static_cast<cs::List<T>*>(c)); });
				if (p) out = std::static_pointer_cast<cs::List<T>>(p);
			}
			static void Walk(StaticWalker& s, cs::Ref<cs::List<T>>& v, const cs::String& key, int depth) { if (v) ArrayCodec<T>::Walk(s, *v, key, depth); }
		};

		// ---- delegates: written as null (see docs/PERSISTENCE.md, "Delegates"); a saved one loads as nothing, and
		// the class's SG_LOADED hook wires it again.
		SPACEGODCORE_API const NetType* DelegateType(int argCount, bool func, std::vector<const NetType*> args);
		template <class... A> struct DelegateArgs { static std::vector<const NetType*> Types() { return { Codec<Bare<A>>::Type()... }; } };
		template <class... A> struct Codec<cs::Event<A...>>
		{
			static const NetType* Type() { return DelegateType(static_cast<int>(sizeof...(A)), false, DelegateArgs<A...>::Types()); }
			static void Write(Writer& w, cs::Event<A...>&) { w.Payload().Var(0); }
			static void Read(Reader&, const Value&, cs::Event<A...>&) {}
			static void Walk(StaticWalker&, cs::Event<A...>&, const cs::String&, int) {}
		};
		template <class... A> struct Codec<cs::Delegate<A...>>
		{
			static const NetType* Type() { return DelegateType(static_cast<int>(sizeof...(A)), false, DelegateArgs<A...>::Types()); }
			static void Write(Writer& w, cs::Delegate<A...>&) { w.Payload().Var(0); }
			static void Read(Reader&, const Value&, cs::Delegate<A...>&) {}
			static void Walk(StaticWalker&, cs::Delegate<A...>&, const cs::String&, int) {}
		};
		template <class R, class... A> struct Codec<std::function<R(A...)>>
		{
			static const NetType* Type()
			{
				if constexpr (std::is_void_v<R>) return DelegateType(static_cast<int>(sizeof...(A)), false, DelegateArgs<A...>::Types());
				else
				{
					auto args = DelegateArgs<A...>::Types();
					args.push_back(Codec<Bare<R>>::Type());
					return DelegateType(static_cast<int>(sizeof...(A)) + 1, true, args);
				}
			}
			static void Write(Writer& w, std::function<R(A...)>&) { w.Payload().Var(0); }
			static void Read(Reader&, const Value&, std::function<R(A...)>&) {}
			static void Walk(StaticWalker&, std::function<R(A...)>&, const cs::String&, int) {}
		};

		// ---- .NET value types with their own encoding
		template <> struct SPACEGODCORE_API Codec<cs::DateTime>
		{
			static const NetType* Type();
			static void Write(Writer& w, cs::DateTime& v);
			static void Read(Reader& r, const Value& v, cs::DateTime& out);
			static void Walk(StaticWalker&, cs::DateTime&, const cs::String&, int) {}
		};
		template <> struct SPACEGODCORE_API Codec<cs::TimeSpan>
		{
			static const NetType* Type();
			static void Write(Writer& w, cs::TimeSpan& v);
			static void Read(Reader& r, const Value& v, cs::TimeSpan& out);
			static void Walk(StaticWalker&, cs::TimeSpan&, const cs::String&, int) {}
		};

		/// System.Random, held by value (the C# `readonly Random rng`): a reference object of .NET 10's layout
		/// (Random._impl -> Random+Net5CompatSeedImpl._prng -> Random+CompatPrng {_inext, _inextp, _seedArray});
		/// reads that layout or Mono's (Unity: Random {SeedArray, inext, inextp}).
		template <> struct SPACEGODCORE_API Codec<cs::Random>
		{
			static const NetType* Type();
			static void Write(Writer& w, cs::Random& v);
			static void Read(Reader& r, const Value& v, cs::Random& out);
			static void Walk(StaticWalker&, cs::Random&, const cs::String&, int) {}
		};
		template <> struct Codec<cs::Ref<cs::Random>>
		{
			static const NetType* Type() { return Codec<cs::Random>::Type(); }
			static void Write(Writer& w, cs::Ref<cs::Random>& v) { if (!v) w.Payload().Var(0); else Codec<cs::Random>::Write(w, *v); }
			static void Read(Reader& r, const Value& v, cs::Ref<cs::Random>& out)
			{
				if (v.ValueTag == Value::Tag::Null) { out = nullptr; return; }
				if (!out) out = cs::New<cs::Random>(0);
				Codec<cs::Random>::Read(r, v, *out);
			}
			static void Walk(StaticWalker&, cs::Ref<cs::Random>&, const cs::String&, int) {}
		};

		// ---- C# value tuples (std::tuple / std::pair): System.ValueTuple`N {Item1..ItemN}
		SPACEGODCORE_API const NetType* TupleType(std::vector<const NetType*> args);
		SPACEGODCORE_API const Value* TupleItem(const Value& v, int index);
		template <class... A> struct Codec<std::tuple<A...>>
		{
			static const NetType* Type() { return TupleType({ Codec<A>::Type()... }); }
			static void Write(Writer& w, std::tuple<A...>& v) { std::apply([&](auto&... x) { (Codec<Bare<decltype(x)>>::Write(w, x), ...); }, v); }
			static void Read(Reader& r, const Value& v, std::tuple<A...>& out)
			{
				if (v.ValueTag != Value::Tag::Struct) return;
				int i = 0;
				std::apply([&](auto&... x) { ((ReadItem(r, v, i++, x)), ...); }, out);
			}
			template <class X> static void ReadItem(Reader& r, const Value& v, int i, X& x) { if (auto item = TupleItem(v, i)) Codec<X>::Read(r, *item, x); }
			static void Walk(StaticWalker& s, std::tuple<A...>& v, const cs::String& key, int depth)
			{
				int i = 1;
				std::apply([&](auto&... x) { (Codec<Bare<decltype(x)>>::Walk(s, x, cs::Cat(key, ".Item", i++), depth + 1), ...); }, v);
			}
		};
		template <class A, class B> struct Codec<std::pair<A, B>>
		{
			static const NetType* Type() { return TupleType({ Codec<A>::Type(), Codec<B>::Type() }); }
			static void Write(Writer& w, std::pair<A, B>& v) { Codec<A>::Write(w, v.first); Codec<B>::Write(w, v.second); }
			static void Read(Reader& r, const Value& v, std::pair<A, B>& out)
			{
				if (v.ValueTag != Value::Tag::Struct) return;
				if (auto a = TupleItem(v, 0)) Codec<A>::Read(r, *a, out.first);
				if (auto b = TupleItem(v, 1)) Codec<B>::Read(r, *b, out.second);
			}
			static void Walk(StaticWalker& s, std::pair<A, B>& v, const cs::String& key, int depth)
			{
				Codec<A>::Walk(s, v.first, cs::Cat(key, ".Item1"), depth + 1);
				Codec<B>::Walk(s, v.second, cs::Cat(key, ".Item2"), depth + 1);
			}
		};

		// ---------------------------------------------------------------- registration builders

		/// A field codec that writes the declared .NET type's default and ignores the saved value: for a C# field the
		/// C++ port has no member for (a cache it computes instead). SG_GHOST.
		template <class T> struct GhostCodec
		{
			static const NetType* Type() { return Codec<T>::Type(); }
		};

		template <class T> class ClassBuilder
		{
		public:
			explicit ClassBuilder(ClassInfo& c) : info(c) {}

			/// A field, by its .NET name, through <typeparamref name="CodecT"/> (Codec of the member's type by default).
			template <class CodecT, class M> ClassBuilder& FieldWith(const cs::String& netName, M T::* member)
			{
				FieldInfo f;
				f.Name = netName;
				f.Key = cs::Cat(info.Name, ":", netName);
				f.Type = [] { return CodecT::Type(); };
				f.Write = [member](Writer& w, void* o) { CodecT::Write(w, const_cast<Bare<M>&>(static_cast<T*>(o)->*member)); };
				f.Read = [member](Reader& r, const Value& v, void* o) { CodecT::Read(r, v, const_cast<Bare<M>&>(static_cast<T*>(o)->*member)); };
				f.Walk = [member](StaticWalker& s, void* o, const cs::String& key, int depth) { CodecT::Walk(s, const_cast<Bare<M>&>(static_cast<T*>(o)->*member), key, depth); };
				info.Own.push_back(std::move(f));
				return *this;
			}
			template <class M> ClassBuilder& Field(const cs::String& netName, M T::* member) { return FieldWith<Codec<Bare<M>>>(netName, member); }
			/// An auto-property's backing field: .NET names it `<Name>k__BackingField`.
			template <class M> ClassBuilder& Property(const cs::String& name, M T::* member) { return Field(cs::Cat("<", name, ">k__BackingField"), member); }
			/// A C# array (`T[]`) the port holds as cs::List<T> (or cs::Ref<cs::List<T>>).
			template <class E> ClassBuilder& Array(const cs::String& netName, cs::List<E> T::* member) { return FieldWith<ArrayCodec<E>>(netName, member); }
			template <class E> ClassBuilder& Array(const cs::String& netName, cs::Ref<cs::List<E>> T::* member) { return FieldWith<SharedArrayCodec<E>>(netName, member); }
			/// A string C# never leaves null ("" written as "", not null).
			ClassBuilder& String(const cs::String& netName, cs::String T::* member) { return FieldWith<StringCodec<false>>(netName, member); }
			/// A C# field the port has no member for: written as the default of <typeparamref name="M"/>, ignored on load.
			template <class M> ClassBuilder& Ghost(const cs::String& netName)
			{
				FieldInfo f;
				f.Name = netName;
				f.Key = cs::Cat(info.Name, ":", netName);
				f.Type = [] { return Codec<M>::Type(); };
				f.Write = [](Writer& w, void*) { M v{}; Codec<M>::Write(w, v); };
				f.Read = [](Reader&, const Value&, void*) {};
				f.Walk = [](StaticWalker&, void*, const cs::String&, int) {};
				info.Own.push_back(std::move(f));
				return *this;
			}
			/// The base class whose fields come first.
			template <class B> ClassBuilder& Base()
			{
				info.BaseCpp = typeid(B);
				info.ToBase = [](void* p) -> void* { return static_cast<void*>(static_cast<B*>(static_cast<T*>(p))); };
				info.DirectUpcasts.push_back({ typeid(B), info.ToBase });
				return *this;
			}
			/// An interface (or any other C++ base) a field may be declared as.
			template <class I> ClassBuilder& Interface()
			{
				info.DirectUpcasts.push_back({ typeid(I), [](void* p) -> void* { return static_cast<void*>(static_cast<I*>(static_cast<T*>(p))); } });
				return *this;
			}
			/// After a load: wire again what the save could not carry (delegates, caches).
			ClassBuilder& Loaded(std::function<void(T&)> fn)
			{
				auto prev = info.Loaded;
				info.Loaded = [fn, prev](void* p) { if (prev) prev(p); fn(*static_cast<T*>(p)); };
				return *this;
			}
			ClassBuilder& Abstract() { info.IsAbstract = true; info.Make = nullptr; return *this; }
			ClassBuilder& NotCore() { info.External = true; return *this; }
			operator bool() const { return true; }

		private:
			ClassInfo& info;
		};

		template <class T> ClassBuilder<T> Class(const cs::String& fullName, std::function<cs::Ref<void>()> make)
		{
			ClassInfo& c = Declare(typeid(T), fullName, false);
			c.Make = std::move(make);
			c.DirectUpcasts.push_back({ typeid(T), [](void* p) { return p; } });
			return ClassBuilder<T>(c);
		}
		template <class T> ClassBuilder<T> Struct(const cs::String& fullName)
		{
			ClassInfo& c = Declare(typeid(T), fullName, true);
			c.DirectUpcasts.push_back({ typeid(T), [](void* p) { return p; } });
			return ClassBuilder<T>(c);
		}
		template <class T> ClassBuilder<T> Interface(const cs::String& fullName)
		{
			ClassInfo& c = Declare(typeid(T), fullName, false);
			c.IsAbstract = true;
			c.DirectUpcasts.push_back({ typeid(T), [](void* p) { return p; } });
			return ClassBuilder<T>(c);
		}

		/// Each serialisable type's registration lives in this specialisation (SG_SERIALIZE), a friend of the type
		/// through SG_SERIALIZABLE, so it can name private members and the private default constructor.
		template <class T> struct Registration { static bool Run(); };

		/// Swallows the field list of SG_SERIALIZE (each item has already registered itself).
		template <class... A> void Fields(A&&...) {}

		/// A static field's walk (SG_STATIC).
		template <class V> std::function<void(StaticWalker&, const cs::String&)> StaticWalk(std::function<V&()> get)
		{
			return [get](StaticWalker& s, const cs::String& key) { Codec<Bare<V>>::Walk(s, const_cast<Bare<V>&>(get()), key, 0); };
		}
	}

	/// <summary>
	/// Writes a whole object graph (every field of every object reachable from a root, private ones
	/// included) to a stream and reads it back as an exact copy: shared references stay shared, cycles
	/// close, random number generators carry on where they were, and the delegates the simulation wires
	/// between its parts (a battle's callbacks into its war, the economy asking the fleet for a trip)
	/// point at the copies. It is what lets the simulation be saved without a hand-written save routine
	/// for each of its hundred-odd classes, and keeps a save complete as they grow.
	///
	/// The format is self-describing: each type's fields are listed by name, so a save still reads after
	/// a field is removed (its value is skipped); one made before a field was added is refused. It is not a long-term
	/// archive format: compiler-generated closures are named per build, so a save made by one build may
	/// not read in another whose lambdas moved.
	///
	/// Objects held in the simulation's static tables (ship designs, weapon specs, module catalogues)
	/// are written by name, not copied, so a loaded hull still shares its design with every other hull
	/// of its class. Delegates are kept only where they point into this assembly (or the root's):
	/// subscribers from outside (the Unity views' toasts) subscribe to the loaded world again themselves.
	///
	/// Writing compacts every dictionary and set it passes (each is cleared and refilled in its own
	/// order), so the original and the copy hold their entries the same way and keep enumerating in
	/// step after later removals: the simulation iterates them, and a save must not fork the timeline.
	/// </summary>
	/// <remarks>C++: reflection is replaced by the SG_SERIALIZE registry (docs/PERSISTENCE.md). Delegates are written
	/// as null and wired again by SG_LOADED hooks; everything else is the C# format, byte for byte where the
	/// registrations are complete.</remarks>
	class SPACEGODCORE_API ObjectGraph
	{
	public:
		/// A root as read: the object and its registered type (C#'s `object`).
		struct ObjectRef
		{
			cs::Ref<void> Object;
			const Graph::ClassInfo* Class = nullptr;
			template <class T> cs::Ref<T> As() const
			{
				if (!Object || !Class) return nullptr;
				void* p = Class->Upcast(Object.get(), typeid(T));
				return p ? cs::Ref<T>(Object, static_cast<T*>(p)) : nullptr;
			}
			explicit operator bool() const { return Object != nullptr; }
		};

		static constexpr int Magic = 0x4A42474F; // "OGBJ"
		static constexpr int Version = 1;

		template <class T> static void Write(std::ostream& stream, const cs::Ref<T>& root)
		{
			Graph::Finalize();
			if (!root) { WriteRoot(stream, nullptr, nullptr); return; }
			WriteRoot(stream, Graph::ObjectCodecBase<T>::MostDerived(root.get()), Graph::ObjectCodecBase<T>::ClassOf(root.get()));
		}

		static ObjectRef Read(std::istream& stream);
		static ObjectRef Read(const uint8_t* data, size_t size);

		template <class T> static cs::List<uint8_t> ToBytes(const cs::Ref<T>& root)
		{
			Graph::Finalize();
			std::vector<uint8_t> bytes = root ? WriteBytes(Graph::ObjectCodecBase<T>::MostDerived(root.get()), Graph::ObjectCodecBase<T>::ClassOf(root.get())) : WriteBytes(nullptr, nullptr);
			return cs::List<uint8_t>(std::move(bytes));
		}

		static ObjectRef FromBytes(const cs::List<uint8_t>& data);

		/// <summary>A deep copy of <paramref name="root"/> by way of a write and a read.</summary>
		template <class T> static cs::Ref<T> Clone(const cs::Ref<T>& root) { return FromBytes(ToBytes(root)).template As<T>(); }

		// (C++) the untyped core of Write.
		static void WriteRoot(std::ostream& stream, void* root, const Graph::ClassInfo* cls);
		static std::vector<uint8_t> WriteBytes(void* root, const Graph::ClassInfo* cls);
	};
}

// ---------------------------------------------------------------- registration macros (docs/PERSISTENCE.md)
//
//   // Law.h, inside the class:      SG_SERIALIZABLE(Law);
//   // Law.cpp, in namespace SpaceGod::Core:
//   SG_SERIALIZE(Law, "Unity.SpaceGod.Core.Law",
//       SG_F(world), SG_F(Player), SG_P(Hail), SG_F(Committed), ...)
//
// Fields may be listed in any order (the registry sorts them by .NET name, as FieldsOf does). Every instance field
// the C# type declares must be listed -- private ones, auto-property backing fields (SG_P) and field-like events
// (SG_F: the backing field has the event's name) -- or the C# build refuses the C++ build's saves and vice versa.

#define SG_CONCAT_INNER(a, b) a##b
#define SG_CONCAT(a, b) SG_CONCAT_INNER(a, b)

/// A field whose .NET name is the C++ member's name.
#define SG_F(member) b.Field(#member, &Self::member)
/// A field whose .NET name differs from the C++ member's.
#define SG_FN(netName, member) b.Field(netName, &Self::member)
/// An auto-property: the backing field `<member>k__BackingField`.
#define SG_P(member) b.Property(#member, &Self::member)
/// A C# array `T[]` held as cs::List<T> (or cs::Ref<cs::List<T>>).
#define SG_ARRAY(member) b.Array(#member, &Self::member)
#define SG_PARRAY(member) b.Array(cs::Cat("<", #member, ">k__BackingField"), &Self::member)
/// A string field C# never leaves null.
#define SG_STR(member) b.String(#member, &Self::member)
/// A C# field with no C++ member: writes CppType's default, ignores the saved value.
#define SG_GHOST(netName, CppType) b.template Ghost<CppType>(netName)
/// The base class (registered itself); its fields come first.
#define SG_BASE(BaseType) b.template Base<BaseType>()
/// An interface the type implements (registered with SG_INTERFACE).
#define SG_IMPLEMENTS(InterfaceType) b.template Interface<InterfaceType>()
/// A hook run on each loaded instance once the whole graph is read.
#define SG_LOADED(fn) b.Loaded(fn)

/// A class held by cs::Ref (needs a default constructor, which may be private: the registration is a friend).
#define SG_SERIALIZE(T, NetFullName, ...) \
	template <> bool SpaceGod::Core::Graph::Registration<T>::Run() \
	{ \
		using Self = T; \
		auto b = ::SpaceGod::Core::Graph::Class<T>(NetFullName, [] { return ::cs::Ref<void>(::cs::Ref<T>(new T())); }); \
		::SpaceGod::Core::Graph::Fields(__VA_ARGS__); \
		return true; \
	} \
	[[maybe_unused]] static const bool SG_CONCAT(sgRegistered_, __LINE__) = ::SpaceGod::Core::Graph::Registration<T>::Run();

/// An abstract class (fields only; never made by a load).
#define SG_SERIALIZE_ABSTRACT(T, NetFullName, ...) \
	template <> bool SpaceGod::Core::Graph::Registration<T>::Run() \
	{ \
		using Self = T; \
		auto b = ::SpaceGod::Core::Graph::Class<T>(NetFullName, nullptr); \
		b.Abstract(); \
		::SpaceGod::Core::Graph::Fields(__VA_ARGS__); \
		return true; \
	} \
	[[maybe_unused]] static const bool SG_CONCAT(sgRegistered_, __LINE__) = ::SpaceGod::Core::Graph::Registration<T>::Run();

/// An interface (no fields): only names the declared type of fields that hold one.
#define SG_INTERFACE(T, NetFullName) \
	template <> bool SpaceGod::Core::Graph::Registration<T>::Run() { ::SpaceGod::Core::Graph::Interface<T>(NetFullName); return true; } \
	[[maybe_unused]] static const bool SG_CONCAT(sgRegistered_, __LINE__) = ::SpaceGod::Core::Graph::Registration<T>::Run();

/// A struct held by value (any by-value class type no other codec claims is taken for one).
#define SG_STRUCT(T, NetFullName, ...) \
	template <> bool SpaceGod::Core::Graph::Registration<T>::Run() \
	{ \
		using Self = T; \
		auto b = ::SpaceGod::Core::Graph::Struct<T>(NetFullName); \
		::SpaceGod::Core::Graph::Fields(__VA_ARGS__); \
		return true; \
	} \
	[[maybe_unused]] static const bool SG_CONCAT(sgRegistered_, __LINE__) = ::SpaceGod::Core::Graph::Registration<T>::Run();

/// An enum: its .NET name (underlying Int32; SG_ENUM_AS for another).
#define SG_ENUM(E, NetFullName) \
	[[maybe_unused]] static const bool SG_CONCAT(sgEnum_, __LINE__) = (::SpaceGod::Core::Graph::DeclareEnum(typeid(E), NetFullName, ::SpaceGod::Core::Graph::TypeCode::Int32), true);
#define SG_ENUM_AS(E, NetFullName, Code) \
	[[maybe_unused]] static const bool SG_CONCAT(sgEnum_, __LINE__) = (::SpaceGod::Core::Graph::DeclareEnum(typeid(E), NetFullName, ::SpaceGod::Core::Graph::TypeCode::Code), true);

/// A static field of a Core type that holds objects (see "Statics"): `expr` is an lvalue of the value it holds.
/// (A CppValueType with commas needs a `using` alias first.)
#define SG_STATIC(TypeFullName, FieldName, CppValueType, expr) \
	[[maybe_unused]] static const bool SG_CONCAT(sgStatic_, __LINE__) = (::SpaceGod::Core::Graph::DeclareStatic(TypeFullName, FieldName, \
		::SpaceGod::Core::Graph::StaticWalk<CppValueType>([]() -> CppValueType& { return const_cast<CppValueType&>(expr); })), true);
