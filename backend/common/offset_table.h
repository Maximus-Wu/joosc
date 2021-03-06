#ifndef BACKEND_COMMON_OFFSET_TABLE_H
#define BACKEND_COMMON_OFFSET_TABLE_H

#include "ast/ids.h"
#include "ir/size.h"
#include "types/type_info_map.h"

namespace backend {
namespace common {

class OffsetTable {
public:
  using TypeMap = map<ast::TypeId, u64>;
  using FieldMap = map<ast::FieldId, u64>;
  using MethodMap = map<ast::MethodId, pair<u64, ast::TypeKind>>;
  using Vtable = vector<pair<ast::TypeId, ast::MethodId>>;
  using Itable = vector<std::tuple<u64, ast::TypeId, ast::MethodId>>;
  using VtableMap = map<ast::TypeId, Vtable>;
  using ItableMap = map<ast::TypeId, Itable>;
  using StaticFields = vector<pair<ast::FieldId, ir::SizeClass>>;
  using StaticFieldMap = map<ast::TypeId, StaticFields>;
  using NativeMap = map<ast::MethodId, string>;

  static OffsetTable Build(const types::TypeInfoMap& tinfo_map, u8 ptr_size);

  static u64 VtableOverhead(u8 ptr_size) {
    u64 type_info_size = (u64)ptr_size;
    u64 itable_ptr_size = (u64)ptr_size;
    return type_info_size + itable_ptr_size;
  }

  u64 SizeOf(ast::TypeId tid) const {
    CHECK(tid.ndims == 0);
    return type_sizes_.at(tid) + ObjectOverhead();
  }

  u64 OffsetOfField(ast::FieldId fid) const {
    return field_offsets_.at(fid) + ObjectOverhead();
  }

  pair<u64, ast::TypeKind> OffsetOfMethod(ast::MethodId mid) const {
    return method_offsets_.at(mid);
  }

  const Vtable& VtableOf(ast::TypeId tid) const {
    return vtables_.at(tid);
  }

  const Itable& ItableOf(ast::TypeId tid) const {
    return itables_.at(tid);
  }

  const StaticFields& StaticFieldsOf(ast::TypeId tid) const {
    return statics_.at(tid);
  }

  pair<string, bool> NativeCall(ast::MethodId mid) const {
    auto iter = natives_.find(mid);
    if (iter == natives_.end()) {
      return make_pair("", false);
    }
    return make_pair(iter->second, true);
  }

private:
  OffsetTable(const TypeMap& type_sizes, const FieldMap& field_offsets, const MethodMap& method_offsets, const VtableMap& vtables, const ItableMap& itables, const StaticFieldMap& statics, const NativeMap& natives, u8 ptr_size) : type_sizes_(type_sizes), field_offsets_(field_offsets), method_offsets_(method_offsets), vtables_(vtables), itables_(itables), statics_(statics), natives_(natives), ptr_size_(ptr_size) {}

  u64 ObjectOverhead() const {
    u64 vtable_ptr_size = (u64)ptr_size_;
    return vtable_ptr_size;
  }

  TypeMap type_sizes_;
  FieldMap field_offsets_;
  MethodMap method_offsets_;
  VtableMap vtables_;
  ItableMap itables_;
  StaticFieldMap statics_;
  NativeMap natives_;
  u8 ptr_size_;
};

} // namespace common
} // namespace backend

#endif
