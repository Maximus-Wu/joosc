#ifndef WEEDER_ASSIGNMENT_VISITOR_H
#define WEEDER_ASSIGNMENT_VISITOR_H

#include "ast/visitor.h"
#include "base/errorlist.h"
#include "base/fileset.h"

namespace weeder {

// AssignmentVisitor checks that the left-hand-side of an assignment is one of
// NameExpr, FieldDerefExpr, or ArrayIndexExpr.
class AssignmentVisitor : public ast::Visitor {
 public:
  AssignmentVisitor(const base::FileSet* fs, base::ErrorList* errors)
      : fs_(fs), errors_(errors) {}

  VISIT_DECL(BinExpr, expr);

 private:
  const base::FileSet* fs_;
  base::ErrorList* errors_;
};

}  // namespace weeder

#endif
