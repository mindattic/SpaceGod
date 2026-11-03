// Port of Unity.SpaceGod Core/Persistence/ObjectGraph.cs (US-T20). See ObjectGraph.h and docs/PERSISTENCE.md.

#include "Persistence/ObjectGraph.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <mutex>
#include <unordered_set>

namespace SpaceGod::Core
{
	namespace Graph
	{
		const char* CoreLibAssembly = "System.Private.CoreLib";
		const char* CollectionsAssembly = "System.Collections";
		const char* CoreAssembly = "Unity.SpaceGod.Core";

		namespace
		{
			// ------------------------------------------------------------ interned types

			struct TypeStore
			{
				std::mutex Gate;
				std::deque<NetType> Types;
				std::unordered_map<std::string, NetType*> ByKey;
			};
			TypeStore& Types()
			{
				static TypeStore store;
				return store;
			}

			cs::String PtrKey(const void* p) { return std::to_string(reinterpret_cast<uintptr_t>(p)); }

			NetType* Intern(const cs::String& key, const std::function<void(NetType&)>& init)
			{
				auto& s = Types();
				std::lock_guard<std::mutex> lock(s.Gate);
				auto it = s.ByKey.find(key);
				if (it != s.ByKey.end()) return it->second;
				s.Types.emplace_back();
				NetType* t = &s.Types.back();
				init(*t);
				s.ByKey[key] = t;
				return t;
			}

			// ------------------------------------------------------------ registry

			struct RegistryStore
			{
				std::recursive_mutex Gate;
				std::map<std::type_index, std::unique_ptr<ClassInfo>> Classes;
				std::unordered_map<cs::String, ClassInfo*> ByName;
				std::map<std::type_index, const NetType*> Enums;
				std::unordered_map<cs::String, TypeCode> EnumCodes;
				std::unordered_set<cs::String> Delegates;
				size_t FinalizedCount = static_cast<size_t>(-1);
			};
			RegistryStore& Registry()
			{
				static RegistryStore store;
				return store;
			}

			// ------------------------------------------------------------ statics

			struct StaticRoot
			{
				cs::String Type, Field;
				std::function<void(StaticWalker&, const cs::String&)> Walk;
			};
			struct StaticEntry
			{
				cs::String Key;
				const void* Addr = nullptr;
				std::type_index Cpp = typeid(void);
				cs::Ref<void> Obj;
				const ClassInfo* Class = nullptr;
			};
			struct StaticsStore
			{
				std::mutex Gate;
				std::vector<StaticRoot> Roots;
				bool Ready = false;
				std::vector<StaticEntry> Entries;
				std::unordered_map<cs::String, size_t> ByAddr;
				std::unordered_map<cs::String, size_t> ByKey;
			};
			StaticsStore& Statics()
			{
				static StaticsStore store;
				return store;
			}

			cs::String AddrKey(const void* addr, std::type_index cpp) { return cs::Cat(PtrKey(addr), "|", cpp.name()); }

			/// Walks every declared static, types by FullName and fields by name (ordinal), as Statics.Ensure does.
			void EnsureStatics()
			{
				auto& s = Statics();
				std::lock_guard<std::mutex> lock(s.Gate);
				if (s.Ready) return;
				Finalize();
				auto roots = s.Roots;
				std::stable_sort(roots.begin(), roots.end(), [](const StaticRoot& a, const StaticRoot& b)
				{
					int c = cs::Strings::CompareOrdinal(a.Type, b.Type);
					if (c != 0) return c < 0;
					return cs::Strings::CompareOrdinal(a.Field, b.Field) < 0;
				});
				StaticWalker walker;
				for (auto& r : roots) r.Walk(walker, cs::Cat(r.Type, "::", r.Field));
				s.Ready = true;
			}

			const StaticEntry* StaticByAddr(const void* addr, std::type_index cpp)
			{
				EnsureStatics();
				auto& s = Statics();
				auto it = s.ByAddr.find(AddrKey(addr, cpp));
				return it == s.ByAddr.end() ? nullptr : &s.Entries[it->second];
			}

			const StaticEntry* StaticByKey(const cs::String& key)
			{
				EnsureStatics();
				auto& s = Statics();
				auto it = s.ByKey.find(key);
				return it == s.ByKey.end() ? nullptr : &s.Entries[it->second];
			}

			const char* PrimitiveName(TypeCode c)
			{
				switch (c)
				{
					case TypeCode::Boolean: return "System.Boolean";
					case TypeCode::Char: return "System.Char";
					case TypeCode::SByte: return "System.SByte";
					case TypeCode::Byte: return "System.Byte";
					case TypeCode::Int16: return "System.Int16";
					case TypeCode::UInt16: return "System.UInt16";
					case TypeCode::Int32: return "System.Int32";
					case TypeCode::UInt32: return "System.UInt32";
					case TypeCode::Int64: return "System.Int64";
					case TypeCode::UInt64: return "System.UInt64";
					case TypeCode::Single: return "System.Single";
					case TypeCode::Double: return "System.Double";
					default: throw cs::NotSupportedException(cs::Cat("Primitive ", static_cast<int>(c)));
				}
			}

			bool PrimitiveCodeOf(const cs::String& name, TypeCode& code)
			{
				static const std::pair<const char*, TypeCode> table[] = {
					{ "System.Boolean", TypeCode::Boolean }, { "System.Char", TypeCode::Char }, { "System.SByte", TypeCode::SByte },
					{ "System.Byte", TypeCode::Byte }, { "System.Int16", TypeCode::Int16 }, { "System.UInt16", TypeCode::UInt16 },
					{ "System.Int32", TypeCode::Int32 }, { "System.UInt32", TypeCode::UInt32 }, { "System.Int64", TypeCode::Int64 },
					{ "System.UInt64", TypeCode::UInt64 }, { "System.Single", TypeCode::Single }, { "System.Double", TypeCode::Double },
				};
				for (auto& [n, c] : table)
					if (name == n) { code = c; return true; }
				return false;
			}

			bool IsUnsigned(TypeCode c) { return c == TypeCode::Byte || c == TypeCode::UInt16 || c == TypeCode::UInt32 || c == TypeCode::UInt64 || c == TypeCode::Char || c == TypeCode::Boolean; }
		}

		// ---------------------------------------------------------------- NetType

		cs::String NetType::Describe() const
		{
			if (Tag == 1) return cs::Cat(Element ? Element->Describe() : cs::String("?"), "[]");
			if (Tag == 2)
			{
				cs::String s = cs::Cat(FullName, "[");
				for (size_t i = 0; i < Args.size(); i++) s += cs::Cat(i > 0 ? "," : "", Args[i]->Describe());
				return s + "]";
			}
			return FullName;
		}

		const NetType* Plain(const cs::String& fullName, const cs::String& assembly, Kind kind, TypeCode code)
		{
			return Intern(cs::Cat("0|", fullName), [&](NetType& t)
			{
				t.Tag = 0;
				t.FullName = fullName;
				t.Assembly = assembly;
				t.TypeKind = kind;
				t.Code = code;
			});
		}

		const NetType* ArrayOf(const NetType* element)
		{
			return Intern(cs::Cat("1|", PtrKey(element)), [&](NetType& t)
			{
				t.Tag = 1;
				t.Rank = 1;
				t.Vector = true;
				t.Element = element;
				t.TypeKind = Kind::Array;
			});
		}

		const NetType* Generic(const cs::String& definition, const cs::String& assembly, std::vector<const NetType*> args, Kind kind)
		{
			cs::String key = cs::Cat("2|", definition);
			for (auto a : args) key += cs::Cat("|", PtrKey(a));
			return Intern(key, [&](NetType& t)
			{
				t.Tag = 2;
				t.FullName = definition;
				t.Assembly = assembly;
				t.Args = std::move(args);
				t.TypeKind = kind;
			});
		}

		void SetShape(const NetType* t, std::function<std::vector<FieldDecl>()> shape)
		{
			auto& s = Types();
			std::lock_guard<std::mutex> lock(s.Gate);
			auto* m = const_cast<NetType*>(t);
			if (!m->Shape) m->Shape = std::move(shape);
		}

		const NetType* Primitive(TypeCode code) { return Plain(PrimitiveName(code), CoreLibAssembly, Kind::Primitive, code); }
		const NetType* StringType() { return Plain("System.String", CoreLibAssembly, Kind::String, TypeCode::String); }
		const NetType* ObjectType()
		{
			const NetType* t = Plain("System.Object", CoreLibAssembly, Kind::Object);
			SetShape(t, [] { return std::vector<FieldDecl>(); });
			return t;
		}

		// ---------------------------------------------------------------- registry

		ClassInfo& Declare(std::type_index cpp, const cs::String& fullName, bool isStruct)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			auto it = reg.Classes.find(cpp);
			if (it != reg.Classes.end()) return *it->second;
			auto info = std::make_unique<ClassInfo>();
			ClassInfo* c = info.get();
			c->Cpp = cpp;
			c->FullName = fullName;
			size_t cut = fullName.find_last_of(".+");
			c->Name = cut == cs::String::npos ? fullName : fullName.substr(cut + 1);
			c->IsStruct = isStruct;
			const NetType* t = Plain(fullName, CoreAssembly, isStruct ? Kind::Struct : Kind::Object);
			{
				auto& s = Types();
				std::lock_guard<std::mutex> tl(s.Gate);
				auto* m = const_cast<NetType*>(t);
				m->Class = c;
				m->TypeKind = isStruct ? Kind::Struct : Kind::Object;
				m->Shape = [c] { Finalize(); return c->Shape(); };
			}
			c->Type = t;
			reg.ByName[fullName] = c;
			reg.Classes[cpp] = std::move(info);
			return *c;
		}

		const ClassInfo* Find(std::type_index cpp)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			auto it = reg.Classes.find(cpp);
			return it == reg.Classes.end() ? nullptr : it->second.get();
		}

		const ClassInfo* FindByName(const cs::String& fullName)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			auto it = reg.ByName.find(fullName);
			return it == reg.ByName.end() ? nullptr : it->second;
		}

		namespace
		{
			void Build(ClassInfo* c, std::unordered_set<ClassInfo*>& done)
			{
				if (done.count(c)) return;
				done.insert(c);
				c->Fields.clear();
				c->Base = nullptr;
				if (c->BaseCpp != std::type_index(typeid(void)))
				{
					auto& reg = Registry();
					auto it = reg.Classes.find(c->BaseCpp);
					if (it == reg.Classes.end()) throw cs::NotSupportedException(cs::Cat(c->FullName, ": its base class is not registered (SG_SERIALIZE)"));
					ClassInfo* b = it->second.get();
					Build(b, done);
					c->Base = b;
					auto toBase = c->ToBase;
					for (const auto& f : b->Fields)
					{
						FieldInfo g = f;
						auto w = f.Write;
						auto r = f.Read;
						auto k = f.Walk;
						g.Write = [w, toBase](Writer& wr, void* p) { w(wr, toBase(p)); };
						g.Read = [r, toBase](Reader& rd, const Value& v, void* p) { r(rd, v, toBase(p)); };
						g.Walk = [k, toBase](StaticWalker& s, void* p, const cs::String& key, int depth) { k(s, toBase(p), key, depth); };
						c->Fields.push_back(std::move(g));
					}
				}
				// Every instance field of a type and its bases, in a fixed order (base first, then by name).
				auto own = c->Own;
				std::stable_sort(own.begin(), own.end(), [](const FieldInfo& a, const FieldInfo& b) { return cs::Strings::CompareOrdinal(a.Name, b.Name) < 0; });
				for (auto& f : own) c->Fields.push_back(std::move(f));
			}
		}

		void Finalize()
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			if (reg.FinalizedCount == reg.Classes.size()) return;
			std::unordered_set<ClassInfo*> done;
			for (auto& [cpp, c] : reg.Classes) Build(c.get(), done);
			reg.FinalizedCount = reg.Classes.size();
		}

		void* ClassInfo::Upcast(void* p, std::type_index target) const
		{
			if (!p) return nullptr;
			if (Cpp == target) return p;
			for (const auto& [t, fn] : DirectUpcasts)
				if (t == target) return fn(p);
			for (const auto& [t, fn] : DirectUpcasts)
			{
				if (t == Cpp) continue;
				if (const ClassInfo* b = Find(t))
					if (void* q = b->Upcast(fn(p), target)) return q;
			}
			return nullptr;
		}

		std::vector<FieldDecl> ClassInfo::Shape() const
		{
			std::vector<FieldDecl> list;
			for (const auto& f : Fields) list.push_back({ f.Key, f.Type() });
			return list;
		}

		void DeclareEnum(std::type_index cpp, const cs::String& fullName, TypeCode underlying)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			reg.Enums[cpp] = Plain(fullName, CoreAssembly, Kind::Enum, underlying);
			reg.EnumCodes[fullName] = underlying;
		}

		const NetType* EnumType(std::type_index cpp)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			auto it = reg.Enums.find(cpp);
			if (it == reg.Enums.end()) throw cs::NotSupportedException(cs::Cat("Cannot save the enum ", cpp.name(), ": not registered (SG_ENUM)"));
			return it->second;
		}

		void DeclareDelegateType(const cs::String& fullName)
		{
			auto& reg = Registry();
			std::lock_guard<std::recursive_mutex> lock(reg.Gate);
			reg.Delegates.insert(fullName);
		}

		void DeclareStatic(const cs::String& typeFullName, const cs::String& fieldName, std::function<void(StaticWalker&, const cs::String&)> walk)
		{
			auto& s = Statics();
			std::lock_guard<std::mutex> lock(s.Gate);
			s.Roots.push_back({ typeFullName, fieldName, std::move(walk) });
			s.Ready = false;
			s.Entries.clear();
			s.ByAddr.clear();
			s.ByKey.clear();
		}

		cs::List<cs::String> StaticKeys()
		{
			EnsureStatics();
			cs::List<cs::String> keys;
			for (const auto& e : Statics().Entries) keys.Add(e.Key);
			return keys;
		}

		bool StaticWalker::Record(const void* addr, std::type_index cpp, const cs::String& key, cs::Ref<void> obj, const ClassInfo* cls)
		{
			auto& s = Statics(); // (EnsureStatics holds the lock)
			cs::String k = AddrKey(addr, cpp);
			if (s.ByAddr.count(k)) return false;
			StaticEntry e;
			e.Key = key;
			e.Addr = addr;
			e.Cpp = cpp;
			e.Obj = std::move(obj);
			e.Class = cls;
			s.Entries.push_back(std::move(e));
			s.ByAddr[k] = s.Entries.size() - 1;
			s.ByKey[key] = s.Entries.size() - 1;
			return true;
		}

		bool WalkCollection(StaticWalker& s, const void* addr, std::type_index cpp, const cs::String& key, int depth)
		{
			if (depth > StaticWalker::MaxDepth) return false;
			return s.Record(addr, cpp, key, nullptr, nullptr);
		}

		// ---------------------------------------------------------------- byte streams

		void ByteWriter::Raw(const void* p, size_t n)
		{
			const auto* b = static_cast<const uint8_t*>(p);
			Bytes.insert(Bytes.end(), b, b + n);
		}

		void ByteWriter::Str(const cs::String& s)
		{
			// Write7BitEncodedInt of the UTF-8 byte count.
			uint32_t v = static_cast<uint32_t>(s.size());
			while (v >= 0x80)
			{
				U8(static_cast<uint8_t>(v | 0x80));
				v >>= 7;
			}
			U8(static_cast<uint8_t>(v));
			Raw(s.data(), s.size());
		}

		void ByteWriter::Var(int value)
		{
			uint32_t v = static_cast<uint32_t>(value) << 1 ^ static_cast<uint32_t>(value >> 31);
			while (v >= 0x80)
			{
				U8(static_cast<uint8_t>(v | 0x80));
				v >>= 7;
			}
			U8(static_cast<uint8_t>(v));
		}

		uint8_t ByteReader::U8()
		{
			if (pos >= n) throw EndOfStreamException("Unable to read beyond the end of the stream.");
			return p[pos++];
		}

		void ByteReader::Raw(void* out, size_t k)
		{
			if (n - pos < k) throw EndOfStreamException("Unable to read beyond the end of the stream.");
			std::memcpy(out, p + pos, k);
			pos += k;
		}

		cs::String ByteReader::Str()
		{
			uint32_t len = 0;
			int shift = 0;
			uint8_t b;
			do
			{
				if (shift == 35) throw cs::FormatException("Too many bytes in what should have been a 7-bit encoded integer.");
				b = U8();
				len |= static_cast<uint32_t>(b & 0x7f) << shift;
				shift += 7;
			} while ((b & 0x80) != 0);
			if (static_cast<int32_t>(len) < 0) throw cs::IOException("Invalid string length");
			if (n - pos < len) throw EndOfStreamException("Unable to read beyond the end of the stream.");
			cs::String s(reinterpret_cast<const char*>(p + pos), len);
			pos += len;
			return s;
		}

		int ByteReader::Var()
		{
			uint32_t v = 0;
			int shift = 0;
			uint8_t b;
			do
			{
				b = U8();
				v |= static_cast<uint32_t>(b & 0x7f) << shift;
				shift += 7;
			} while ((b & 0x80) != 0);
			return static_cast<int>(v >> 1) ^ -static_cast<int>(v & 1);
		}

		void WritePrimitive(ByteWriter& w, TypeCode code, int64_t i, uint64_t u, double d)
		{
			switch (code)
			{
				case TypeCode::Boolean: w.Bool(u != 0); break;
				case TypeCode::Byte: w.U8(static_cast<uint8_t>(u)); break;
				case TypeCode::SByte: w.U8(static_cast<uint8_t>(static_cast<int8_t>(i))); break;
				case TypeCode::Char: w.U16(static_cast<uint16_t>(u)); break;
				case TypeCode::Int16: w.I16(static_cast<int16_t>(i)); break;
				case TypeCode::UInt16: w.U16(static_cast<uint16_t>(u)); break;
				case TypeCode::Int32: w.I32(static_cast<int32_t>(i)); break;
				case TypeCode::UInt32: w.U32(static_cast<uint32_t>(u)); break;
				case TypeCode::Int64: w.I64(i); break;
				case TypeCode::UInt64: w.U64(u); break;
				case TypeCode::Single: w.F32(static_cast<float>(d)); break;
				case TypeCode::Double: w.F64(d); break;
				default: throw cs::NotSupportedException(cs::Cat("Primitive ", static_cast<int>(code)));
			}
		}

		Value ReadPrimitive(ByteReader& r, TypeCode code)
		{
			Value v;
			v.ValueTag = Value::Tag::Prim;
			v.Code = code;
			switch (code)
			{
				case TypeCode::Boolean: v.Prim.U = r.Bool() ? 1 : 0; break;
				case TypeCode::Byte: v.Prim.U = r.U8(); break;
				case TypeCode::SByte: v.Prim.I = static_cast<int8_t>(r.U8()); break;
				case TypeCode::Char: v.Prim.U = r.U16(); break;
				case TypeCode::Int16: v.Prim.I = r.I16(); break;
				case TypeCode::UInt16: v.Prim.U = r.U16(); break;
				case TypeCode::Int32: v.Prim.I = r.I32(); break;
				case TypeCode::UInt32: v.Prim.U = r.U32(); break;
				case TypeCode::Int64: v.Prim.I = r.I64(); break;
				case TypeCode::UInt64: v.Prim.U = r.U64(); break;
				case TypeCode::Single: v.Prim.F = r.F32(); break;
				case TypeCode::Double: v.Prim.D = r.F64(); break;
				default: throw cs::NotSupportedException(cs::Cat("Primitive ", static_cast<int>(code)));
			}
			return v;
		}

		// ---------------------------------------------------------------- writer

		void Writer::BeginEntry(int id, EntryKind kind)
		{
			Table.Var(id);
			Table.U8(static_cast<uint8_t>(kind));
			EntryCount++;
		}

		int Writer::Meta(const NetType* t)
		{
			auto it = typeOf.find(t);
			if (it != typeOf.end()) return it->second;
			int id = static_cast<int>(types.size());
			typeOf[t] = id;
			types.push_back(t);
			shapes.emplace_back();
			hasShape.push_back(false);
			if (t->Tag == 1) Meta(t->Element);
			if (t->Tag == 2)
				for (auto a : t->Args) Meta(a);
			if (t->TypeKind == Kind::Struct || t->TypeKind == Kind::Object)
			{
				if (!t->Shape && t != ObjectType())
					throw cs::NotSupportedException(cs::Cat("Cannot save a ", t->Describe(), ": its fields are not known (register it with SG_SERIALIZE / SG_INTERFACE)"));
				std::vector<FieldDecl> shape = t->Shape ? t->Shape() : std::vector<FieldDecl>();
				hasShape[static_cast<size_t>(id)] = true;
				shapes[static_cast<size_t>(id)] = shape;
				for (const auto& f : shape) Meta(f.Type);
			}
			return id;
		}

		void Writer::WriteTypes(ByteWriter& w) const
		{
			w.I32(static_cast<int32_t>(types.size()));
			for (auto t : types)
			{
				if (t->Tag == 1)
				{
					w.U8(1);
					w.I32(t->Rank);
					w.U8(t->Vector ? 1 : 0);
					w.I32(typeOf.at(t->Element));
				}
				else if (t->Tag == 2)
				{
					w.U8(2);
					w.Str(t->FullName);
					w.Str(t->Assembly);
					w.I32(static_cast<int32_t>(t->Args.size()));
					for (auto a : t->Args) w.I32(typeOf.at(a));
				}
				else
				{
					w.U8(0);
					w.Str(t->FullName);
					w.Str(t->Assembly);
				}
			}
			for (size_t i = 0; i < types.size(); i++)
			{
				if (!hasShape[i])
				{
					w.I32(-1);
					continue;
				}
				w.I32(static_cast<int32_t>(shapes[i].size()));
				for (const auto& f : shapes[i])
				{
					w.Str(f.Key);
					w.I32(typeOf.at(f.Type));
				}
			}
		}

		int Writer::RefObject(void* mostDerived, const ClassInfo* cls)
		{
			if (!mostDerived) return 0;
			auto it = ids.find(mostDerived);
			if (it != ids.end()) return it->second;
			int id = nextId++;
			ids[mostDerived] = id;
			if (const StaticEntry* s = StaticByAddr(mostDerived, cls->Cpp))
			{
				BeginEntry(id, EntryKind::External);
				Table.Str(s->Key);
				return id;
			}
			if (cls->IsStruct) throw cs::NotSupportedException(cs::Cat("Cannot save a ", cls->FullName, " by reference"));
			int tid = Meta(cls->Type);
			BeginEntry(id, EntryKind::Object);
			Table.I32(tid);
			pending.push_back({ id, [mostDerived, cls](Writer& w) { for (const auto& f : cls->Fields) f.Write(w, mostDerived); } });
			return id;
		}

		int Writer::RefString(const cs::String& s, bool emptyIsNull)
		{
			if (s.empty() && emptyIsNull) return 0;
			auto it = strings.find(s);
			if (it != strings.end()) return it->second;
			int sid = nextId++;
			strings[s] = sid;
			BeginEntry(sid, EntryKind::String);
			Table.Str(s);
			return sid;
		}

		int Writer::RefEmptyArray(const NetType* type)
		{
			// Every empty array is the same as any other of its type (the runtime hands out shared ones
			// from more than one place): one entry per type, read back as Array.Empty.
			int et = Meta(type);
			auto it = emptyIds.find(type);
			if (it != emptyIds.end()) return it->second;
			int id = nextId++;
			emptyIds[type] = id;
			BeginEntry(id, EntryKind::EmptyArray);
			Table.I32(et);
			return id;
		}

		int Writer::RefCollection(const void* addr, std::type_index cpp, const NetType* type, int comparer, std::function<void(Writer&)> content)
		{
			cs::String key = AddrKey(addr, cpp);
			auto it = collectionIds.find(key);
			if (it != collectionIds.end()) return it->second;
			int id = nextId++;
			collectionIds[key] = id;
			if (const StaticEntry* s = StaticByAddr(addr, cpp))
			{
				BeginEntry(id, EntryKind::External);
				Table.Str(s->Key);
				return id;
			}
			int tid = Meta(type);
			BeginEntry(id, EntryKind::Collection);
			Table.I32(tid);
			Table.I32(comparer);
			pending.push_back({ id, std::move(content) });
			return id;
		}

		int Writer::RefArray(const void* addr, std::type_index cpp, const NetType* type, int length, std::function<void(Writer&)> content)
		{
			cs::String key = AddrKey(addr, cpp);
			auto it = collectionIds.find(key);
			if (it != collectionIds.end()) return it->second;
			int id = nextId++;
			collectionIds[key] = id;
			if (const StaticEntry* s = StaticByAddr(addr, cpp))
			{
				BeginEntry(id, EntryKind::External);
				Table.Str(s->Key);
				return id;
			}
			int tid = Meta(type);
			BeginEntry(id, EntryKind::Array);
			Table.I32(tid);
			Table.I32(1);
			Table.I32(length);
			pending.push_back({ id, std::move(content) });
			return id;
		}

		int Writer::RefCustomObject(const void* addr, std::type_index cpp, const NetType* type, std::function<void(Writer&)> content)
		{
			cs::String key = AddrKey(addr, cpp);
			auto it = collectionIds.find(key);
			if (it != collectionIds.end()) return it->second;
			int id = nextId++;
			collectionIds[key] = id;
			int tid = Meta(type);
			BeginEntry(id, EntryKind::Object);
			Table.I32(tid);
			pending.push_back({ id, std::move(content) });
			return id;
		}

		/// <summary>Breadth first, so a long chain of references never runs deep on the stack.</summary>
		int Writer::WriteAll(void* root, const ClassInfo* cls)
		{
			int rootId = root ? RefObject(root, cls) : 0;
			while (pendingHead < pending.size())
			{
				int id = pending[pendingHead].first;
				auto content = std::move(pending[pendingHead].second);
				pendingHead++;
				payload.Var(id);
				content(*this);
				PayloadCount++;
			}
			return rootId;
		}

		void WriteStructFields(Writer& w, const ClassInfo* c, void* p)
		{
			for (const auto& f : c->Fields) f.Write(w, p);
		}

		void WalkStructFields(StaticWalker& s, const ClassInfo* c, void* p, const cs::String& key, int depth)
		{
			if (depth > StaticWalker::MaxDepth) return;
			for (const auto& f : c->Fields) f.Walk(s, p, cs::Cat(key, ".", f.Name), depth + 1);
		}

		void WalkObjectFields(StaticWalker& s, const ClassInfo* c, void* p, const cs::String& key, int depth)
		{
			// Another assembly's non-generic class (System.Random): the C# walk does not look inside.
			if (c->External) return;
			for (const auto& f : c->Fields) f.Walk(s, p, cs::Cat(key, ".", f.Name), depth + 1);
		}

		// ---------------------------------------------------------------- reader

		int RType::IndexOfField(const cs::String& key) const
		{
			for (size_t i = 0; i < Fields.size(); i++)
				if (Fields[i].first == key) return static_cast<int>(i);
			return -1;
		}

		cs::String RType::Describe() const
		{
			if (Tag == 1) return cs::Cat(Element ? Element->Describe() : cs::String("?"), "[]");
			return FullName;
		}

		const Node& Reader::NodeOf(int id) const
		{
			if (id <= 0 || static_cast<size_t>(id) >= nodes.size()) throw InvalidDataException(cs::Cat("The save refers to a missing entry ", id));
			return nodes[static_cast<size_t>(id)];
		}

		Node& Reader::MutableNode(int id)
		{
			if (id <= 0 || static_cast<size_t>(id) >= nodes.size()) throw InvalidDataException(cs::Cat("The save refers to a missing entry ", id));
			return nodes[static_cast<size_t>(id)];
		}

		cs::Ref<void> Reader::ObjectAs(int id, std::type_index target, void*& converted)
		{
			converted = nullptr;
			const Node& n = NodeOf(id);
			if (!n.Obj || !n.Class) return nullptr;
			converted = n.Class->Upcast(n.Obj.get(), target);
			return n.Obj;
		}

		namespace
		{
			bool StartsWith(const cs::String& s, const char* p) { return s.rfind(p, 0) == 0; }

			Kind ResolveKind(RType& t)
			{
				if (t.Tag == 1) return Kind::Array;
				auto& reg = Registry();
				if (t.Tag == 2)
				{
					const cs::String& d = t.FullName;
					if (d == "System.Nullable`1") return Kind::Nullable;
					if (d == "System.Collections.Generic.List`1") return Kind::List;
					if (d == "System.Collections.Generic.Dictionary`2") return Kind::Dictionary;
					if (d == "System.Collections.Generic.HashSet`1") return Kind::HashSet;
					if (d == "System.Collections.Generic.Queue`1") return Kind::Queue;
					if (d == "System.Collections.Generic.Stack`1") return Kind::Stack;
					if (StartsWith(d, "System.Action`") || StartsWith(d, "System.Func`") || d == "System.Predicate`1" || d == "System.Comparison`1"
						|| d == "System.EventHandler`1" || d == "System.Converter`2" || reg.Delegates.count(d)) return Kind::Delegate;
					if (StartsWith(d, "System.ValueTuple`") || d == "System.Collections.Generic.KeyValuePair`2") return Kind::Struct;
					if (auto c = FindByName(d)) return c->IsStruct ? Kind::Struct : Kind::Object;
					if (t.HasFields) return Kind::Object;
					throw InvalidDataException(cs::Cat("The save holds a type this build does not have: ", d));
				}
				const cs::String& n = t.FullName;
				TypeCode code;
				if (PrimitiveCodeOf(n, code)) { t.Code = code; return Kind::Primitive; }
				if (n == "System.String") return Kind::String;
				if (n == "System.DateTime") return Kind::DateTime;
				if (n == "System.TimeSpan") return Kind::TimeSpan;
				if (n == "System.Decimal") return Kind::Decimal;
				if (n == "System.Object") return Kind::Object;
				if (n == "System.Action" || n == "System.EventHandler" || reg.Delegates.count(n)) return Kind::Delegate;
				if (n == "System.Random+CompatPrng") return Kind::Struct;
				if (StartsWith(n, "System.Random")) return Kind::Object;
				{
					auto it = reg.EnumCodes.find(n);
					if (it != reg.EnumCodes.end()) { t.Code = it->second; return Kind::Enum; }
				}
				if (auto c = FindByName(n)) return c->IsStruct ? Kind::Struct : Kind::Object;
				// A compiler-generated closure (<>c__DisplayClass..., a lambda's home) is a class with fields: kept as an
				// opaque object nothing in C++ binds to.
				if (t.HasFields) return Kind::Object;
				throw InvalidDataException(cs::Cat("The save holds a type this build does not have: ", n));
			}
		}

		void Reader::ReadTypes(ByteReader& r)
		{
			int n = r.I32();
			if (n < 0) throw InvalidDataException("Bad type table");
			types.assign(static_cast<size_t>(n), RType());
			for (int i = 0; i < n; i++)
			{
				RType& t = types[static_cast<size_t>(i)];
				t.Tag = r.U8();
				if (t.Tag == 1)
				{
					t.Rank = r.I32();
					t.Vector = r.U8() == 1;
					t.ElementId = r.I32();
				}
				else if (t.Tag == 2)
				{
					t.FullName = r.Str();
					t.Assembly = r.Str();
					int args = r.I32();
					for (int a = 0; a < args; a++) t.ArgIds.push_back(r.I32());
				}
				else
				{
					t.FullName = r.Str();
					t.Assembly = r.Str();
				}
			}
			auto at = [&](int id) -> RType* {
				if (id < 0 || id >= n) throw InvalidDataException("Bad type reference");
				return &types[static_cast<size_t>(id)];
			};
			std::vector<std::vector<int>> fieldTypeIds(static_cast<size_t>(n));
			for (int i = 0; i < n; i++)
			{
				int count = r.I32();
				if (count < 0) continue;
				RType& t = types[static_cast<size_t>(i)];
				t.HasFields = true;
				for (int f = 0; f < count; f++)
				{
					cs::String key = r.Str();
					fieldTypeIds[static_cast<size_t>(i)].push_back(r.I32());
					t.Fields.push_back({ key, nullptr });
				}
			}
			for (int i = 0; i < n; i++)
			{
				RType& t = types[static_cast<size_t>(i)];
				if (t.Tag == 1) t.Element = at(t.ElementId);
				for (int a : t.ArgIds) t.Args.push_back(at(a));
				for (size_t f = 0; f < t.Fields.size(); f++) t.Fields[f].second = at(fieldTypeIds[static_cast<size_t>(i)][f]);
				t.TypeKind = ResolveKind(t);
				switch (t.TypeKind)
				{
					case Kind::Nullable:
					case Kind::List:
					case Kind::HashSet:
					case Kind::Queue:
					case Kind::Stack:
						if (t.Args.empty()) throw InvalidDataException("Bad generic type");
						t.Element = t.Args[0];
						break;
					case Kind::Dictionary:
						if (t.Args.size() < 2) throw InvalidDataException("Bad generic type");
						t.Key = t.Args[0];
						t.Item = t.Args[1];
						break;
					default:
						break;
				}
				if (t.Tag == 0) t.Class = FindByName(t.FullName);
				if (t.Class && t.HasFields)
				{
					// Objects are made without running their constructors, so a field the save lacks would load as
					// null (not its initialiser) and break later: a save from before it was added is refused instead.
					t.FieldMap.assign(t.Fields.size(), -1);
					std::vector<bool> seen(t.Class->Fields.size(), false);
					for (size_t f = 0; f < t.Fields.size(); f++)
						for (size_t k = 0; k < t.Class->Fields.size(); k++)
							if (!seen[k] && t.Class->Fields[k].Key == t.Fields[f].first)
							{
								t.FieldMap[f] = static_cast<int>(k);
								seen[k] = true;
								break;
							}
					cs::List<cs::String> lacking;
					for (size_t k = 0; k < seen.size(); k++)
						if (!seen[k]) lacking.Add(t.Class->Fields[k].Key);
					if (lacking.Count() > 0)
						throw InvalidDataException(cs::Cat("The save is from an older build: ", t.FullName, " has fields it lacks (", cs::Strings::Join(", ", lacking), ")"));
				}
			}
		}

		Value Reader::ReadValue(ByteReader& r, const RType* m)
		{
			switch (m->TypeKind)
			{
				case Kind::Primitive:
				case Kind::Enum:
				{
					Value v = ReadPrimitive(r, m->Code);
					v.Type = m;
					return v;
				}
				case Kind::Nullable:
				{
					if (!r.Bool()) return Value();
					return ReadValue(r, m->Element);
				}
				case Kind::DateTime:
				case Kind::TimeSpan:
				{
					Value v;
					v.ValueTag = Value::Tag::Prim;
					v.Code = TypeCode::Int64;
					v.Prim.I = r.I64();
					v.Type = m;
					return v;
				}
				case Kind::Decimal:
				{
					Value v;
					v.ValueTag = Value::Tag::Struct;
					v.Type = m;
					for (int i = 0; i < 4; i++)
					{
						Value b = ReadPrimitive(r, TypeCode::Int32);
						v.Items.push_back(b);
					}
					return v;
				}
				case Kind::Struct:
				{
					Value v;
					v.ValueTag = Value::Tag::Struct;
					v.Type = m;
					for (const auto& f : m->Fields) v.Items.push_back(ReadValue(r, f.second));
					return v;
				}
				default:
					break;
			}
			int id = r.Var();
			Value v;
			if (id == 0) return v;
			if (id == -1)
			{
				int tid = r.I32();
				if (tid < 0 || static_cast<size_t>(tid) >= types.size()) throw InvalidDataException("Bad boxed type");
				v.ValueTag = Value::Tag::Boxed;
				v.Type = &types[static_cast<size_t>(tid)];
				v.Items.push_back(ReadValue(r, v.Type));
				return v;
			}
			v.ValueTag = Value::Tag::Ref;
			v.Id = id;
			return v;
		}

		void Reader::ReadContent(ByteReader& r, Node& n)
		{
			const RType* m = n.Type;
			if (!m) throw InvalidDataException("A payload for an entry with no type");
			switch (n.Entry)
			{
				case EntryKind::Object:
					for (const auto& f : m->Fields) n.Fields.push_back(ReadValue(r, f.second));
					break;
				case EntryKind::Array:
				{
					long long total = 1;
					for (int len : n.Lengths) total *= len;
					for (long long i = 0; i < total; i++) n.Items.push_back(ReadValue(r, m->Element));
					break;
				}
				case EntryKind::Collection:
				{
					int count = r.Var();
					if (m->TypeKind == Kind::Dictionary)
					{
						for (int i = 0; i < count; i++)
						{
							n.Keys.push_back(ReadValue(r, m->Key));
							n.Items.push_back(ReadValue(r, m->Item));
						}
					}
					else
						for (int i = 0; i < count; i++) n.Items.push_back(ReadValue(r, m->Element));
					break;
				}
				default:
					throw InvalidDataException(cs::Cat("Unexpected payload for entry kind ", static_cast<int>(n.Entry)));
			}
		}

		cs::Ref<void> Reader::ReadAll(ByteReader& r, const ClassInfo*& rootClass)
		{
			Finalize();
			ReadTypes(r);
			auto typeAt = [&](int id) -> const RType* {
				if (id < 0 || static_cast<size_t>(id) >= types.size()) throw InvalidDataException("Bad type reference");
				return &types[static_cast<size_t>(id)];
			};
			int entries = r.I32();
			std::vector<std::pair<int, Node>> made;
			made.reserve(entries > 0 ? static_cast<size_t>(entries) : 0);
			int maxId = 0;
			for (int e = 0; e < entries; e++)
			{
				int id = r.Var();
				maxId = cs::Math::Max(maxId, id);
				Node n;
				n.Entry = static_cast<EntryKind>(r.U8());
				switch (n.Entry)
				{
					case EntryKind::String:
						n.Str = r.Str();
						break;
					case EntryKind::External:
					{
						n.Str = r.Str();
						const StaticEntry* s = StaticByKey(n.Str);
						// A compiler-generated closure's cached instance (`World+<>c::<>9`, a C# lambda's target): only
						// delegates point at it, and C++ drops saved delegates, so it binds to nothing (as closures do).
						if (!s && n.Str.find("+<") != cs::String::npos) break;
						if (!s) throw InvalidDataException(cs::Cat("The save refers to a static object this build does not have: ", n.Str));
						n.Obj = s->Obj;
						n.Class = s->Class;
						n.StaticAddr = s->Addr;
						n.StaticCpp = s->Cpp;
						break;
					}
					case EntryKind::EmptyArray:
						n.Type = typeAt(r.I32());
						break;
					case EntryKind::Object:
						n.Type = typeAt(r.I32());
						break;
					case EntryKind::Array:
					{
						n.Type = typeAt(r.I32());
						int rank = r.I32();
						for (int i = 0; i < rank; i++) n.Lengths.push_back(r.I32());
						break;
					}
					case EntryKind::Collection:
						n.Type = typeAt(r.I32());
						n.Comparer = r.I32();
						break;
					case EntryKind::Delegate:
					{
						n.Type = typeAt(r.I32());
						int calls = r.I32();
						for (int i = 0; i < calls; i++)
						{
							Node::Call c;
							c.Declaring = typeAt(r.I32());
							c.Method = r.Str();
							int ps = r.I32();
							for (int p = 0; p < ps; p++) c.Params.push_back(typeAt(r.I32()));
							c.Target = r.Var();
							n.Calls.push_back(std::move(c));
						}
						break;
					}
					default:
						throw InvalidDataException(cs::Cat("Unknown entry ", static_cast<int>(n.Entry)));
				}
				made.push_back({ id, std::move(n) });
			}
			nodes.assign(static_cast<size_t>(maxId) + 1, Node());
			for (auto& [id, n] : made)
				if (id > 0) nodes[static_cast<size_t>(id)] = std::move(n);

			// Every object of a registered class is made first (empty), so references between them can close.
			for (auto& n : nodes)
				if (n.Entry == EntryKind::Object && n.Type && n.Type->Class && n.Type->Class->Make && !n.Type->Class->IsStruct)
				{
					n.Class = n.Type->Class;
					n.Obj = n.Class->Make();
				}

			int payloads = r.I32();
			std::vector<int> order;
			order.reserve(payloads > 0 ? static_cast<size_t>(payloads) : 0);
			for (int p = 0; p < payloads; p++)
			{
				int id = r.Var();
				ReadContent(r, MutableNode(id));
				order.push_back(id);
			}
			int rootId = r.I32();

			// Then filled in, in payload order.
			for (int id : order)
			{
				Node& n = MutableNode(id);
				if (n.Entry != EntryKind::Object || !n.Obj || !n.Class) continue;
				const RType* m = n.Type;
				for (size_t i = 0; i < n.Fields.size() && i < m->FieldMap.size(); i++)
				{
					int k = m->FieldMap[i];
					if (k >= 0) n.Class->Fields[static_cast<size_t>(k)].Read(*this, n.Fields[i], n.Obj.get());
				}
			}
			// What the save could not carry (delegates) is wired again by each class's SG_LOADED hook.
			for (int id : order)
			{
				Node& n = MutableNode(id);
				if (n.Entry == EntryKind::Object && n.Obj && n.Class && n.Class->Loaded) n.Class->Loaded(n.Obj.get());
			}

			rootClass = nullptr;
			if (rootId <= 0) return nullptr;
			const Node& root = NodeOf(rootId);
			rootClass = root.Class;
			return root.Obj;
		}

		void ReadStructFields(Reader& r, const ClassInfo* c, const Value& v, void* p)
		{
			// As C#'s Fits: only a saved value of this very struct goes into the field.
			if (v.ValueTag != Value::Tag::Struct || !v.Type || v.Type->Class != c) return;
			const RType* m = v.Type;
			for (size_t i = 0; i < v.Items.size() && i < m->FieldMap.size(); i++)
			{
				int k = m->FieldMap[i];
				if (k >= 0) c->Fields[static_cast<size_t>(k)].Read(r, v.Items[i], p);
			}
		}

		cs::Ref<void> SharedCollection(Reader& r, int id, std::type_index cpp, const std::function<cs::Ref<void>()>& make, const std::function<void(void*)>& fill)
		{
			Node& n = r.MutableNode(id);
			if (n.Shared) return n.SharedCpp == cpp ? n.Shared : nullptr;
			if (n.Entry == EntryKind::External)
			{
				if (n.StaticCpp != cpp || !n.StaticAddr) return nullptr;
				// The static table itself, shared, not owned.
				n.Shared = cs::Ref<void>(cs::Ref<void>(), const_cast<void*>(n.StaticAddr));
				n.SharedCpp = cpp;
				return n.Shared;
			}
			if (n.Entry != EntryKind::Collection && n.Entry != EntryKind::Array && n.Entry != EntryKind::EmptyArray) return nullptr;
			n.Shared = make();
			n.SharedCpp = cpp;
			fill(n.Shared.get());
			return n.Shared;
		}

		const void* StaticCollection(Reader& r, int id, std::type_index cpp)
		{
			const Node& n = r.NodeOf(id);
			if (n.Entry != EntryKind::External || n.StaticCpp != cpp) return nullptr;
			return n.StaticAddr;
		}

		// ---------------------------------------------------------------- delegates and tuples

		const NetType* DelegateType(int argCount, bool func, std::vector<const NetType*> args)
		{
			if (!func && argCount == 0) return Plain("System.Action", CoreLibAssembly, Kind::Delegate);
			return Generic(cs::Cat(func ? "System.Func`" : "System.Action`", argCount), CoreLibAssembly, std::move(args), Kind::Delegate);
		}

		const NetType* TupleType(std::vector<const NetType*> args)
		{
			int n = static_cast<int>(args.size());
			if (n < 1) throw cs::NotSupportedException("Cannot save an empty tuple");
			// Eight or more items: C#'s ValueTuple`8 {Item1..Item7, Rest}, Rest the tuple of the others (nested as deep as
			// it takes). Written inline, its bytes are the items in order, so the tuple codec writes them flat.
			if (n > 7)
			{
				std::vector<const NetType*> head(args.begin(), args.begin() + 7);
				const NetType* rest = TupleType(std::vector<const NetType*>(args.begin() + 7, args.end()));
				head.push_back(rest);
				auto copy8 = head;
				const NetType* t8 = Generic("System.ValueTuple`8", CoreLibAssembly, std::move(head), Kind::Struct);
				SetShape(t8, [copy8]
				{
					std::vector<FieldDecl> list;
					for (int i = 0; i < 7; i++) list.push_back({ cs::Cat("ValueTuple`8:Item", i + 1), copy8[static_cast<size_t>(i)] });
					list.push_back({ cs::String("ValueTuple`8:Rest"), copy8[7] });
					return list;
				});
				return t8;
			}
			auto copy = args;
			const NetType* t = Generic(cs::Cat("System.ValueTuple`", n), CoreLibAssembly, std::move(args), Kind::Struct);
			SetShape(t, [copy, n]
			{
				std::vector<FieldDecl> list;
				for (int i = 0; i < n; i++) list.push_back({ cs::Cat("ValueTuple`", n, ":Item", i + 1), copy[static_cast<size_t>(i)] });
				return list;
			});
			return t;
		}

		const Value* TupleItem(const Value& v, int index)
		{
			if (!v.Type) return nullptr;
			const cs::String& d = v.Type->FullName; // "System.ValueTuple`2"
			size_t dot = d.find_last_of('.');
			cs::String name = dot == cs::String::npos ? d : d.substr(dot + 1);
			if (index >= 7 && name == "ValueTuple`8")
			{
				// Item 8 on lives in Rest (ValueTuple`8's eighth field), itself a tuple.
				int r = v.Type->IndexOfField("ValueTuple`8:Rest");
				return r >= 0 && static_cast<size_t>(r) < v.Items.size() ? TupleItem(v.Items[static_cast<size_t>(r)], index - 7) : nullptr;
			}
			int k = v.Type->IndexOfField(cs::Cat(name, ":Item", index + 1));
			return k >= 0 && static_cast<size_t>(k) < v.Items.size() ? &v.Items[static_cast<size_t>(k)] : nullptr;
		}

		// ---------------------------------------------------------------- DateTime, TimeSpan

		const NetType* Codec<cs::DateTime>::Type() { return Plain("System.DateTime", CoreLibAssembly, Kind::DateTime); }
		void Codec<cs::DateTime>::Write(Writer& w, cs::DateTime& v) { w.Payload().I64(v.ToBinary()); }
		void Codec<cs::DateTime>::Read(Reader&, const Value& v, cs::DateTime& out)
		{
			if (v.ValueTag == Value::Tag::Prim && v.Type && v.Type->TypeKind == Kind::DateTime) out = cs::DateTime::FromBinary(v.Prim.I);
		}

		const NetType* Codec<cs::TimeSpan>::Type() { return Plain("System.TimeSpan", CoreLibAssembly, Kind::TimeSpan); }
		void Codec<cs::TimeSpan>::Write(Writer& w, cs::TimeSpan& v) { w.Payload().I64(v.Ticks()); }
		void Codec<cs::TimeSpan>::Read(Reader&, const Value& v, cs::TimeSpan& out)
		{
			if (v.ValueTag == Value::Tag::Prim && v.Type && v.Type->TypeKind == Kind::TimeSpan) out = cs::TimeSpan(v.Prim.I);
		}

		// ---------------------------------------------------------------- System.Random

		namespace
		{
			// cs::Random keeps its state private and has no accessor for it (a Cs.h gap): this mirrors its layout
			// (int seedArray[56]; int inext, inextp;), which the asserts pin.
			struct RandomState
			{
				int seedArray[56];
				int inext;
				int inextp;
			};
			static_assert(sizeof(RandomState) == sizeof(cs::Random), "cs::Random's layout changed: update RandomState");
			static_assert(std::is_standard_layout_v<cs::Random>, "cs::Random must stay standard-layout for RandomState");
			RandomState& StateOf(cs::Random& r) { return *reinterpret_cast<RandomState*>(&r); }

			struct RandomImplTag {};
			struct RandomSeedArrayTag {};

			const NetType* ImplBaseType()
			{
				const NetType* t = Plain("System.Random+ImplBase", CoreLibAssembly, Kind::Object);
				SetShape(t, [] { return std::vector<FieldDecl>(); });
				return t;
			}
			const NetType* PrngType()
			{
				const NetType* t = Plain("System.Random+CompatPrng", CoreLibAssembly, Kind::Struct);
				SetShape(t, []
				{
					return std::vector<FieldDecl>{
						{ "CompatPrng:_inext", Primitive(TypeCode::Int32) },
						{ "CompatPrng:_inextp", Primitive(TypeCode::Int32) },
						{ "CompatPrng:_seedArray", ArrayOf(Primitive(TypeCode::Int32)) },
					};
				});
				return t;
			}
			const NetType* SeedImplType()
			{
				// .NET 10's name (.NET 5-7 called it Net5CompatSeedImpl; the reader takes either).
				const NetType* t = Plain("System.Random+CompatSeedImpl", CoreLibAssembly, Kind::Object);
				SetShape(t, [] { return std::vector<FieldDecl>{ { "CompatSeedImpl:_prng", PrngType() } }; });
				return t;
			}

			const Value* FieldOf(const RType* t, const std::vector<Value>& items, const char* key)
			{
				if (!t) return nullptr;
				int k = t->IndexOfField(key);
				return k >= 0 && static_cast<size_t>(k) < items.size() ? &items[static_cast<size_t>(k)] : nullptr;
			}

			bool ReadInt(const Value* v, int& out)
			{
				if (!v || v->ValueTag != Value::Tag::Prim || v->Code != TypeCode::Int32) return false;
				out = static_cast<int>(v->Prim.I);
				return true;
			}

			bool ReadSeedArray(Reader& r, const Value* v, int (&out)[56])
			{
				if (!v || v->ValueTag != Value::Tag::Ref) return false;
				const Node& a = r.NodeOf(v->Id);
				if (a.Entry != EntryKind::Array || a.Items.size() != 56) return false;
				for (size_t i = 0; i < 56; i++)
					if (!ReadInt(&a.Items[i], out[i])) return false;
				return true;
			}
		}

		const NetType* Codec<cs::Random>::Type()
		{
			const NetType* t = Plain("System.Random", CoreLibAssembly, Kind::Object);
			SetShape(t, [] { return std::vector<FieldDecl>{ { "Random:_impl", ImplBaseType() } }; });
			return t;
		}

		void Codec<cs::Random>::Write(Writer& w, cs::Random& v)
		{
			cs::Random* p = &v;
			int id = w.RefCustomObject(p, typeid(cs::Random), Type(), [p](Writer& wr)
			{
				int impl = wr.RefCustomObject(p, typeid(RandomImplTag), SeedImplType(), [p](Writer& w2)
				{
					RandomState& s = StateOf(*p);
					// _prng, inline: _inext, _inextp, _seedArray (by name).
					w2.Payload().I32(s.inext);
					w2.Payload().I32(s.inextp);
					int arr = w2.RefArray(s.seedArray, typeid(RandomSeedArrayTag), ArrayOf(Primitive(TypeCode::Int32)), 56, [p](Writer& w3)
					{
						RandomState& st = StateOf(*p);
						for (int i = 0; i < 56; i++) w3.Payload().I32(st.seedArray[i]);
					});
					w2.Payload().Var(arr);
				});
				wr.Payload().Var(impl);
			});
			w.Payload().Var(id);
		}

		void Codec<cs::Random>::Read(Reader& r, const Value& v, cs::Random& out)
		{
			if (v.ValueTag != Value::Tag::Ref) return;
			const Node& n = r.NodeOf(v.Id);
			if (n.Entry != EntryKind::Object || !n.Type || n.Type->FullName != "System.Random") return;
			RandomState s{};
			if (const Value* impl = FieldOf(n.Type, n.Fields, "Random:_impl"))
			{
				// .NET 5+: Random._impl (CompatSeedImpl; Net5CompatSeedImpl before .NET 8) -> _prng (CompatPrng).
				if (impl->ValueTag != Value::Tag::Ref) return;
				const Node& in = r.NodeOf(impl->Id);
				bool net8 = in.Type && in.Type->FullName == "System.Random+CompatSeedImpl";
				if (in.Entry != EntryKind::Object || !in.Type || (!net8 && in.Type->FullName != "System.Random+Net5CompatSeedImpl"))
					throw InvalidDataException(cs::Cat("Cannot load a System.Random of kind ", in.Type ? in.Type->FullName : cs::String("?"), " (only seeded ones)"));
				const Value* prng = FieldOf(in.Type, in.Fields, net8 ? "CompatSeedImpl:_prng" : "Net5CompatSeedImpl:_prng");
				if (!prng || prng->ValueTag != Value::Tag::Struct) return;
				if (!ReadInt(FieldOf(prng->Type, prng->Items, "CompatPrng:_inext"), s.inext)) return;
				if (!ReadInt(FieldOf(prng->Type, prng->Items, "CompatPrng:_inextp"), s.inextp)) return;
				if (!ReadSeedArray(r, FieldOf(prng->Type, prng->Items, "CompatPrng:_seedArray"), s.seedArray)) return;
			}
			else
			{
				// Mono (Unity): Random {SeedArray, inext, inextp}.
				if (!ReadInt(FieldOf(n.Type, n.Fields, "Random:inext"), s.inext)) return;
				if (!ReadInt(FieldOf(n.Type, n.Fields, "Random:inextp"), s.inextp)) return;
				if (!ReadSeedArray(r, FieldOf(n.Type, n.Fields, "Random:SeedArray"), s.seedArray)) return;
			}
			StateOf(out) = s;
		}
	}

	// ---------------------------------------------------------------- ObjectGraph

	std::vector<uint8_t> ObjectGraph::WriteBytes(void* root, const Graph::ClassInfo* cls)
	{
		Graph::Finalize();
		Graph::Writer w;
		int rootId = w.WriteAll(root, cls);
		Graph::ByteWriter bw;
		bw.I32(Magic);
		bw.I32(Version);
		w.WriteTypes(bw);
		bw.I32(w.EntryCount);
		bw.Append(w.Table.Bytes);
		bw.I32(w.PayloadCount);
		bw.Append(w.Payload().Bytes);
		bw.I32(rootId);
		return std::move(bw.Bytes);
	}

	void ObjectGraph::WriteRoot(std::ostream& stream, void* root, const Graph::ClassInfo* cls)
	{
		auto bytes = WriteBytes(root, cls);
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream) throw cs::IOException("Could not write the object graph");
	}

	ObjectGraph::ObjectRef ObjectGraph::Read(const uint8_t* data, size_t size)
	{
		Graph::ByteReader br(data, size);
		if (br.I32() != Magic) throw InvalidDataException("Not an object graph");
		int version = br.I32();
		if (version != Version) throw InvalidDataException(cs::Cat("Unsupported object graph version ", version));
		Graph::Reader reader;
		ObjectRef result;
		const Graph::ClassInfo* cls = nullptr;
		result.Object = reader.ReadAll(br, cls);
		result.Class = cls;
		return result;
	}

	ObjectGraph::ObjectRef ObjectGraph::Read(std::istream& stream)
	{
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		return Read(bytes.data(), bytes.size());
	}

	ObjectGraph::ObjectRef ObjectGraph::FromBytes(const cs::List<uint8_t>& data)
	{
		const auto& raw = data.Raw();
		return Read(raw.data(), raw.size());
	}
}
