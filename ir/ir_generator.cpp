#include "ir/ir_generator.h"

#include <algorithm>

#include "ast/ast.h"
#include "ast/visitor.h"
#include "ir/stream_builder.h"
#include "lexer/lexer.h"

using ast::VisitResult;

namespace ir {

SizeClass SizeOfTypeId(ast::TypeId tid) {
  if (tid == ast::TypeId::kInt) {
    return SizeClass::INT;
  } else if (tid == ast::TypeId::kBool) {
    return SizeClass::BOOL;
  }
  // TODO.
  return SizeClass::INT;
}

class MethodIRGenerator final : public ast::Visitor {
 public:
  MethodIRGenerator(Mem res, bool lvalue, StreamBuilder& builder, vector<ast::LocalVarId>& locals, map<ast::LocalVarId, Mem>& locals_map): res_(res), lvalue_(lvalue), builder_(builder), locals_(locals), locals_map_(locals_map)  {}

  MethodIRGenerator WithResultIn(Mem res, bool lvalue=false) {
    return MethodIRGenerator(res, lvalue, builder_, locals_, locals_map_);
  }

  MethodIRGenerator WithLocals(vector<ast::LocalVarId>& locals) {
    return MethodIRGenerator(res_, lvalue_, builder_, locals, locals_map_);
  }

  VISIT_DECL(MethodDecl, decl,) {
    // Get param sizes.
    auto params = decl.Params().Params();
    vector<SizeClass> param_sizes;
    for (int i = 0; i < params.Size(); ++i) {
      param_sizes.push_back(SizeOfTypeId(params.At(i)->GetType().GetTypeId()));
    }

    // Allocate params.
    vector<Mem> param_mems;
    builder_.AllocParams(param_sizes, &param_mems);

    // Add params to local map.
    for (int i = 0; i < params.Size(); ++i) {
      locals_map_.insert({params.At(i)->GetVarId(), param_mems.at(i)});
    }

    Visit(decl.BodyPtr());
    // Param Mems will be deallocated when map is deallocated.

    return VisitResult::SKIP;
  }

  VISIT_DECL(BlockStmt, stmt,) {
    vector<ast::LocalVarId> block_locals;
    MethodIRGenerator gen = WithLocals(block_locals);
    for (int i = 0; i < stmt.Stmts().Size(); ++i) {
      auto st = stmt.Stmts().At(i);
      gen.Visit(st);
    }

    // Have the Mems deallocated in order of allocation, so we maintain stack
    // invariant.
    std::reverse(block_locals.begin(), block_locals.end());
    for (auto vid : block_locals) {
      locals_map_.erase(vid);
    }

    return VisitResult::SKIP;
  }

  VISIT_DECL(BinExpr, expr,) {
    SizeClass size = SizeOfTypeId(expr.Lhs().GetTypeId());

    // Special code for short-circuiting boolean and or.
    if (expr.Op().type == lexer::AND) {
      Mem lhs = builder_.AllocLocal(size);
      Mem rhs = builder_.AllocTemp(size);
      WithResultIn(lhs).Visit(expr.LhsPtr());

      LabelId short_circuit = builder_.AllocLabel();
      {
        // Short circuit 'and' with a false result if lhs is false.
        Mem not_lhs = builder_.AllocLocal(SizeClass::BOOL);
        builder_.Not(not_lhs, lhs);
        builder_.JmpIf(short_circuit, not_lhs);
      }

      // Rhs code.
      WithResultIn(rhs).Visit(expr.RhsPtr());
      // Using lhs as answer.
      builder_.Mov(lhs, rhs);

      builder_.EmitLabel(short_circuit);
      builder_.Mov(res_, lhs);

      return VisitResult::SKIP;

    } else if (expr.Op().type == lexer::OR) {
      Mem lhs = builder_.AllocLocal(size);
      Mem rhs = builder_.AllocTemp(size);
      WithResultIn(lhs).Visit(expr.LhsPtr());

      // Short circuit 'or' with a true result if lhs is true.
      LabelId short_circuit = builder_.AllocLabel();
      builder_.JmpIf(short_circuit, lhs);

      // Rhs code.
      WithResultIn(rhs).Visit(expr.RhsPtr());
      builder_.Mov(lhs, rhs);

      // Short circuit code.
      builder_.EmitLabel(short_circuit);
      builder_.Mov(res_, lhs);

      return VisitResult::SKIP;
    }

    bool is_assg = false;
    if (expr.Op().type == lexer::ASSG) {
      is_assg = true;
    }

    Mem lhs = builder_.AllocTemp(is_assg ? SizeClass::PTR : size);
    WithResultIn(lhs, is_assg).Visit(expr.LhsPtr());
    Mem rhs = builder_.AllocTemp(size);
    WithResultIn(rhs).Visit(expr.RhsPtr());

    if (is_assg) {
      builder_.MovToAddr(lhs, rhs);

      // The result of an assignment expression is the rhs. We don't bother
      // with this if it's in a top-level context.
      if (res_.IsValid()) {
        builder_.Mov(res_, rhs);
      }
      return VisitResult::SKIP;
    }

#define C(fn) builder_.fn(res_, lhs, rhs); break;
    switch (expr.Op().type) {
      case lexer::ADD:  C(Add);
      case lexer::SUB:  C(Sub);
      case lexer::MUL:  C(Mul);
      case lexer::DIV:  C(Div);
      case lexer::MOD:  C(Mod);
      case lexer::EQ:   C(Eq);
      case lexer::NEQ:  C(Neq);
      case lexer::LT:   C(Lt);
      case lexer::LE:   C(Leq);
      case lexer::GT:   C(Gt);
      case lexer::GE:   C(Geq);
      case lexer::BAND: C(And);
      case lexer::BOR:  C(Or);
      case lexer::XOR:  C(Xor);
      default: break;
    }
#undef C

    return VisitResult::SKIP;
  }

  VISIT_DECL(IntLitExpr, expr,) {
    // TODO: Ensure no overflow.
    builder_.ConstInt32(res_, (i32)expr.Value());
    return VisitResult::SKIP;
  }

  VISIT_DECL(BoolLitExpr, expr,) {
    builder_.ConstBool(res_, expr.GetToken().type == lexer::K_TRUE);
    return VisitResult::SKIP;
  }

  VISIT_DECL(NameExpr, expr,) {
    auto i = locals_map_.find(expr.GetVarId());
    CHECK(i != locals_map_.end());
    Mem local = i->second;
    if (lvalue_) {
      builder_.MovAddr(res_, local);
    } else {
      builder_.Mov(res_, local);
    }
    return VisitResult::SKIP;
  }

  VISIT_DECL(ReturnStmt, stmt,) {
    if (stmt.GetExprPtr() == nullptr) {
      builder_.Ret();
      return VisitResult::SKIP;
    }

    Mem ret = builder_.AllocTemp(SizeOfTypeId(stmt.GetExprPtr()->GetTypeId()));
    WithResultIn(ret).Visit(stmt.GetExprPtr());
    builder_.Ret(ret);

    return VisitResult::SKIP;
  }

  VISIT_DECL(LocalDeclStmt, stmt,) {
    ast::TypeId tid = stmt.GetType().GetTypeId();
    Mem local = builder_.AllocLocal(SizeOfTypeId(tid));
    locals_.push_back(stmt.GetVarId());
    locals_map_.insert({stmt.GetVarId(), local});

    WithResultIn(local).Visit(stmt.GetExprPtr());

    return VisitResult::SKIP;
  }

  VISIT_DECL(IfStmt, stmt,) {
    Mem cond = builder_.AllocTemp(SizeClass::BOOL);
    WithResultIn(cond).Visit(stmt.CondPtr());

    LabelId begin_false = builder_.AllocLabel();
    LabelId after_if = builder_.AllocLabel();

    Mem not_cond = builder_.AllocTemp(SizeClass::BOOL);
    builder_.Not(not_cond, cond);
    builder_.JmpIf(begin_false, not_cond);

    // Emit true body code.
    Visit(stmt.TrueBodyPtr());
    builder_.Jmp(after_if);

    // Emit false body code.
    builder_.EmitLabel(begin_false);
    Visit(stmt.FalseBodyPtr());

    builder_.EmitLabel(after_if);

    return VisitResult::SKIP;
  }

  VISIT_DECL(WhileStmt, stmt,) {
    // Top of loop label.
    LabelId loop_begin = builder_.AllocLabel();
    LabelId loop_end = builder_.AllocLabel();
    builder_.EmitLabel(loop_begin);

    // Condition code.
    Mem cond = builder_.AllocTemp(SizeClass::BOOL);
    WithResultIn(cond).Visit(stmt.CondPtr());

    // Leave loop if condition is false.
    Mem not_cond = builder_.AllocTemp(SizeClass::BOOL);
    builder_.Not(not_cond, cond);
    builder_.JmpIf(loop_end, not_cond);

    // Loop body.
    Visit(stmt.BodyPtr());

    // Loop back to first label.
    builder_.Jmp(loop_begin);

    builder_.EmitLabel(loop_end);

    return VisitResult::SKIP;
  }

  VISIT_DECL(ForStmt, stmt,) {
    // Scope initializer variable.
    vector<ast::LocalVarId> loop_locals;
    {
      // Do initialization.
      MethodIRGenerator gen = WithLocals(loop_locals);
      gen.Visit(stmt.InitPtr());

      LabelId loop_begin = builder_.AllocLabel();
      LabelId loop_end = builder_.AllocLabel();

      builder_.EmitLabel(loop_begin);

      // Condition code.
      if (stmt.CondPtr() != nullptr) {
        Mem cond = builder_.AllocTemp(SizeClass::BOOL);
        gen.WithResultIn(cond).Visit(stmt.CondPtr());

        // Leave loop if condition is false.
        Mem not_cond = builder_.AllocTemp(SizeClass::BOOL);
        builder_.Not(not_cond, cond);
        builder_.JmpIf(loop_end, not_cond);
      }

      // Loop body.
      Visit(stmt.BodyPtr());

      // Loop update.
      if (stmt.UpdatePtr() != nullptr) {
        Visit(stmt.UpdatePtr());
      }

      // Loop back to first label.
      builder_.Jmp(loop_begin);

      builder_.EmitLabel(loop_end);
    }

    // Have the Mems deallocated in order of allocation, so we maintain stack
    // invariant.
    std::reverse(loop_locals.begin(), loop_locals.end());
    for (auto vid : loop_locals) {
      locals_map_.erase(vid);
    }
    return VisitResult::SKIP;
  }

  // Location result of the computation should be stored.
  Mem res_;
  bool lvalue_ = false;
  StreamBuilder& builder_;
  vector<ast::LocalVarId>& locals_;
  map<ast::LocalVarId, Mem>& locals_map_;
};

class ProgramIRGenerator final : public ast::Visitor {
 public:
  ProgramIRGenerator() {}
  VISIT_DECL(CompUnit, unit, ) {
    stringstream ss;
    ss << 'f' << unit.FileId() << ".s";
    current_unit_.filename = ss.str();

    for (int i = 0; i < unit.Types().Size(); ++i) {
      Visit(unit.Types().At(i));
    }
    prog.units.push_back(current_unit_);
    current_unit_ = ir::CompUnit();
    return VisitResult::SKIP;
  }

  VISIT_DECL(MethodDecl, decl, declptr) {
    if (decl.Name() != "test") {
      // TODO.
      return VisitResult::SKIP;
    }
    StreamBuilder builder;

    vector<ast::LocalVarId> empty_locals;
    map<ast::LocalVarId, Mem> locals_map;
    {
      Mem ret = builder.AllocDummy();

      MethodIRGenerator gen(ret, false, builder, empty_locals, locals_map);
      gen.Visit(declptr);
    }
    // Return mem must be deallocated before Build is called.

    current_unit_.streams.push_back(builder.Build(true, 2, 2));
    return VisitResult::SKIP;
  }

  ir::Program prog;

 private:
  ir::CompUnit current_unit_;
};

Program GenerateIR(sptr<const ast::Program> program) {
  ProgramIRGenerator gen;
  gen.Visit(program);
  return gen.prog;
}


} // namespace ir


