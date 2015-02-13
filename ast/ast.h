#ifndef AST_AST_H
#define AST_AST_H

#include <iostream>

#include "ast/ids.h"
#include "ast/visitor.h"
#include "base/shared_ptr_vector.h"
#include "base/unique_ptr_vector.h"
#include "lexer/lexer.h"

namespace ast {

#define ACCEPT_VISITOR_ABSTRACT(type) \
  virtual sptr<const type> Accept(Visitor* visitor, sptr<const type> ptr) const = 0

#define ACCEPT_VISITOR(type, ret_type) \
  virtual sptr<const ret_type> Accept(Visitor* visitor, sptr<const ret_type> ptr) const { \
    sptr<const type> downcasted = std::dynamic_pointer_cast<const type, const ret_type>(ptr); \
    assert(downcasted != nullptr); \
    assert(downcasted.get() == this); \
    return visitor->Rewrite##type(*this, downcasted); \
  }


#define REF_GETTER(type, name, expr) \
  const type& name() const { return (expr); }

#define SPTR_GETTER(type, name, field) \
  const type& name() const { return *field; } \
  sptr<const type> name##Ptr() const { return field; }

#define VAL_GETTER(type, name, expr) \
  type name() const { return (expr); }

class QualifiedName final {
 public:
  QualifiedName(const vector<lexer::Token>& tokens, const vector<string>& parts,
                const string& name)
      : tokens_(tokens), parts_(parts), name_(name) {}

  QualifiedName() = default;
  QualifiedName(const QualifiedName&) = default;
  QualifiedName& operator=(const QualifiedName&) = default;

  void PrintTo(std::ostream* os) const { *os << name_; }

  REF_GETTER(string, Name, name_);
  REF_GETTER(vector<string>, Parts, parts_);
  REF_GETTER(vector<lexer::Token>, Tokens, tokens_);

 private:
  vector<lexer::Token>
      tokens_;            // [IDENTIFIER, DOT, IDENTIFIER, DOT, IDENTIFIER]
  vector<string> parts_;  // ["java", "lang", "String"]
  string name_;           // "java.lang.String"
};

class Type {
 public:
  virtual ~Type() = default;

  virtual void PrintTo(std::ostream* os) const = 0;

 protected:
  Type() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(Type);
};

class PrimitiveType : public Type {
 public:
  PrimitiveType(lexer::Token token) : token_(token) {}

  void PrintTo(std::ostream* os) const override { *os << token_.TypeInfo(); }

  VAL_GETTER(lexer::Token, GetToken, token_);

 private:
  DISALLOW_COPY_AND_ASSIGN(PrimitiveType);

  lexer::Token token_;
};

class ReferenceType : public Type {
 public:
  ReferenceType(const QualifiedName& name) : name_(name) {}

  void PrintTo(std::ostream* os) const override { name_.PrintTo(os); }

  REF_GETTER(QualifiedName, Name, name_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ReferenceType);

  QualifiedName name_;
};

class ArrayType : public Type {
 public:
  ArrayType(sptr<const Type> elemtype) : elemtype_(elemtype) {}

  void PrintTo(std::ostream* os) const override {
    *os << "array<";
    elemtype_->PrintTo(os);
    *os << '>';
  }

  REF_GETTER(Type, ElemType, *elemtype_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ArrayType);

  sptr<const Type> elemtype_;
};

class Expr {
 public:
  virtual ~Expr() = default;

  ACCEPT_VISITOR_ABSTRACT(Expr);

  VAL_GETTER(TypeId, GetTypeId, tid_);

 protected:
  Expr(TypeId tid = TypeId::Unassigned()) : tid_(tid) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(Expr);

  TypeId tid_;
};

class NameExpr : public Expr {
 public:
  NameExpr(const QualifiedName& name) : name_(name) {}

  ACCEPT_VISITOR(NameExpr, Expr);

  REF_GETTER(QualifiedName, Name, name_);

 private:
  DISALLOW_COPY_AND_ASSIGN(NameExpr);

  QualifiedName name_;
};

class InstanceOfExpr : public Expr {
 public:
  InstanceOfExpr(sptr<const Expr> lhs, lexer::Token instanceof, sptr<const Type> type)
      : lhs_(lhs), instanceof_(instanceof), type_(type) {}

  ACCEPT_VISITOR(InstanceOfExpr, Expr);

  SPTR_GETTER(Expr, Lhs, lhs_);
  VAL_GETTER(lexer::Token, InstanceOf, instanceof_);
  SPTR_GETTER(Type, GetType, type_);

 private:
  DISALLOW_COPY_AND_ASSIGN(InstanceOfExpr);

  sptr<const Expr> lhs_;
  lexer::Token instanceof_;
  sptr<const Type> type_;
};

class ParenExpr : public Expr {
 public:
  ParenExpr(sptr<const Expr> nested) : nested_(nested) { assert(nested_ != nullptr); }

  ACCEPT_VISITOR(ParenExpr, Expr);

  SPTR_GETTER(Expr, Nested, nested_);

 private:
  sptr<const Expr> nested_;
};

class BinExpr : public Expr {
 public:
  BinExpr(sptr<const Expr> lhs, lexer::Token op, sptr<const Expr> rhs)
      : op_(op), lhs_(lhs), rhs_(rhs) {
    assert(lhs != nullptr);
    assert(op.TypeInfo().IsBinOp());
    assert(rhs != nullptr);
  }

  ACCEPT_VISITOR(BinExpr, Expr);

  VAL_GETTER(lexer::Token, Op, op_);
  SPTR_GETTER(Expr, Lhs, lhs_);
  SPTR_GETTER(Expr, Rhs, rhs_);

 private:
  lexer::Token op_;
  sptr<const Expr> lhs_;
  sptr<const Expr> rhs_;
};

class UnaryExpr : public Expr {
 public:
  UnaryExpr(lexer::Token op, sptr<const Expr> rhs) : op_(op), rhs_(rhs) {
    assert(op.TypeInfo().IsUnaryOp());
    assert(rhs != nullptr);
  }

  ACCEPT_VISITOR(UnaryExpr, Expr);

  VAL_GETTER(lexer::Token, Op, op_);
  SPTR_GETTER(Expr, Rhs, rhs_);

 private:
  lexer::Token op_;
  sptr<const Expr> rhs_;
};

class LitExpr : public Expr {
 public:
  LitExpr(lexer::Token token) : token_(token) {}

  VAL_GETTER(lexer::Token, GetToken, token_);

 private:
  lexer::Token token_;
};

class BoolLitExpr : public LitExpr {
 public:
  BoolLitExpr(lexer::Token token) : LitExpr(token) {}

  ACCEPT_VISITOR(BoolLitExpr, Expr);
};

class IntLitExpr : public LitExpr {
 public:
  IntLitExpr(lexer::Token token, const string& value)
      : LitExpr(token), value_(value) {}

  ACCEPT_VISITOR(IntLitExpr, Expr);

  REF_GETTER(string, Value, value_);

 private:
  string value_;
};

class StringLitExpr : public LitExpr {
 public:
  StringLitExpr(lexer::Token token) : LitExpr(token) {}

  ACCEPT_VISITOR(StringLitExpr, Expr);
};

class CharLitExpr : public LitExpr {
 public:
  CharLitExpr(lexer::Token token) : LitExpr(token) {}

  ACCEPT_VISITOR(CharLitExpr, Expr);
};

class NullLitExpr : public LitExpr {
 public:
  NullLitExpr(lexer::Token token) : LitExpr(token) {}

  ACCEPT_VISITOR(NullLitExpr, Expr);
};

class ThisExpr : public Expr {
 public:
  ACCEPT_VISITOR(ThisExpr, Expr);
};

class ArrayIndexExpr : public Expr {
 public:
  ArrayIndexExpr(sptr<const Expr> base, sptr<const Expr> index) : base_(base), index_(index) {}

  ACCEPT_VISITOR(ArrayIndexExpr, Expr);

  SPTR_GETTER(Expr, Base, base_);
  SPTR_GETTER(Expr, Index, index_);

 private:
  sptr<const Expr> base_;
  sptr<const Expr> index_;
};

class FieldDerefExpr : public Expr {
 public:
  FieldDerefExpr(sptr<const Expr> base, const string& fieldname, lexer::Token token)
      : base_(base), fieldname_(fieldname), token_(token) {}

  ACCEPT_VISITOR(FieldDerefExpr, Expr);

  SPTR_GETTER(Expr, Base, base_);
  REF_GETTER(string, FieldName, fieldname_);
  REF_GETTER(lexer::Token, GetToken, token_);

 private:
  sptr<const Expr> base_;
  string fieldname_;
  lexer::Token token_;
};

class CallExpr : public Expr {
 public:
  CallExpr(sptr<const Expr> base, lexer::Token lparen, const base::SharedPtrVector<const Expr>& args)
      : base_(base), lparen_(lparen), args_(args) {}

  ACCEPT_VISITOR(CallExpr, Expr);

  SPTR_GETTER(Expr, Base, base_);
  VAL_GETTER(lexer::Token, Lparen, lparen_);
  REF_GETTER(base::SharedPtrVector<const Expr>, Args, args_);

 private:
  sptr<const Expr> base_;
  lexer::Token lparen_;
  base::SharedPtrVector<const Expr> args_;
};

class CastExpr : public Expr {
 public:
  CastExpr(sptr<const Type> type, sptr<const Expr> expr) : type_(type), expr_(expr) {}

  ACCEPT_VISITOR(CastExpr, Expr);

  SPTR_GETTER(Type, GetType, type_);
  SPTR_GETTER(Expr, GetExpr, expr_);

 private:
  DISALLOW_COPY_AND_ASSIGN(CastExpr);

  sptr<const Type> type_;
  sptr<const Expr> expr_;
};

class NewClassExpr : public Expr {
 public:
  NewClassExpr(lexer::Token newTok, sptr<const Type> type, const base::SharedPtrVector<const Expr>& args)
      : newTok_(newTok), type_(type), args_(args) {}

  ACCEPT_VISITOR(NewClassExpr, Expr);

  VAL_GETTER(lexer::Token, NewToken, newTok_);
  SPTR_GETTER(Type, GetType, type_);
  REF_GETTER(base::SharedPtrVector<const Expr>, Args, args_);

 private:
  DISALLOW_COPY_AND_ASSIGN(NewClassExpr);

  lexer::Token newTok_;
  sptr<const Type> type_;
  base::SharedPtrVector<const Expr> args_;
};

class NewArrayExpr : public Expr {
 public:
  NewArrayExpr(sptr<const Type> type, sptr<const Expr> expr) : type_(type), expr_(expr) {}

  ACCEPT_VISITOR(NewArrayExpr, Expr);

  SPTR_GETTER(Type, GetType, type_);
  VAL_GETTER(sptr<const Expr>, GetExprPtr, expr_);

 private:
  DISALLOW_COPY_AND_ASSIGN(NewArrayExpr);

  sptr<const Type> type_;
  sptr<const Expr> expr_; // Can be nullptr.
};

class Stmt {
 public:
  virtual ~Stmt() = default;

  ACCEPT_VISITOR_ABSTRACT(Stmt);

 protected:
  Stmt() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(Stmt);
};

class EmptyStmt : public Stmt {
 public:
  EmptyStmt() = default;

  ACCEPT_VISITOR(EmptyStmt, Stmt);
};

class LocalDeclStmt : public Stmt {
 public:
  LocalDeclStmt(sptr<const Type> type, lexer::Token ident, sptr<const Expr> expr)
      : type_(type), ident_(ident), expr_(expr) {}

  ACCEPT_VISITOR(LocalDeclStmt, Stmt);

  SPTR_GETTER(Type, GetType, type_);
  VAL_GETTER(lexer::Token, Ident, ident_);
  SPTR_GETTER(Expr, GetExpr, expr_);

  // TODO: get the identifier as a string.

 private:
  sptr<const Type> type_;
  lexer::Token ident_;
  sptr<const Expr> expr_;
};

class ReturnStmt : public Stmt {
 public:
  ReturnStmt(sptr<const Expr> expr) : expr_(expr) {}

  ACCEPT_VISITOR(ReturnStmt, Stmt);

  VAL_GETTER(sptr<const Expr>, GetExprPtr, expr_);

 private:
  sptr<const Expr> expr_; // Can be nullptr.
};

class ExprStmt : public Stmt {
 public:
  ExprStmt(sptr<const Expr> expr) : expr_(expr) {}

  ACCEPT_VISITOR(ExprStmt, Stmt);

  SPTR_GETTER(Expr, GetExpr, expr_);

 private:
  sptr<const Expr> expr_;
};

class BlockStmt : public Stmt {
 public:
  BlockStmt(const base::SharedPtrVector<const Stmt>& stmts)
      : stmts_(stmts) {}

  ACCEPT_VISITOR(BlockStmt, Stmt);

  REF_GETTER(base::SharedPtrVector<const Stmt>, Stmts, stmts_);

 private:
  DISALLOW_COPY_AND_ASSIGN(BlockStmt);
  base::SharedPtrVector<const Stmt> stmts_;
};

class IfStmt : public Stmt {
 public:
  IfStmt(sptr<const Expr> cond, sptr<const Stmt> trueBody, sptr<const Stmt> falseBody)
      : cond_(cond), trueBody_(trueBody), falseBody_(falseBody) {}

  ACCEPT_VISITOR(IfStmt, Stmt);

  SPTR_GETTER(Expr, Cond, cond_);
  SPTR_GETTER(Stmt, TrueBody, trueBody_);
  SPTR_GETTER(Stmt, FalseBody, falseBody_);

 private:
  DISALLOW_COPY_AND_ASSIGN(IfStmt);

  sptr<const Expr> cond_;
  sptr<const Stmt> trueBody_;
  sptr<const Stmt> falseBody_;
};

class ForStmt : public Stmt {
 public:
  ForStmt(sptr<const Stmt> init, sptr<const Expr> cond, sptr<const Expr> update, sptr<const Stmt> body)
      : init_(init), cond_(cond), update_(update), body_(body) {}

  ACCEPT_VISITOR(ForStmt, Stmt);

  SPTR_GETTER(Stmt, Init, init_);
  VAL_GETTER(sptr<const Expr>, CondPtr, cond_);
  VAL_GETTER(sptr<const Expr>, UpdatePtr, update_);
  SPTR_GETTER(Stmt, Body, body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ForStmt);

  sptr<const Stmt> init_;
  sptr<const Expr> cond_;    // May be nullptr.
  sptr<const Expr> update_;  // May be nullptr.
  sptr<const Stmt> body_;
};

class WhileStmt : public Stmt {
 public:
  WhileStmt(sptr<const Expr> cond, sptr<const Stmt> body) : cond_(cond), body_(body) {}

  ACCEPT_VISITOR(WhileStmt, Stmt);

  SPTR_GETTER(Expr, Cond, cond_);
  SPTR_GETTER(Stmt, Body, body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(WhileStmt);

  sptr<const Expr> cond_;
  sptr<const Stmt> body_;
};

class ModifierList {
 public:
  ModifierList()
      : mods_(int(lexer::NUM_MODIFIERS),
              lexer::Token(lexer::K_NULL, base::PosRange(0, 0, 0))) {}

  ModifierList(const ModifierList&) = default;

  void PrintTo(std::ostream* os) const {
    for (int i = 0; i < lexer::NUM_MODIFIERS; ++i) {
      if (!HasModifier((lexer::Modifier)i)) {
        continue;
      }
      *os << mods_[i].TypeInfo() << ' ';
    }
  }

  bool HasModifier(lexer::Modifier m) const {
    return mods_[m].TypeInfo().IsModifier();
  }

  bool AddModifier(lexer::Token t) {
    if (!t.TypeInfo().IsModifier()) {
      return false;
    }
    lexer::Modifier m = t.TypeInfo().GetModifier();
    if (HasModifier(m)) {
      return false;
    }
    mods_[m] = t;
    return true;
  }

  lexer::Token GetModifierToken(lexer::Modifier m) const {
    assert(HasModifier(m));
    return mods_[m];
  }

 private:
  vector<lexer::Token> mods_;
};

class Param final {
 public:
  Param(sptr<const Type> type, lexer::Token ident) : type_(type), ident_(ident) {}

  ACCEPT_VISITOR(Param, Param);

  REF_GETTER(Type, GetType, *type_);
  VAL_GETTER(lexer::Token, Ident, ident_);

 private:
  DISALLOW_COPY_AND_ASSIGN(Param);

  sptr<const Type> type_;
  lexer::Token ident_;
};

class ParamList final {
 public:
  ParamList(const ParamList& other) = default;
  ~ParamList() = default;
  ParamList(const base::SharedPtrVector<const Param>& params)
      : params_(params) {}

  ACCEPT_VISITOR(ParamList, ParamList);

  REF_GETTER(base::SharedPtrVector<const Param>, Params, params_);

 private:
  base::SharedPtrVector<const Param> params_;
};

class MemberDecl {
 public:
  MemberDecl(const ModifierList& mods, lexer::Token ident)
      : mods_(mods), ident_(ident) {}
  virtual ~MemberDecl() = default;

  ACCEPT_VISITOR_ABSTRACT(MemberDecl);

  REF_GETTER(ModifierList, Mods, mods_);
  VAL_GETTER(lexer::Token, Ident, ident_);

 private:
  DISALLOW_COPY_AND_ASSIGN(MemberDecl);

  ModifierList mods_;
  lexer::Token ident_;
};

class FieldDecl : public MemberDecl {
 public:
  FieldDecl(const ModifierList& mods, sptr<const Type> type, lexer::Token ident, sptr<const Expr> val)
      : MemberDecl(mods, ident),
        type_(type),
        val_(val) {}

  ACCEPT_VISITOR(FieldDecl, MemberDecl);

  SPTR_GETTER(Type, GetType, type_);
  VAL_GETTER(sptr<const Expr>, ValPtr, val_);

 private:
  DISALLOW_COPY_AND_ASSIGN(FieldDecl);

  sptr<const Type> type_;
  sptr<const Expr> val_;  // Might be nullptr.
};

class MethodDecl : public MemberDecl {
 public:
  MethodDecl(const ModifierList& mods, sptr<const Type> type, lexer::Token ident,
             sptr<const ParamList> params, sptr<const Stmt> body)
      : MemberDecl(mods, ident),
        type_(type),
        params_(params),
        body_(body) {}

  ACCEPT_VISITOR(MethodDecl, MemberDecl);

  VAL_GETTER(sptr<const Type>, TypePtr, type_);
  SPTR_GETTER(ParamList, Params, params_);
  SPTR_GETTER(Stmt, Body, body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(MethodDecl);

  sptr<const Type> type_; // nullptr for constructors.
  sptr<const ParamList> params_;
  sptr<const Stmt> body_;
};

enum class TypeKind {
  CLASS,
  INTERFACE,
};

class TypeDecl final {
 public:
  TypeDecl(const ModifierList& mods, TypeKind kind, const string& name, lexer::Token nameToken,
           const vector<QualifiedName>& extends, const vector<QualifiedName>& implements,
           const base::SharedPtrVector<const MemberDecl>& members, TypeId tid = TypeId::Unassigned())
      : mods_(mods),
        kind_(kind),
        name_(name),
        nameToken_(nameToken),
        extends_(extends),
        implements_(implements),
        members_(members),
        tid_(tid) {}

  virtual ~TypeDecl() = default;

  ACCEPT_VISITOR(TypeDecl, TypeDecl);

  REF_GETTER(ModifierList, Mods, mods_);
  VAL_GETTER(TypeKind, Kind, kind_);
  REF_GETTER(string, Name, name_);
  VAL_GETTER(lexer::Token, NameToken, nameToken_);
  REF_GETTER(vector<QualifiedName>, Extends, extends_);
  REF_GETTER(vector<QualifiedName>, Implements, implements_);
  REF_GETTER(base::SharedPtrVector<const MemberDecl>, Members, members_);

  VAL_GETTER(TypeId, GetTypeId, tid_);

 private:
  DISALLOW_COPY_AND_ASSIGN(TypeDecl);

  ModifierList mods_;
  TypeKind kind_;
  string name_;
  lexer::Token nameToken_;
  vector<QualifiedName> extends_;
  vector<QualifiedName> implements_;
  base::SharedPtrVector<const MemberDecl> members_;

  TypeId tid_;
};

class ImportDecl final {
 public:
  ImportDecl(const QualifiedName& name, bool isWildCard)
      : name_(name), isWildCard_(isWildCard) {}
  ImportDecl(const ImportDecl&) = default;

  REF_GETTER(QualifiedName, Name, name_);
  VAL_GETTER(bool, IsWildCard, isWildCard_);

 private:
  QualifiedName name_;
  bool isWildCard_;
};

class CompUnit final {
 public:
  CompUnit(sptr<const QualifiedName> package, const vector<ImportDecl>& imports,
           const base::SharedPtrVector<const TypeDecl>& types)
      : package_(package),
        imports_(imports),
        types_(types) {}

  ACCEPT_VISITOR(CompUnit, CompUnit);

  VAL_GETTER(sptr<const QualifiedName>, PackagePtr, package_);
  REF_GETTER(vector<ImportDecl>, Imports, imports_);
  REF_GETTER(base::SharedPtrVector<const TypeDecl>, Types, types_);

 private:
  sptr<const QualifiedName> package_;  // Might be nullptr.
  vector<ImportDecl> imports_;
  base::SharedPtrVector<const TypeDecl> types_;
};

class Program final {
 public:
  Program(const base::SharedPtrVector<const CompUnit>& units)
      : units_(units) {}

  ACCEPT_VISITOR(Program, Program);

  REF_GETTER(base::SharedPtrVector<const CompUnit>, CompUnits, units_);

 private:
  DISALLOW_COPY_AND_ASSIGN(Program);

  base::SharedPtrVector<const CompUnit> units_;
};

#undef ACCEPT_VISITOR_ABSTRACT
#undef ACCEPT_VISITOR
#undef REF_GETTER
#undef VAL_GETTER

}  // namespace ast

#endif
