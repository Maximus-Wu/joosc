#ifndef TYPES_TYPE_INFO_MAP_H
#define TYPES_TYPE_INFO_MAP_H

#include <algorithm>
#include <map>

#include "ast/ast.h"
#include "ast/ids.h"
#include "base/errorlist.h"
#include "base/fileset.h"

namespace types {

struct TypeIdList {
public:
  TypeIdList(const vector<ast::TypeId>& tids) : tids_(tids){}

  int Size() const {
    return tids_.size();
  }

  ast::TypeId At(int i) const {
    return tids_.at(i);
  }

  bool operator<(const TypeIdList& other) const {
    return std::lexicographical_compare(tids_.begin(), tids_.end(), other.tids_.begin(), other.tids_.end());
  }

  bool operator==(const TypeIdList& other) const {
    return tids_ == other.tids_;
  }
private:
  vector<ast::TypeId> tids_;
};

using MethodId = u64;

enum CallContext {
  INSTANCE,
  CONSTRUCTOR,
  STATIC,
};

struct MethodSignature {
  string name;
  TypeIdList param_types;

  bool operator<(const MethodSignature& other) const {
    return std::tie(name, param_types) < std::tie(other.name, other.param_types);
  }

  bool operator==(const MethodSignature& other) const {
    return name == other.name && param_types == other.param_types;
  }
};

struct MethodInfo {
  ast::TypeId class_type;
  ast::ModifierList mods;
  ast::TypeId return_type;
  base::PosRange pos;
  MethodSignature signature;
  bool is_constructor;

  bool operator<(const MethodInfo& other) const {
    return std::tie(class_type, is_constructor, signature) < std::tie(other.class_type, other.is_constructor, other.signature);
  }
};

struct MethodTableParam {
  MethodInfo minfo;
  MethodId mid;
};

class MethodTable {
public:
  // TODO
  MethodId ResolveCall(ast::TypeId callerType, CallContext ctx, const TypeIdList& params, base::ErrorList* out) const;

  // Given a valid MethodId, return all the associated info about it.
  const MethodInfo& LookupMethod(MethodId mid) const {
    auto info = method_info_.find(mid);
    assert(info != method_info_.end());
    return info->second;
  }

private:
  friend class TypeInfoMapBuilder;
  using MethodSignatureMap = std::map<MethodSignature, MethodId>;
  using MethodInfoMap = std::map<MethodId, MethodInfo>;

  void InsertMethod(MethodId mid, const MethodInfo& minfo) {
    method_signatures_.insert({minfo.signature, mid});
    method_info_.insert({mid, minfo});
  }

  MethodTable(const vector<MethodTableParam>& entries, const set<string>& bad_methods, bool has_bad_constructor) : has_bad_constructor_(has_bad_constructor), bad_methods_(bad_methods) {
    for (const auto& entry : entries) {
      InsertMethod(entry.mid, entry.minfo);
    }
  }

  MethodTable() : all_blacklisted_(true) {}

  static MethodTable kEmptyMethodTable;
  static MethodTable kErrorMethodTable;

  MethodSignatureMap method_signatures_;
  MethodInfoMap method_info_;

  // All blacklisting information.
  // Every call is blacklisted.
  bool all_blacklisted_ = false;
  // Any constructor is blacklisted.
  bool has_bad_constructor_ = false;
  // Specific method names are blacklisted.
  set<string> bad_methods_;
};

struct TypeInfo {
  ast::ModifierList mods;
  ast::TypeKind kind;
  ast::TypeId type;
  string name;
  base::PosRange pos;
  TypeIdList extends;
  TypeIdList implements;
  MethodTable methods;

  // Orders all types in topological order such that if there is a type A that
  // implements or extends another type B, then B has a lower top_sort_index
  // than A.
  u64 top_sort_index;

  bool operator<(const TypeInfo& other) const {
    return type < other.type;
  }
};

class TypeInfoMap {
public:
  static const TypeInfoMap& Empty() {
    return kEmptyTypeInfoMap;
  }

  pair<const TypeInfo&, bool> LookupTypeInfo(ast::TypeId tid) {
    auto info = type_info_.find(tid);
    assert(info != type_info_.end());
    // TODO: Bool?
    return make_pair(info->second, true);
  }

private:
  using Map = map<ast::TypeId, TypeInfo>;
  friend class TypeInfoMapBuilder;

  TypeInfoMap(const Map& typeinfo) : type_info_(typeinfo) {}

  static TypeInfoMap kEmptyTypeInfoMap;
  static TypeInfo kEmptyTypeInfo;

  Map type_info_;
};

class TypeInfoMapBuilder {
public:
  TypeInfoMapBuilder(const base::FileSet* fs) : fs_(fs) {}

  void PutType(ast::TypeId tid, const ast::TypeDecl& type, const vector<ast::TypeId>& extends, const vector<ast::TypeId>& implements) {
    assert(tid.ndims == 0);
    type_entries_.push_back(TypeInfo{type.Mods(), type.Kind(), tid, type.Name(), type.NameToken().pos, TypeIdList(extends), TypeIdList(implements), MethodTable::kEmptyMethodTable, tid.base});
  }

  void PutMethod(ast::TypeId curtid, ast::TypeId rettid, const vector<ast::TypeId>& paramtids, const ast::MemberDecl& meth, bool is_constructor) {
    method_entries_.push_back(MethodInfo{curtid, meth.Mods(), rettid, meth.NameToken().pos, MethodSignature{meth.Name(), TypeIdList(paramtids)}, is_constructor});
  }

  TypeInfoMap Build(base::ErrorList* out);

private:
  using MInfoIter = vector<MethodInfo>::iterator;
  using MInfoCIter = vector<MethodInfo>::const_iterator;

  void BuildMethodTable(MInfoIter begin, MInfoIter end, TypeInfo* tinfo, MethodId* cur_mid, const map<ast::TypeId, TypeInfo>& sofar, base::ErrorList* out);

  base::Error* MakeConstructorNameError(base::PosRange pos) const;

  const base::FileSet* fs_;
  vector<TypeInfo> type_entries_;
  vector<MethodInfo> method_entries_;
};

} // namespace types

#endif
