#ifndef PARSER_AST_H
#define PARSER_AST_H

#include "lexer/lexer.h"
#include "parser/visitor.h"
#include "typing/rewriter.h"
#include <iostream>

namespace parser {

class Rewriter;

#define ACCEPT_VISITOR_INTERNAL(type, ret_type, body) \
  virtual void Accept(Visitor* visitor) const body; \
  virtual ret_type* Rewrite(Rewriter* visitor) const body

#define ACCEPT_VISITOR(type, ret_type) \
  ACCEPT_VISITOR_INTERNAL(type, ret_type, { return visitor->Visit##type(*this); })

#define ACCEPT_VISITOR_ABSTRACT(type) \
  ACCEPT_VISITOR_INTERNAL(type, type, = 0)

#define REF_GETTER(type, name, expr) \
  const type& name() const { return (expr); }

#define VAL_GETTER(type, name, expr) \
  type name() const { return (expr); }

class QualifiedName final {
 public:
  QualifiedName(const vector<lexer::Token>& tokens, const vector<string>& parts,
                const string& name)
      : tokens_(tokens), parts_(parts), name_(name) {}

  QualifiedName() = default;
  QualifiedName(const QualifiedName&) = default;
  QualifiedName(QualifiedName&&) = default;

  void PrintTo(std::ostream* os) const { *os << name_; }

  REF_GETTER(string, Name, name_);

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

  virtual Type* clone() const = 0;

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

  virtual Type* clone() const {
    return new PrimitiveType(token_);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PrimitiveType);

  lexer::Token token_;
};

class ReferenceType : public Type {
 public:
  ReferenceType(const QualifiedName& name) : name_(name) {}

  void PrintTo(std::ostream* os) const override { name_.PrintTo(os); }

  REF_GETTER(QualifiedName, Name, name_);

  virtual Type* clone() const {
    return new ReferenceType(name_);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ReferenceType);

  QualifiedName name_;
};

class ArrayType : public Type {
 public:
  ArrayType(Type* elemtype) : elemtype_(elemtype) {}

  void PrintTo(std::ostream* os) const override {
    *os << "array<";
    elemtype_->PrintTo(os);
    *os << '>';
  }

  REF_GETTER(Type, ElemType, *elemtype_);

  virtual Type* clone() const {
    return new ArrayType(elemtype_->clone());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ArrayType);

  unique_ptr<Type> elemtype_;
};

class Expr {
 public:
  virtual ~Expr() = default;

  ACCEPT_VISITOR_ABSTRACT(Expr);

 protected:
  Expr() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(Expr);
};

class ArgumentList final {
 public:
  ArgumentList(base::UniquePtrVector<Expr>&& args)
      : args_(std::forward<base::UniquePtrVector<Expr>>(args)) {}
  ~ArgumentList() = default;
  ArgumentList(ArgumentList&&) = default;

  ACCEPT_VISITOR(ArgumentList, ArgumentList);

  REF_GETTER(base::UniquePtrVector<Expr>, Args, args_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ArgumentList);

  base::UniquePtrVector<Expr> args_;
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
  InstanceOfExpr(Expr* lhs, lexer::Token instanceof, Type* type)
      : lhs_(lhs), instanceof_(instanceof), type_(type) {}

  ACCEPT_VISITOR(InstanceOfExpr, Expr);

  REF_GETTER(Expr, Lhs, *lhs_);
  VAL_GETTER(lexer::Token, InstanceOf, instanceof_);
  REF_GETTER(Type, GetType, *type_);

 private:
  DISALLOW_COPY_AND_ASSIGN(InstanceOfExpr);

  unique_ptr<Expr> lhs_;
  lexer::Token instanceof_;
  unique_ptr<Type> type_;
};

class ParenExpr : public Expr {
 public:
  ParenExpr(Expr* nested) : nested_(nested) { assert(nested_ != nullptr); }

  ACCEPT_VISITOR(ParenExpr, Expr);

  REF_GETTER(Expr, Nested, *nested_);

 private:
  unique_ptr<Expr> nested_;
};

class BinExpr : public Expr {
 public:
  BinExpr(Expr* lhs, lexer::Token op, Expr* rhs)
      : op_(op), lhs_(lhs), rhs_(rhs) {
    assert(lhs != nullptr);
    assert(op.TypeInfo().IsBinOp());
    assert(rhs != nullptr);
  }

  ACCEPT_VISITOR(BinExpr, Expr);

  VAL_GETTER(lexer::Token, Op, op_);
  REF_GETTER(Expr, Lhs, *lhs_);
  REF_GETTER(Expr, Rhs, *rhs_);

 private:
  lexer::Token op_;
  unique_ptr<Expr> lhs_;
  unique_ptr<Expr> rhs_;
};

class UnaryExpr : public Expr {
 public:
  UnaryExpr(lexer::Token op, Expr* rhs) : op_(op), rhs_(rhs) {
    assert(op.TypeInfo().IsUnaryOp());
    assert(rhs != nullptr);
  }

  ACCEPT_VISITOR(UnaryExpr, Expr);

  VAL_GETTER(lexer::Token, Op, op_);
  REF_GETTER(Expr, Rhs, *rhs_);

 private:
  lexer::Token op_;
  unique_ptr<Expr> rhs_;
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
  ArrayIndexExpr(Expr* base, Expr* index) : base_(base), index_(index) {}

  ACCEPT_VISITOR(ArrayIndexExpr, Expr);

  REF_GETTER(Expr, Base, *base_);
  REF_GETTER(Expr, Index, *index_);

 private:
  unique_ptr<Expr> base_;
  unique_ptr<Expr> index_;
};

class FieldDerefExpr : public Expr {
 public:
  FieldDerefExpr(Expr* base, const string& fieldname, lexer::Token token)
      : base_(base), fieldname_(fieldname), token_(token) {}

  ACCEPT_VISITOR(FieldDerefExpr, Expr);

  REF_GETTER(Expr, Base, *base_);
  REF_GETTER(string, FieldName, fieldname_);
  REF_GETTER(lexer::Token, GetToken, token_);

 private:
  unique_ptr<Expr> base_;
  string fieldname_;
  lexer::Token token_;
};

class CallExpr : public Expr {
 public:
  CallExpr(Expr* base, lexer::Token lparen, ArgumentList&& args)
      : base_(base), lparen_(lparen), args_(std::forward<ArgumentList>(args)) {}

  ACCEPT_VISITOR(CallExpr, Expr);

  REF_GETTER(Expr, Base, *base_);
  VAL_GETTER(lexer::Token, Lparen, lparen_);
  REF_GETTER(ArgumentList, Args, args_);

 private:
  unique_ptr<Expr> base_;
  lexer::Token lparen_;
  ArgumentList args_;
};

class CastExpr : public Expr {
 public:
  CastExpr(Type* type, Expr* expr) : type_(type), expr_(expr) {}

  ACCEPT_VISITOR(CastExpr, Expr);

  REF_GETTER(Type, GetType, *type_);
  REF_GETTER(Expr, GetExpr, *expr_);

 private:
  DISALLOW_COPY_AND_ASSIGN(CastExpr);

  unique_ptr<Type> type_;
  unique_ptr<Expr> expr_;
};

class NewClassExpr : public Expr {
 public:
  NewClassExpr(lexer::Token newTok, Type* type, ArgumentList&& args)
      : newTok_(newTok), type_(type), args_(std::forward<ArgumentList>(args)) {}

  ACCEPT_VISITOR(NewClassExpr, Expr);

  VAL_GETTER(lexer::Token, NewToken, newTok_);
  REF_GETTER(Type, GetType, *type_);
  REF_GETTER(ArgumentList, Args, args_);

 private:
  DISALLOW_COPY_AND_ASSIGN(NewClassExpr);

  lexer::Token newTok_;
  unique_ptr<Type> type_;
  ArgumentList args_;
};

class NewArrayExpr : public Expr {
 public:
  NewArrayExpr(Type* type, Expr* expr) : type_(type), expr_(expr) {}

  ACCEPT_VISITOR(NewArrayExpr, Expr);

  REF_GETTER(Type, GetType, *type_);
  VAL_GETTER(const Expr*, GetExpr, expr_.get());

 private:
  DISALLOW_COPY_AND_ASSIGN(NewArrayExpr);

  unique_ptr<Type> type_;
  unique_ptr<Expr> expr_; // Can be nullptr.
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
  LocalDeclStmt(Type* type, lexer::Token ident, Expr* expr)
      : type_(type), ident_(ident), expr_(expr) {}

  ACCEPT_VISITOR(LocalDeclStmt, Stmt);

  REF_GETTER(Type, GetType, *type_);
  VAL_GETTER(lexer::Token, Ident, ident_);
  REF_GETTER(Expr, GetExpr, *expr_);

  // TODO: get the identifier as a string.

 private:
  unique_ptr<Type> type_;
  lexer::Token ident_;
  unique_ptr<Expr> expr_;
};

class ReturnStmt : public Stmt {
 public:
  ReturnStmt(Expr* expr) : expr_(expr) {}

  ACCEPT_VISITOR(ReturnStmt, Stmt);

  VAL_GETTER(const Expr*, GetExpr, expr_.get());

 private:
  unique_ptr<Expr> expr_; // Can be nullptr.
};

class ExprStmt : public Stmt {
 public:
  ExprStmt(Expr* expr) : expr_(expr) {}

  ACCEPT_VISITOR(ExprStmt, Stmt);

  REF_GETTER(Expr, GetExpr, *expr_);

 private:
  unique_ptr<Expr> expr_;
};

class BlockStmt : public Stmt {
 public:
  BlockStmt(base::UniquePtrVector<Stmt>&& stmts)
      : stmts_(std::forward<base::UniquePtrVector<Stmt>>(stmts)) {}

  ACCEPT_VISITOR(BlockStmt, Stmt);

  REF_GETTER(base::UniquePtrVector<Stmt>, Stmts, stmts_);

 private:
  DISALLOW_COPY_AND_ASSIGN(BlockStmt);
  base::UniquePtrVector<Stmt> stmts_;
};

class IfStmt : public Stmt {
 public:
  IfStmt(Expr* cond, Stmt* trueBody, Stmt* falseBody)
      : cond_(cond), trueBody_(trueBody), falseBody_(falseBody) {}

  ACCEPT_VISITOR(IfStmt, Stmt);

  REF_GETTER(Expr, Cond, *cond_);
  REF_GETTER(Stmt, TrueBody, *trueBody_);
  REF_GETTER(Stmt, FalseBody, *falseBody_);

 private:
  DISALLOW_COPY_AND_ASSIGN(IfStmt);

  unique_ptr<Expr> cond_;
  unique_ptr<Stmt> trueBody_;
  unique_ptr<Stmt> falseBody_;
};

class ForStmt : public Stmt {
 public:
  ForStmt(Stmt* init, Expr* cond, Expr* update, Stmt* body)
      : init_(init), cond_(cond), update_(update), body_(body) {}

  ACCEPT_VISITOR(ForStmt, Stmt);

  REF_GETTER(Stmt, Init, *init_);
  VAL_GETTER(const Expr*, Cond, cond_.get());
  VAL_GETTER(const Expr*, Update, update_.get());
  REF_GETTER(Stmt, Body, *body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ForStmt);

  unique_ptr<Stmt> init_;
  unique_ptr<Expr> cond_;    // May be nullptr.
  unique_ptr<Expr> update_;  // May be nullptr.
  unique_ptr<Stmt> body_;
};

class WhileStmt : public Stmt {
 public:
  WhileStmt(Expr* cond, Stmt* body) : cond_(cond), body_(body) {}

  ACCEPT_VISITOR(WhileStmt, Stmt);

  REF_GETTER(Expr, Cond, *cond_);
  REF_GETTER(Stmt, Body, *body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(WhileStmt);

  unique_ptr<Expr> cond_;
  unique_ptr<Stmt> body_;
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
  Param(Type* type, lexer::Token ident) : type_(type), ident_(ident) {}

  ACCEPT_VISITOR(Param, Param);

  REF_GETTER(Type, GetType, *type_);
  VAL_GETTER(lexer::Token, Ident, ident_);

 private:
  DISALLOW_COPY_AND_ASSIGN(Param);

  unique_ptr<Type> type_;
  lexer::Token ident_;
};

class ParamList final {
 public:
  ParamList(base::UniquePtrVector<Param>&& params)
      : params_(std::forward<base::UniquePtrVector<Param>>(params)) {}
  ~ParamList() = default;
  ParamList(ParamList&&) = default;

  ACCEPT_VISITOR(ParamList, ParamList);

  REF_GETTER(base::UniquePtrVector<Param>, Params, params_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ParamList);

  base::UniquePtrVector<Param> params_;
};

class MemberDecl {
 public:
  MemberDecl(ModifierList&& mods, lexer::Token ident)
      : mods_(std::forward<ModifierList>(mods)), ident_(ident) {}
  virtual ~MemberDecl() = default;

  ACCEPT_VISITOR_ABSTRACT(MemberDecl);

  REF_GETTER(ModifierList, Mods, mods_);
  VAL_GETTER(lexer::Token, Ident, ident_);

 private:
  DISALLOW_COPY_AND_ASSIGN(MemberDecl);

  ModifierList mods_;
  lexer::Token ident_;
};

class ConstructorDecl : public MemberDecl {
 public:
  ConstructorDecl(ModifierList&& mods, lexer::Token ident, ParamList&& params,
                  Stmt* body)
      : MemberDecl(std::forward<ModifierList>(mods), ident),
        params_(std::forward<ParamList>(params)),
        body_(body) {}

  ACCEPT_VISITOR(ConstructorDecl, MemberDecl);

  REF_GETTER(ParamList, Params, params_);
  REF_GETTER(Stmt, Body, *body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ConstructorDecl);

  ModifierList mods_;
  ParamList params_;
  unique_ptr<Stmt> body_;
};

class FieldDecl : public MemberDecl {
 public:
  FieldDecl(ModifierList&& mods, Type* type, lexer::Token ident, Expr* val)
      : MemberDecl(std::forward<ModifierList>(mods), ident),
        type_(type),
        val_(val) {}

  ACCEPT_VISITOR(FieldDecl, MemberDecl);

  REF_GETTER(Type, GetType, *type_);
  VAL_GETTER(const Expr*, Val, val_.get());

 private:
  DISALLOW_COPY_AND_ASSIGN(FieldDecl);

  unique_ptr<Type> type_;
  unique_ptr<Expr> val_;  // Might be nullptr.
};

class MethodDecl : public MemberDecl {
 public:
  MethodDecl(ModifierList&& mods, Type* type, lexer::Token ident,
             ParamList&& params, Stmt* body)
      : MemberDecl(std::forward<ModifierList>(mods), ident),
        type_(type),
        params_(std::forward<ParamList>(params)),
        body_(body) {}

  ACCEPT_VISITOR(MethodDecl, MemberDecl);

  REF_GETTER(Type, GetType, *type_);
  REF_GETTER(ParamList, Params, params_);
  REF_GETTER(Stmt, Body, *body_);

 private:
  DISALLOW_COPY_AND_ASSIGN(MethodDecl);

  unique_ptr<Type> type_;
  ParamList params_;
  unique_ptr<Stmt> body_;
};

class TypeDecl {
 public:
  TypeDecl(ModifierList&& mods, const string& name, lexer::Token nameToken,
           base::UniquePtrVector<ReferenceType>&& interfaces,
           base::UniquePtrVector<MemberDecl>&& members)
      : mods_(std::forward<ModifierList>(mods)),
        name_(name),
        nameToken_(nameToken),
        interfaces_(
            std::forward<base::UniquePtrVector<ReferenceType>>(interfaces)),
        members_(std::forward<base::UniquePtrVector<MemberDecl>>(members)) {}
  virtual ~TypeDecl() = default;

  ACCEPT_VISITOR_ABSTRACT(TypeDecl);

  REF_GETTER(ModifierList, Mods, mods_);
  REF_GETTER(string, Name, name_);
  VAL_GETTER(lexer::Token, NameToken, nameToken_);
  REF_GETTER(base::UniquePtrVector<ReferenceType>, Interfaces, interfaces_);
  REF_GETTER(base::UniquePtrVector<MemberDecl>, Members, members_);

 private:
  DISALLOW_COPY_AND_ASSIGN(TypeDecl);

  ModifierList mods_;
  string name_;
  lexer::Token nameToken_;
  base::UniquePtrVector<ReferenceType> interfaces_;
  base::UniquePtrVector<MemberDecl> members_;
};

class ClassDecl : public TypeDecl {
 public:
  ClassDecl(ModifierList&& mods, const string& name, lexer::Token nameToken,
            base::UniquePtrVector<ReferenceType>&& interfaces,
            base::UniquePtrVector<MemberDecl>&& members, ReferenceType* super)
      : TypeDecl(std::forward<ModifierList>(mods), name, nameToken,
                 std::forward<base::UniquePtrVector<ReferenceType>>(interfaces),
                 std::forward<base::UniquePtrVector<MemberDecl>>(members)),
        super_(super) {}

  ACCEPT_VISITOR(ClassDecl, TypeDecl);

  VAL_GETTER(const ReferenceType*, Super, super_.get());

 private:
  DISALLOW_COPY_AND_ASSIGN(ClassDecl);

  unique_ptr<ReferenceType> super_;  // Might be nullptr.
};

class InterfaceDecl : public TypeDecl {
 public:
  InterfaceDecl(ModifierList&& mods, const string& name, lexer::Token nameToken,
                base::UniquePtrVector<ReferenceType>&& interfaces,
                base::UniquePtrVector<MemberDecl>&& members)
      : TypeDecl(std::forward<ModifierList>(mods), name, nameToken,
                 std::forward<base::UniquePtrVector<ReferenceType>>(interfaces),
                 std::forward<base::UniquePtrVector<MemberDecl>>(members)) {}

  ACCEPT_VISITOR(InterfaceDecl, TypeDecl);

 private:
  DISALLOW_COPY_AND_ASSIGN(InterfaceDecl);
};

class ImportDecl final {
 public:
  ImportDecl(const QualifiedName& name, bool isWildCard)
      : name_(name), isWildCard_(isWildCard) {}

  ACCEPT_VISITOR(ImportDecl, ImportDecl);

  REF_GETTER(QualifiedName, Name, name_);
  VAL_GETTER(bool, IsWildCard, isWildCard_);

 private:
  DISALLOW_COPY_AND_ASSIGN(ImportDecl);

  QualifiedName name_;
  bool isWildCard_;
};

class CompUnit final {
 public:
  CompUnit(QualifiedName* package, base::UniquePtrVector<ImportDecl>&& imports,
           base::UniquePtrVector<TypeDecl>&& types)
      : package_(package),
        imports_(std::forward<base::UniquePtrVector<ImportDecl>>(imports)),
        types_(std::forward<base::UniquePtrVector<TypeDecl>>(types)) {}

  ACCEPT_VISITOR(CompUnit, CompUnit);

  VAL_GETTER(const QualifiedName*, Package, package_.get());
  REF_GETTER(base::UniquePtrVector<ImportDecl>, Imports, imports_);
  REF_GETTER(base::UniquePtrVector<TypeDecl>, Types, types_);

 private:
  unique_ptr<QualifiedName> package_;  // Might be nullptr.
  base::UniquePtrVector<ImportDecl> imports_;
  base::UniquePtrVector<TypeDecl> types_;
};

class Program final {
 public:
  Program(base::UniquePtrVector<CompUnit>&& units)
      : units_(std::forward<base::UniquePtrVector<CompUnit>>(units)) {}

  ACCEPT_VISITOR(Program, Program);

  REF_GETTER(base::UniquePtrVector<CompUnit>, CompUnits, units_);

 private:
  DISALLOW_COPY_AND_ASSIGN(Program);

  base::UniquePtrVector<CompUnit> units_;
};

#undef ACCEPT_VISITOR
#undef REF_GETTER
#undef VAL_GETTER

unique_ptr<Program> Parse(const base::FileSet* fs,
                          const vector<vector<lexer::Token>>& tokens,
                          base::ErrorList* out);

}  // namespace parser

#endif
