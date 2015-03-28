#include "backend/i386/writer.h"

#include <iostream>

#include "backend/common/asm_writer.h"
#include "base/printf.h"
#include "ir/mem.h"
#include "ir/stream.h"

using std::ostream;

using ast::FieldId;
using ast::MethodId;
using ast::TypeId;
using backend::common::AsmWriter;
using backend::common::OffsetTable;
using base::Fprintf;
using base::Sprintf;
using ir::CompUnit;
using ir::LabelId;
using ir::MemId;
using ir::Op;
using ir::OpType;
using ir::SizeClass;
using ir::Stream;
using ir::kInvalidMemId;

#define EXPECT_NARGS(n) CHECK((end - begin) == n)

#define UNIMPLEMENTED_OP(type) \
  case OpType::type: \
    *out << "; Unimplemented op " << #type << '\n'; \
    *out << "ud2\n"; \
    break; \

namespace backend {
namespace i386 {

namespace {

using ArgIter = vector<u64>::const_iterator;

string Sized(SizeClass size, const string& b1, const string& b2, const string& b4) {
  switch(size) {
    case SizeClass::BOOL: // Fall through.
    case SizeClass::BYTE:
      return b1;
    case SizeClass::SHORT: // Fall through.
    case SizeClass::CHAR:
      return b2;
    case SizeClass::INT: // Fall through.
    case SizeClass::PTR:
      return b4;
    default:
      UNREACHABLE();
  }
}

// Convert our internal stack offset to an "[ebp-x]"-style string.
string StackOffset(i64 offset) {
  if (offset >= 0) {
    // We add 4 since we want our offsets to be 0-indexed, but [ebp-0] contains
    // the old value of ebp.
    return Sprintf("[ebp-%v]", offset + 4);
  }
  return Sprintf("[ebp+%v]", -offset);
}

struct FuncWriter final {
  FuncWriter(const OffsetTable& offsets, ostream* out) : offsets(offsets), w(out) {}

  void WritePrologue(const Stream& stream) {
    w.Col0("; Starting method.");

    if (stream.is_entry_point) {
      w.Col0("_entry:");
    }

    string label = Sprintf("_t%v_m%v", stream.tid, stream.mid);

    w.Col0("%v:\n", label);

    w.Col1("; Function prologue.");
    w.Col1("push ebp");
    w.Col1("mov ebp, esp\n");
  }

  void WriteEpilogue() {
    w.Col0(".epilogue:");
    w.Col1("pop ebp");
    w.Col1("ret");
    w.Col0("\n");
  }

  void SetupParams(const Stream& stream) {
    // [ebp-0] is the old ebp, [ebp-4] is the esp, so we start at [ebp-8].
    i64 param_offset = -8;
    for (size_t i = 0; i < stream.params.size(); ++i) {
      i64 cur = param_offset;
      param_offset -= 4;

      StackEntry entry = {stream.params.at(i), cur, i + 1};

      auto iter_pair = stack_map.insert({entry.id, entry});
      CHECK(iter_pair.second);
    }
  }

  void AllocHeap(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(2);

    MemId dst = begin[0];
    TypeId::Base tid = begin[1];

    const StackEntry& dst_e = stack_map.at(dst);

    CHECK(dst_e.size == SizeClass::PTR);

    u64 size = offsets.SizeOf({tid, 0});
    i64 stack_used = cur_offset;

    w.Col1("; t%v = new %v", dst, size);
    w.Col1("mov eax, %v", size);
    w.Col1("sub esp, %v", stack_used);
    w.Col1("call _joos_malloc");
    w.Col1("add esp, %v", stack_used);
    w.Col1("mov %v, eax", StackOffset(dst_e.offset));
  }

  void AllocMem(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(3);

    MemId memid = begin[0];
    SizeClass size = (SizeClass)begin[1];
    // bool is_immutable = begin[2] == 1;

    // TODO: We always allocate 4 bytes. Fixes here will also affect dealloc.

    i64 offset = cur_offset;
    cur_offset += 4;

    w.Col1("; %v refers to t%v.", StackOffset(offset), memid);

    StackEntry entry = {size, offset, memid};

    auto iter_pair = stack_map.insert({memid, entry});
    CHECK(iter_pair.second);

    stack.push_back(entry);
  }

  void DeallocMem(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(1);

    MemId memid = begin[0];

    CHECK(stack.size() > 0);
    auto entry = stack.back();

    CHECK(entry.id == memid);
    stack.pop_back();

    stack_map.erase(memid);

    cur_offset -= 4;
    CHECK(cur_offset >= 0);

    w.Col1("; t%v deallocated, used to be at %v.", memid, StackOffset(entry.offset));
  }

  void Label(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(1);

    LabelId lid = begin[0];

    w.Col0(".L%v:", lid);
  }

  void Const(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(3);

    MemId memid = begin[0];
    SizeClass size = (SizeClass)begin[1];
    u64 value = begin[2];

    const StackEntry& entry = stack_map.at(memid);
    CHECK(entry.size == size);

    string mov_size = Sized(size, "byte", "word", "dword");

    w.Col1("; t%v = %v.", memid, value);
    w.Col1("mov %v %v, %v", mov_size, StackOffset(entry.offset), value);
  }

  void MovImpl(ArgIter begin, ArgIter end, bool addr) {
    EXPECT_NARGS(2);

    MemId dst = begin[0];
    MemId src = begin[1];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& src_e = stack_map.at(src);

    if (addr) {
      CHECK(dst_e.size == SizeClass::PTR);
    } else {
      CHECK(dst_e.size == src_e.size);
    }

    string reg_size = addr ? "eax" : Sized(dst_e.size, "al", "ax", "eax");
    string src_prefix = addr ? "&" : "";
    string instr = addr ? "lea" : "mov";

    w.Col1("; t%v = %vt%v.", dst_e.id, src_prefix, src_e.id);
    w.Col1("%v %v, %v", instr, reg_size, StackOffset(src_e.offset));
    w.Col1("mov %v, %v", StackOffset(dst_e.offset), reg_size);
  }

  void Mov(ArgIter begin, ArgIter end) {
    MovImpl(begin, end, false);
  }

  void MovAddr(ArgIter begin, ArgIter end) {
    MovImpl(begin, end, true);
  }

  void MovToAddr(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(2);

    MemId dst = begin[0];
    MemId src = begin[1];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& src_e = stack_map.at(src);

    CHECK(dst_e.size == SizeClass::PTR);

    string src_reg = Sized(src_e.size, "bl", "bx", "ebx");

    w.Col1("; *t%v = t%v.", dst_e.id, src_e.id);
    w.Col1("mov %v, %v", src_reg, StackOffset(src_e.offset));
    w.Col1("mov eax, %v", StackOffset(dst_e.offset));
    w.Col1("mov [eax], %v", src_reg);
  }

  void FieldImpl(ArgIter begin, ArgIter end, bool addr) {
    // TODO: Handle PosRange and NPEs.
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId src = begin[1];
    FieldId fid = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& src_e = stack_map.at(src);

    if (addr) {
      CHECK(dst_e.size == SizeClass::PTR);
    }

    string reg_size = addr ? "eax" : Sized(dst_e.size, "al", "ax", "eax");
    string src_prefix = addr ? "&" : "";
    string instr = addr ? "lea" : "mov";

    u64 field_offset = offsets.OffsetOf(fid);

    w.Col1("; t%v = %vt%v.f%v.", dst_e.id, src_prefix, src_e.id, fid);
    w.Col1("mov ebx, %v", StackOffset(src_e.offset));
    w.Col1("%v %v, [ebx+%v]", instr, reg_size, field_offset);
    w.Col1("mov %v, %v", StackOffset(dst_e.offset), reg_size);
  }

  void Field(ArgIter begin, ArgIter end) {
    FieldImpl(begin, end, false);
  }

  void FieldAddr(ArgIter begin, ArgIter end) {
    FieldImpl(begin, end, true);
  }

  void AddSub(ArgIter begin, ArgIter end, bool add) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::INT);
    CHECK(lhs_e.size == SizeClass::INT);
    CHECK(rhs_e.size == SizeClass::INT);

    string op_str = add ? "+" : "-";
    string instr = add ? "add" : "sub";

    w.Col1("; t%v = t%v %v t%v.", dst_e.id, lhs_e.id, op_str, rhs_e.id);
    w.Col1("mov eax, %v", StackOffset(lhs_e.offset));
    w.Col1("%v eax, %v", instr, StackOffset(rhs_e.offset));
    w.Col1("mov %v, eax", StackOffset(dst_e.offset));
  }

  void Add(ArgIter begin, ArgIter end) {
    AddSub(begin, end, true);
  }

  void Sub(ArgIter begin, ArgIter end) {
    AddSub(begin, end, false);
  }

  void Mul(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::INT);
    CHECK(lhs_e.size == SizeClass::INT);
    CHECK(rhs_e.size == SizeClass::INT);

    w.Col1("; t%v = t%v * t%v.", dst_e.id, lhs_e.id, rhs_e.id);
    w.Col1("mov eax, %v", StackOffset(lhs_e.offset));
    w.Col1("mov ebx, %v", StackOffset(rhs_e.offset));
    w.Col1("imul ebx");
    w.Col1("mov %v, eax", StackOffset(dst_e.offset));
  }

  void DivMod(ArgIter begin, ArgIter end, bool div) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::INT);
    CHECK(lhs_e.size == SizeClass::INT);
    CHECK(rhs_e.size == SizeClass::INT);

    string op_str = div ? "/" : "%";
    string res_reg = div ? "eax" : "edx";

    w.Col1("; t%v = t%v %v t%v.", dst_e.id, lhs_e.id, op_str, rhs_e.id);
    w.Col1("mov eax, %v", StackOffset(lhs_e.offset));
    w.Col1("cdq"); // Sign-extend EAX through to EDX.
    w.Col1("mov ebx, %v", StackOffset(rhs_e.offset));
    w.Col1("idiv ebx");
    w.Col1("mov %v, %v", StackOffset(dst_e.offset), res_reg);
  }

  void Div(ArgIter begin, ArgIter end) {
    DivMod(begin, end, true);
  }

  void Mod(ArgIter begin, ArgIter end) {
    DivMod(begin, end, false);
  }

  void Jmp(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(1);

    LabelId lid = begin[0];

    w.Col1("jmp .L%v", lid);
  }

  void JmpIf(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(2);

    LabelId lid = begin[0];
    MemId cond = begin[1];

    const StackEntry& cond_e = stack_map.at(cond);

    CHECK(cond_e.size == SizeClass::BOOL);

    w.Col1("; Jumping if t%v.", cond);
    w.Col1("mov al, %v", StackOffset(cond_e.offset));
    w.Col1("test al, al");
    w.Col1("jnz .L%v", lid);
  }

  void RelImpl(ArgIter begin, ArgIter end, const string& relation, const string& instruction) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::BOOL);
    CHECK(lhs_e.size == SizeClass::INT);
    CHECK(rhs_e.size == SizeClass::INT);

    w.Col1("; t%v = (t%v %v t%v).", dst_e.id, lhs_e.id, relation, rhs_e.id);
    w.Col1("mov eax, %v", StackOffset(lhs_e.offset));
    w.Col1("cmp eax, %v", StackOffset(rhs_e.offset));
    w.Col1("%v %v", instruction, StackOffset(dst_e.offset));
  }

  void Lt(ArgIter begin, ArgIter end) {
    RelImpl(begin, end, "<", "setl");
  }

  void Leq(ArgIter begin, ArgIter end) {
    RelImpl(begin, end, "<=", "setle");
  }

  void Eq(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::BOOL);
    CHECK(lhs_e.size == rhs_e.size);
    CHECK(lhs_e.size == SizeClass::BOOL ||
          lhs_e.size == SizeClass::INT ||
          lhs_e.size == SizeClass::PTR);

    string reg_size = Sized(lhs_e.size, "al", "", "eax");

    w.Col1("; t%v = (t%v == t%v).", dst_e.id, lhs_e.id, rhs_e.id);
    w.Col1("mov %v, %v", reg_size, StackOffset(lhs_e.offset));
    w.Col1("cmp %v, %v", reg_size, StackOffset(rhs_e.offset));
    w.Col1("sete %v", StackOffset(dst_e.offset));
  }

  void Not(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(2);

    MemId dst = begin[0];
    MemId src = begin[1];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& src_e = stack_map.at(src);

    CHECK(dst_e.size == SizeClass::BOOL);
    CHECK(src_e.size == SizeClass::BOOL);

    w.Col1("; t%v = !t%v", dst_e.id, src_e.id);
    w.Col1("mov al, %v", StackOffset(src_e.offset));
    w.Col1("xor al, 1");
    w.Col1("mov %v, al", StackOffset(dst_e.offset));
  }

  void Neg(ArgIter begin, ArgIter end) {
    EXPECT_NARGS(2);

    MemId dst = begin[0];
    MemId src = begin[1];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& src_e = stack_map.at(src);

    CHECK(dst_e.size == SizeClass::INT);
    CHECK(src_e.size == SizeClass::INT);

    w.Col1("; t%v = -t%v", dst_e.id, src_e.id);
    w.Col1("mov eax, %v", StackOffset(src_e.offset));
    w.Col1("neg eax");
    w.Col1("mov %v, eax", StackOffset(dst_e.offset));
  }


  void BoolOpImpl(ArgIter begin, ArgIter end, const string& op_str, const string& instr) {
    EXPECT_NARGS(3);

    MemId dst = begin[0];
    MemId lhs = begin[1];
    MemId rhs = begin[2];

    const StackEntry& dst_e = stack_map.at(dst);
    const StackEntry& lhs_e = stack_map.at(lhs);
    const StackEntry& rhs_e = stack_map.at(rhs);

    CHECK(dst_e.size == SizeClass::BOOL);
    CHECK(lhs_e.size == SizeClass::BOOL);
    CHECK(rhs_e.size == SizeClass::BOOL);

    w.Col1("; t%v = t%v %v t%v.", dst_e.id, lhs_e.id, op_str, rhs_e.id);
    w.Col1("mov al, %v", StackOffset(lhs_e.offset));
    w.Col1("%v al, %v", instr, StackOffset(rhs_e.offset));
    w.Col1("mov %v, al", StackOffset(dst_e.offset));
  }

  void And(ArgIter begin, ArgIter end) {
    BoolOpImpl(begin, end, "&", "and");
  }

  void Or(ArgIter begin, ArgIter end) {
    BoolOpImpl(begin, end, "|", "or");
  }

  void Xor(ArgIter begin, ArgIter end) {
    BoolOpImpl(begin, end, "^", "xor");
  }

  void StaticCall(ArgIter begin, ArgIter end) {
    CHECK((end-begin) >= 4);

    MemId dst = begin[0];
    TypeId::Base tid = begin[1];
    MethodId mid = begin[2];
    u64 nargs = begin[3];

    CHECK(((u64)(end-begin) - 4) == nargs);


    i64 stack_used = cur_offset;

    w.Col1("; Pushing %v arguments onto stack for call.", nargs);

    // Push args onto stack in reverse order.
    for (ArgIter cur = end; cur != (begin + 4); --cur) {
      MemId arg = *(cur - 1);
      const StackEntry& arg_e = stack_map.at(arg);

      string reg = Sized(arg_e.size, "al", "ax", "eax");
      w.Col1("mov %v, %v", reg, StackOffset(arg_e.offset));
      w.Col1("mov %v, %v", StackOffset(stack_used), reg);

      stack_used += 4;
    }

    w.Col1("; Performing call.");

    w.Col1("sub esp, %v", stack_used);
    w.Col1("call _t%v_m%v", tid, mid);
    w.Col1("add esp, %v", stack_used);

    if (dst != kInvalidMemId) {
      const StackEntry& dst_e = stack_map.at(dst);
      string dst_reg = Sized(dst_e.size, "al", "ax", "eax");
      w.Col1("mov %v, %v", StackOffset(stack_map.at(dst).offset), dst_reg);
    }
  }

  void Ret(ArgIter begin, ArgIter end) {
    CHECK((end-begin) <= 1);

    if ((end - begin) == 1) {
      MemId ret = begin[0];
      const StackEntry& ret_e = stack_map.at(ret);

      string reg_size = Sized(ret_e.size, "al", "ax", "eax");

      w.Col1("; Return t%v.", ret_e.id);
      w.Col1("mov %v, %v", reg_size, StackOffset(ret_e.offset));
    } else {
      w.Col1("; Return.");
    }

    w.Col1("jmp .epilogue");
  }

 private:
  struct StackEntry {
    SizeClass size;
    i64 offset;
    MemId id;
  };

  map<MemId, StackEntry> stack_map;
  i64 cur_offset = 0;
  vector<StackEntry> stack;

  // TODO: do more optimal stack management for non-int-sized things.

  const OffsetTable& offsets;
  AsmWriter w;
};

} // namespace

void Writer::WriteCompUnit(const CompUnit& comp_unit, ostream* out) const {
  static string kMethodNameFmt = "_t%v_m%v";

  set<string> externs{"_joos_malloc"};
  set<string> globals;
  for (const Stream& method_stream : comp_unit.streams) {
    if (method_stream.is_entry_point) {
      globals.insert("_entry");
    }

    globals.insert(Sprintf(kMethodNameFmt, method_stream.tid, method_stream.mid));

    for (const Op& op : method_stream.ops) {
      // TODO: also will need things like static fields here.
      if (op.type == OpType::STATIC_CALL) {
        TypeId::Base tid = method_stream.args[op.begin+1];
        MethodId mid = method_stream.args[op.begin+2];

        externs.insert(Sprintf(kMethodNameFmt, tid, mid));
      }
    }
  }

  Fprintf(out, "; Predeclaring all necessary symbols.\n");
  for (const auto& global : globals) {
    // We cannot extern a symbol we are declaring in this file, so we remove
    // any local method calls from the externs map.
    externs.erase(global);

    Fprintf(out, "global %v\n", global);
  }
  for (const auto& ext : externs) {
    Fprintf(out, "extern %v\n", ext);
  }

  for (const ir::Stream& method_stream : comp_unit.streams) {
    WriteFunc(method_stream, out);
  }
}

void Writer::WriteFunc(const Stream& stream, ostream* out) const {
  FuncWriter writer{offsets_, out};

  writer.WritePrologue(stream);

  writer.SetupParams(stream);

  for (const Op& op : stream.ops) {
    ArgIter begin = stream.args.begin() + op.begin;
    ArgIter end = stream.args.begin() + op.end;

    switch (op.type) {
      case OpType::ALLOC_HEAP:
        writer.AllocHeap(begin, end);
        break;
      case OpType::ALLOC_MEM:
        writer.AllocMem(begin, end);
        break;
      case OpType::DEALLOC_MEM:
        writer.DeallocMem(begin, end);
        break;
      case OpType::LABEL:
        writer.Label(begin, end);
        break;
      case OpType::CONST:
        writer.Const(begin, end);
        break;
      case OpType::MOV:
        writer.Mov(begin, end);
        break;
      case OpType::MOV_ADDR:
        writer.MovAddr(begin, end);
        break;
      case OpType::MOV_TO_ADDR:
        writer.MovToAddr(begin, end);
        break;
      case OpType::FIELD:
        writer.Field(begin, end);
        break;
      case OpType::FIELD_ADDR:
        writer.FieldAddr(begin, end);
        break;
      case OpType::ADD:
        writer.Add(begin, end);
        break;
      case OpType::SUB:
        writer.Sub(begin, end);
        break;
      case OpType::MUL:
        writer.Mul(begin, end);
        break;
      case OpType::DIV:
        writer.Div(begin, end);
        break;
      case OpType::MOD:
        writer.Mod(begin, end);
        break;
      case OpType::JMP:
        writer.Jmp(begin, end);
        break;
      case OpType::JMP_IF:
        writer.JmpIf(begin, end);
        break;
      case OpType::LT:
        writer.Lt(begin, end);
        break;
      case OpType::LEQ:
        writer.Leq(begin, end);
        break;
      case OpType::EQ:
        writer.Eq(begin, end);
        break;
      case OpType::NOT:
        writer.Not(begin, end);
        break;
      case OpType::NEG:
        writer.Neg(begin, end);
        break;
      case OpType::AND:
        writer.And(begin, end);
        break;
      case OpType::OR:
        writer.Or(begin, end);
        break;
      case OpType::XOR:
        writer.Xor(begin, end);
        break;
      case OpType::STATIC_CALL:
        writer.StaticCall(begin, end);
        break;
      case OpType::RET:
        writer.Ret(begin, end);
        break;

      UNIMPLEMENTED_OP(SIGN_EXTEND);
      UNIMPLEMENTED_OP(ZERO_EXTEND);
      UNIMPLEMENTED_OP(TRUNCATE);

      default:
        UNREACHABLE();
    }
  }

  writer.WriteEpilogue();

}

void Writer::WriteMain(ostream* out) const {
  AsmWriter w(out);

  // Externs and globals.
  w.Col0("extern __malloc");
  w.Col0("extern _entry");
  w.Col0("global _joos_malloc");
  w.Col0("global _start");
  w.Col0("\n");

  // Entry point.
  w.Col0("_start:");
  w.Col1("; Call user code.");
  w.Col1("push ebp");
  w.Col1("mov ebp, esp");
  w.Col1("call _entry");
  w.Col1("pop ebp");
  w.Col1("; Call EXIT syscall.");
  w.Col1("mov ebx, eax");
  w.Col1("mov eax, 1");
  w.Col1("int 0x80");
  w.Col0("\n");

  // Zeroing malloc.
  w.Col0("; Custom malloc that zeroes memory.");
  w.Col0("_joos_malloc:");
  w.Col1("push eax"); // Save number of bytes.
  w.Col1("push ebp");
  w.Col1("mov ebp, esp");
  w.Col1("call __malloc");
  w.Col1("pop ebp");
  w.Col1("pop ebx");
  w.Col1("mov ecx, 0");
  w.Col0(".before:");
  w.Col1("cmp ecx, ebx");
  w.Col1("je .after");
  w.Col1("mov byte [eax + ecx], 0");
  w.Col1("inc ecx");
  w.Col1("jmp .before");
  w.Col0(".after:");
  w.Col1("ret");

}

} // namespace i386
} // namespace backend
