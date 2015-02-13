#ifndef AST_VISITOR2_H
#define AST_VISITOR2_H

#include "ast/ast_fwd.h"
#include "base/shared_ptr_vector.h"

namespace ast {

#define FOR_EACH_VISITABLE(code) \
  code(ArrayIndexExpr, Expr, expr) \
  code(BinExpr, Expr, expr) \
  code(CallExpr, Expr, expr) \
  code(CastExpr, Expr, expr) \
  code(FieldDerefExpr, Expr, expr) \
  code(BoolLitExpr, Expr, expr) \
  code(StringLitExpr, Expr, expr) \
  code(CharLitExpr, Expr, expr) \
  code(IntLitExpr, Expr, expr) \
  code(NullLitExpr, Expr, expr) \
  code(NameExpr, Expr, expr) \
  code(NewArrayExpr, Expr, expr) \
  code(NewClassExpr, Expr, expr) \
  code(ParenExpr, Expr, expr) \
  code(ThisExpr, Expr, expr) \
  code(UnaryExpr, Expr, expr) \
  code(InstanceOfExpr, Expr, expr) \
  code(BlockStmt, Stmt, stmt) \
  code(EmptyStmt, Stmt, stmt) \
  code(ExprStmt, Stmt, stmt) \
  code(LocalDeclStmt, Stmt, stmt) \
  code(ReturnStmt, Stmt, stmt) \
  code(IfStmt, Stmt, stmt) \
  code(ForStmt, Stmt, stmt) \
  code(WhileStmt, Stmt, stmt) \
  code(ParamList, ParamList, params) \
  code(Param, Param, param) \
  code(FieldDecl, MemberDecl, field) \
  code(MethodDecl, MemberDecl, meth) \
  code(ConstructorDecl, MemberDecl, cons) \
  code(ClassDecl, TypeDecl, decl) \
  code(InterfaceDecl, TypeDecl, decl) \
  code(CompUnit, CompUnit, unit) \
  code(Program, Program, prog)

class Visitor2 {
public:
#define _REWRITE_DECL(type, rettype, name) virtual const sptr<rettype> Rewrite##type(const type& name, const sptr<type> name##ptr);
  FOR_EACH_VISITABLE(_REWRITE_DECL)
#undef _REWRITE_DECL

protected:
  enum class VisitResult {
    PRUNE,
    SKIP,
    RECURSE,
  };

#define _VISIT_DECL(type, rettype, name) virtual VisitResult Visit##type(const type&) { return VisitResult::RECURSE; }
  FOR_EACH_VISITABLE(_VISIT_DECL)
#undef _VISIT_DECL

private:
  template <typename T>
  base::SharedPtrVector<T> AcceptMulti(const base::SharedPtrVector<T>& oldVec, bool* changed_out) {
    base::SharedPtrVector<T> newVec;
    *changed_out = false;
    for (int i = 0; i < oldVec.Size(); ++i) {
      sptr<T> oldVal = oldVec.At(i);
      sptr<T> newVal = oldVal->Accept2(this, oldVal);
      if (newVal == nullptr) {
        *changed_out = true;
        continue;
      }
      if (newVal != oldVal) {
        *changed_out = true;
      }
      newVec.Append(newVal);
    }
    return newVec;
  }
};

#undef FOR_EACH_VISITABLE

template <typename T>
const sptr<T> Visit(Visitor2* visitor, const sptr<T> t) {
  return t->Accept2(visitor, t);
}

#define VISIT_DECL2(type, var) VisitResult Visit##type(const ast::type& var) override
#define VISIT_DEFN2(cls, type, var) VisitResult cls::Visit##type(const ast::type& var)

#define REWRITE_DECL2(type, rettype, var, varptr) const sptr<ast::rettype> Rewrite##type(const ast::type& var, const sptr<ast::type> varptr) override
#define REWRITE_DEFN2(cls, type, rettype, var, varptr) const sptr<ast::rettype> cls::Rewrite##type(const ast::type& var, const sptr<ast::type> varptr)

} // namespace ast

#endif
