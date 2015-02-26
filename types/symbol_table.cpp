
#include "types/symbol_table.h"
#include "base/error.h"
#include "ast/extent.h"

namespace types {

using ast::LocalVarId;
using ast::kVarUnassigned;
using ast::kVarFirst;
using ast::Type;
using ast::TypeId;
using base::PosRange;
using base::Error;
using base::ErrorList;

SymbolTable SymbolTable::empty(nullptr, {}, {}, {});

SymbolTable::SymbolTable(const base::FileSet* fs, const TypeIdList& paramTids, const vector<string>& paramNames, const vector<PosRange>& ranges): fs_(fs), currently_declaring_(kVarUnassigned) {
  const u64 num_params = paramTids.Size();
  assert(num_params == paramNames.size());

  var_id_counter_ = kVarFirst;
  for (u64 i = 0; i < num_params; ++i) {
    VariableInfo varInfo = VariableInfo{
      var_id_counter_,
      paramTids.At(i),
      paramNames.at(i),
      ranges[i]};
    params_[paramNames.at(i)] = varInfo;
    ++var_id_counter_;
  }
}

LocalVarId SymbolTable::DeclareLocalStart(ast::TypeId tid, const string& name, PosRange nameRange, ErrorList* errors) {
  assert(currently_declaring_ == kVarUnassigned);

  // Check if already defined (not as a parameter).
  auto previousDef = cur_symbols_.find(name);
  if (previousDef != cur_symbols_.end()) {
    VariableInfo varInfo = previousDef->second;
    errors->Append(MakeDuplicateVarDeclError(name, nameRange, varInfo.posRange));
    return varInfo.vid;
  }

  // Add new variable to current scope.
  VariableInfo varInfo = VariableInfo{
    var_id_counter_,
    tid,
    name,
    nameRange};
  currently_declaring_ = var_id_counter_;
  ++var_id_counter_;
  cur_symbols_[name] = varInfo;
  cur_scope_.push_back(name);
  return varInfo.vid;
}

void SymbolTable::DeclareLocalEnd(ast::LocalVarId) {
  currently_declaring_ = kVarUnassigned;
}

pair<TypeId, LocalVarId> SymbolTable::ResolveLocal(const string& name, PosRange nameRange, ErrorList* errors) const {
  // First check local vars (non-params).
  const VariableInfo* var = nullptr;
  auto findVar = cur_symbols_.find(name);
  auto findParam = params_.find(name);
  if (findVar != cur_symbols_.end()) {
    var = &findVar->second;
  } else if (findParam != params_.end()) {
    var = &findParam->second;
  } else {
    errors->Append(MakeUndefinedReferenceError(name, nameRange));
    return make_pair(TypeId::kUnassigned, kVarUnassigned);
  }

  // Check if currently in this variable's initializer.
  if (currently_declaring_ == var->vid) {
    errors->Append(MakeVariableInitializerSelfReferenceError(nameRange));
    return make_pair(TypeId::kUnassigned, kVarUnassigned);
  }

  return make_pair(var->tid, var->vid);
}

void SymbolTable::EnterScope() {
  scopes_.push_back(cur_scope_);
  cur_scope_.clear();
}

void SymbolTable::LeaveScope() {
  for (string varName : cur_scope_) {
    auto found = cur_symbols_.find(varName);
    assert(found != cur_symbols_.end());
    cur_symbols_.erase(found);
  }
  cur_scope_ = scopes_.back();
  scopes_.pop_back();
}

Error* SymbolTable::MakeUndefinedReferenceError(string varName, PosRange varRange) const {
  stringstream ss;
  ss << "Undefined reference to \"";
  ss << varName;
  ss << "\"";
  return MakeSimplePosRangeError(fs_, varRange, "UndefinedReferenceError", ss.str());
}

Error* SymbolTable::MakeDuplicateVarDeclError(string varName, PosRange varRange, PosRange originalVarRange) const {
  // This lambda will outlive this instance of SymbolTable. Capture local copy of fs_.
  const base::FileSet* fs = fs_;
  return base::MakeError([=](std::ostream* out, const base::OutputOptions& opt) {
    if (opt.simple) {
      *out << "DuplicateVarDeclError(";
      *out << varRange;
      *out << ',';
      *out << originalVarRange;
      *out << ')';
      return;
    }

    stringstream msgstream;
    msgstream << "Local variable '" << varName << "' was declared multiple times.";

    PrintDiagnosticHeader(out, opt, fs, varRange, base::DiagnosticClass::ERROR, msgstream.str());
    PrintRangePtr(out, opt, fs, varRange);
    *out << '\n';
    PrintDiagnosticHeader(out, opt, fs, originalVarRange, base::DiagnosticClass::INFO, "Previously declared here.");
    PrintRangePtr(out, opt, fs, varRange);
  });
}

Error* SymbolTable::MakeVariableInitializerSelfReferenceError(PosRange pos) const {
  return MakeSimplePosRangeError(fs_, pos, "VariableInitializerSelfReferenceError", "You can't use a variable in its own initializer.");
}


} // namespace types
