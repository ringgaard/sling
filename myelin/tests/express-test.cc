#include <iostream>
#include <string>

#include "base/init.h"
#include "base/logging.h"
#include "myelin/express.h"

using namespace sling;
using namespace sling::myelin;

void Test(const string &str) {
  bool three_arg_ops = true;
  bool fma = true;
  bool hoist = 0;

  Express::Model model;
  if (three_arg_ops) {
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;

    model.op_reg_reg = true;
    model.op_reg_imm = true;
    model.op_reg_mem = true;
    model.op_mem_reg = true;
    model.op_mem_imm = false;

    model.op_reg_reg_reg = true;
    model.op_reg_reg_imm = true;
    model.op_reg_reg_mem = true;
    model.op_mem_reg_reg = true;

    model.func_reg_reg = true;
    model.func_reg_imm = true;
    model.func_reg_mem = true;
    model.func_mem_reg = true;
    model.func_mem_imm = false;
  } else {
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg = true;
    model.op_reg_imm = true;
    model.op_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_imm = true;
    model.func_reg_mem = true;
    fma = false;
  }

  LOG(INFO) << "Expression: " << str;
  Express expr;
  expr.Parse(str, true);

  bool raw = false;
  if (raw) {
    LOG(INFO) << "Raw:";
    for (auto *op : expr.ops()) {
      LOG(INFO) << "  " << op->result->AsString() << " := " << op->AsString();
    }
  }

  expr.EliminateCommonSubexpressions();

  if (fma) {
    expr.FuseMulAdd();
    expr.FuseMulSub();
    model.fm_reg_reg_reg = true;
    model.fm_reg_reg_imm = true;
    model.fm_reg_reg_mem = true;
  }

  expr.CacheResults();
  if (hoist > 0) expr.HoistConstants(hoist);
  for (auto *op : expr.ops()) {
    if (expr.body() > 0 && op == expr.ops()[expr.body()]) {
      LOG(INFO) << "body:";
    }
    LOG(INFO) << "  " << op->result->AsString() << " := " << op->AsString();
  }

  Express instrs;
  bool success = expr.Rewrite(model, &instrs);
  if (!success) {
    LOG(ERROR) << "Rewrite failed";
    return;
  }
  instrs.ComputeLiveRanges();

  bool raw_instruction = false;
  if (raw_instruction) {
    LOG(INFO) << "Instructions: " << (success ? "OK" : "FAIL") << ", "
              << instrs.MaxActiveTemps() << " temps";
    for (auto *instr : instrs.ops()) {
      LOG(INFO) << "  " << instr->AsInstruction() << " ; "
                << instr->result->AsString() << "=" << instr->AsString();
    }
  }

  int regs = instrs.AllocateRegisters();
  LOG(INFO) << "Final: " << regs << " registers";
  for (auto *instr : instrs.ops()) {
    if (instrs.body() > 0 && instr == instrs.ops()[instrs.body()]) {
      LOG(INFO) << "body:";
    }
    if (!instr->nop()) LOG(INFO) << "  " << instr->AsInstruction();
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

#if 0
  Test("@0=Add(%0,%1)");
  Test("@0=Add(%2,Mul(%0,%1))");
  Test("$0=Max(%1,%2);$1=Min(%3,%4);@0=Mul($0,$1)");
  Test("@0=Add(Add(Add(Add(%4,%3),%2),%1),%0)");
  Test("$1=Mul(%0,%1);@0=Add($1,%2);@1=Add($1,%3)");
  Test("$1=Mul(%0,%1);@0=Add($1,Sub(%2,%3));@1=Add($1,Sub(%2,%3))");
  Test("$1=Mul(%0,%1);@0=Add($1,Sub(%2,%3));@1=Mul(Add($1,Sub(%2,%3)),%4)");
  Test("@1=Add(Mul(Add(%0,%1),Sub(%0,%1)),Mul(Add(%2,%3),Sub(%2,%3)))");
  Test("@1=Max(Add(Mul(Sub(%0,%1),Sub(%0,%2)),Mul(Sub(%1,%0),Sub(%2,%0))),"
       "Add(Mul(Sub(%1,%2),Sub(%2,%3)),Mul(Sub(%2,%1),Sub(%3,%2))))");
  Test("@0=Add(Mul(%0,%1),%2)");
  Test("@0=Add(%1,Mul(%1,%2))");
  Test("@0=Add(%0,%1);@1=Sub(Add(%0,%1),%2)");
  Test("@0=Id(%0)");
  Test("@0=Id(%0);@1=Id(%1)");
  Test("@0=Id(%0);@1=Id(@0)");
  Test("@0=Mul(Add(%0,%1),Add(%0,%1))");
  Test("@0=Add(Mul(%0,%1),Mul(%0,%1))");
  Test("@0=Add(Mul(%0,#1),#2);@1=Sub(#3,@0)");

  Test("@0=Add(%0,_13)");
  Test("@0=Log(%0)");
  Test("$0=Add(Mul(%0,#1),#2);@0=Mul(Log($0),Log(Sub(#3,$0)))");
  Test("@0=Mul(Log(Add(Mul(%0,#1),#2)),Log(Sub(#3,Add(Mul(%0,#1),#2))))");
  Test("@0=Exp(%0)");
  Test("@0=Sigmoid(%0)");
  Test("@0=Mul(Sigmoid(%0),Tanh(%0))");
  Test("$0=Add(Mul(%0,#1),#2);@0=Sub(Log($0),Log(Sub(#3,$0)))");

  Test("$2=Sigmoid(Add(%2,#3));"
       "@0=Add(Mul($2,Tanh(Add(%0,#1))),Mul(Sub(#4,$2),%5));"
       "@1=Tanh(@0)");

  Test("@0=Log(%0)");
  Test("@0=Mul(Add(%0,#1),#1)");
  Test("@0=Mul(Add(%0,_1),_1)");
  Test("@0=Add(Mul(%0,_1),_2)");
  Test("@0=Log(Sigmoid(%0))");
  Test("@0=Sub(_0,Log(Add(Exp(Sub(_0,%0)),_1)))");
  Test("@0=Add(Mul(Div(Add(Tanh(%9),#10),#11),Tanh(%8)),"
       "Mul(Sub(#6,Div(Add(Tanh(%3),#4),#5)),%7));"
       "@1=Mul(Tanh(@0),Div(Add(Tanh(%0),#1),#2))");

  Test("@0=Add(%0,#1)");
  Test("@0=Id(#0)");

  Test("@0=Neg(%1)");
  Test("@0=Abs(%1)");
  Test("@0=Relu(%1)");
  Test("@0=Softsign(%1)");
  Test("@0=Softplus(%1)");
  Test("@0=LogSigmoid(%1)");
  Test("@0=Reciprocal(%1)");
  Test("@0=Square(%1)");

  Test("@0=Mul(%0,#1)");

  Test("@0=Add(Mul(Div(Add(Tanh(%9),#10),#11),Tanh(%8)),Mul(Sub(#6,Div(Add(Tanh(%3),#4),#5)),%7));"
       "@1=Mul(Tanh(@0),Div(Add(Tanh(%0),#1),#2))");

  Test("$3=Tanh(%9);"
       "@0=Add(Mul(Div(Add(Tanh(%10),#11),#12),$3),Mul(Sub(#8,Tanh(%3)),%7));"
       "@1=Mul(Add(Mul(Div(Add(Tanh(%10),#13),#14),$3),Mul(Sub(#6,Div(Add(Tanh(%3),#4),#5)),%7)),Div(Add(Tanh(%0),#1),#2))");

#endif

  Test("@0=Tanh(%0)");

  return 0;
}
