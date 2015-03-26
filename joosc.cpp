#include "joosc.h"

#include <fstream>
#include <iostream>

#include "ast/ast.h"
#include "ast/print_visitor.h"
#include "backend/i386/writer.h"
#include "base/error.h"
#include "base/errorlist.h"
#include "base/fileset.h"
#include "lexer/lexer.h"
#include "opt/opt.h"
#include "parser/parser.h"
#include "types/types.h"
#include "weeder/weeder.h"

using std::cerr;
using std::cout;
using std::endl;
using std::ofstream;
using std::ostream;

using ast::PrintVisitor;
using ast::Program;
using base::ErrorList;
using base::FileSet;
using lexer::LexJoosFiles;
using lexer::StripSkippableTokens;
using lexer::Token;
using parser::Parse;
using types::TypecheckProgram;
using weeder::WeedProgram;

namespace {

bool PrintErrors(const ErrorList& errors, ostream* err, const FileSet* fs) {
  if (errors.Size() > 0) {
    errors.PrintTo(err, base::OutputOptions::kUserOutput, fs);
  }
  return errors.IsFatal();
}

}

sptr<const Program> CompilerFrontend(CompilerStage stage, const FileSet* fs, ErrorList* out) {
  // Lex files.
  vector<vector<Token>> tokens;
  LexJoosFiles(fs, &tokens, out);
  if (out->IsFatal() || stage == CompilerStage::LEX) {
    return nullptr;
  }

  // Strip out comments and whitespace.
  vector<vector<Token>> filtered_tokens;
  StripSkippableTokens(tokens, &filtered_tokens);

  // Look for unsupported tokens.
  FindUnsupportedTokens(tokens, out);
  if (out->IsFatal() || stage == CompilerStage::UNSUPPORTED_TOKS) {
    return nullptr;
  }

  // Parse.
  sptr<const Program> program = Parse(fs, filtered_tokens, out);
  if (out->IsFatal() || stage == CompilerStage::PARSE) {
    return program;
  }

  // Weed.
  program = WeedProgram(fs, program, out);
  if (out->IsFatal() || stage == CompilerStage::WEED) {
    return program;
  }

  // Type-checking.
  program = TypecheckProgram(program, out);
  if (out->IsFatal() || stage == CompilerStage::TYPE_CHECK) {
    return program;
  }

  // Add more implementation here.
  return program;
}

bool CompilerBackend(CompilerStage stage, sptr<const ast::Program> prog, const string& dir, std::ostream* err) {
  ir::Program ir_prog = ir::GenerateIR(prog);
  if (stage == CompilerStage::GEN_IR) {
    return true;
  }

  // Run through IR-level-optimizations.
  ir_prog = opt::Optimize(ir_prog);

  // TODO: have a more generic backend mechanism.
  bool success = true;
  for (const ir::CompUnit& comp_unit : ir_prog.units) {
    string fname = dir + "/" + comp_unit.filename;

    ofstream out(fname);
    if (!out) {
      // TODO: make error pretty.
      *err << "Could not open output file: " << fname << "\n";
      success = false;
      continue;
    }

    backend::i386::Writer writer;
    for (const ir::Stream& method_stream : comp_unit.streams) {
      // TODO: thread method names through ir generation, so we can emit nice
      // comments.

      writer.WriteFunc(method_stream, &out);
    }

    out << std::flush;
  }


  do {
    string fname = dir + "/start.s";
    ofstream out(fname);
    if (!out) {
      // TODO: make error pretty.
      *err << "Could not open output file: " << fname << "\n";
      success = false;
      break;
    }

    out << "extern _entry\n";
    out << "global _start\n";
    out << "_start:\n";
    out << "push ebp\n";
    out << "mov ebp, esp\n";
    out << "call _entry\n";
    out << "pop ebp\n";
    out << "mov ebx, eax\n";
    out << "mov eax, 1\n";
    out << "int 0x80\n";

  } while (0);

  return success;
}

bool CompilerMain(CompilerStage stage, const vector<string>& files, ostream*, ostream* err) {
  // Open files.
  FileSet* fs = nullptr;
  {
    ErrorList errors;
    FileSet::Builder builder;

    for (const auto& file : files) {
      builder.AddDiskFile(file);
    }

    if (!builder.Build(&fs, &errors)) {
      errors.PrintTo(&cerr, base::OutputOptions::kUserOutput, fs);
      return false;
    }
  }
  uptr<FileSet> fs_deleter(fs);
  if (stage == CompilerStage::OPEN_FILES) {
    return true;
  }

  ErrorList errors;
  sptr<const Program> program = CompilerFrontend(stage, fs, &errors);
  if (PrintErrors(errors, err, fs)) {
    return false;
  }
  if (stage == CompilerStage::TYPE_CHECK) {
    return true;
  }

  // TODO.
  return CompilerBackend(stage, program, "output", err);
}
