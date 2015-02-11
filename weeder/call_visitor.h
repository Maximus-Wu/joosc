#ifndef WEEDER_CALL_VISITOR_H
#define WEEDER_CALL_VISITOR_H

#include "ast/recursive_visitor.h"
#include "base/errorlist.h"
#include "base/fileset.h"

namespace weeder {

// CallVisitor checks that the left-hand-side of a method call is one of
// NameExpr, or FieldDerefExpr.
class CallVisitor : public ast::RecursiveVisitor {
 public:
  CallVisitor(const base::FileSet* fs, base::ErrorList* errors)
      : fs_(fs), errors_(errors) {}

  REC_VISIT_DECL(CallExpr, expr);

 private:
  const base::FileSet* fs_;
  base::ErrorList* errors_;
};

}  // namespace weeder

#endif
