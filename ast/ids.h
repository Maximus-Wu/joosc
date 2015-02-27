#ifndef AST_IDS_H
#define AST_IDS_H

namespace ast {

struct TypeId {
  using Base = u64;

  static const Base kUnassignedBase = 0;
  static const Base kErrorBase = 1;
  static const Base kNullBase = 2;
  static const Base kVoidBase = 3;
  static const Base kBoolBase = 4;
  static const Base kByteBase = 5;
  static const Base kCharBase = 6;
  static const Base kShortBase = 7;
  static const Base kIntBase = 8;
  static const Base kFirstRefTypeBase = 16;

  static const TypeId kUnassigned;
  static const TypeId kError;
  static const TypeId kNull;
  static const TypeId kVoid;
  static const TypeId kBool;
  static const TypeId kByte;
  static const TypeId kChar;
  static const TypeId kShort;
  static const TypeId kInt;

  bool IsUnassigned() const {
    return base == kUnassignedBase;
  }
  bool IsError() const {
    return base == kErrorBase;
  }
  bool IsValid() const {
    return !IsUnassigned() && !IsError();
  }

  bool operator==(const TypeId& other) const {
    return base == other.base && ndims == other.ndims;
  }
  bool operator!=(const TypeId& other) const {
    return !(*this == other);
  }
  bool operator<(const TypeId& other) const {
    return std::tie(base, ndims) < std::tie(other.base, other.ndims);
  }
  bool operator>(const TypeId& other) const {
    return other < *this;
  }

  Base base;
  u64 ndims;
};

} // namespace ast

#endif
