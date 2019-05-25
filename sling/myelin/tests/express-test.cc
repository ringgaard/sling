#include <iostream>
#include <string>
#include <unordered_map>

#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/myelin/express.h"

using namespace sling;
using namespace sling::myelin;

void Test(const string &str) {
  bool three_arg_ops = false;
  bool gpu = false;
  bool fma = false;
  int hoist = 0;
  bool live_ranges = false;

  Express::Model model;
  if (gpu) {
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;

    model.op_reg_reg = true;
    model.op_reg_imm = true;

    model.op_reg_reg_reg = true;
    model.op_reg_reg_imm = true;

    model.func_reg_reg = true;
    model.func_reg_imm = true;

    fma = true;
  } else  if (three_arg_ops) {
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

  if (fma) {
    model.fm_reg_reg_reg = true;
    model.fm_reg_reg_imm = true;
  }

  LOG(INFO) << "Expression: " << str;
  Express expr(&model);
  expr.Parse(str);

  bool raw = false;
  if (raw) {
    LOG(INFO) << "Raw:";
    for (auto *op : expr.ops()) {
      LOG(INFO) << "  " << op->result->AsString() << " := " << op->AsString();
    }
  }

  expr.Optimize(fma, hoist);

  int addr = 0;
  for (auto *op : expr.ops()) {
    if (expr.body() > 0 && op == expr.ops()[expr.body()]) {
      LOG(INFO) << "body:";
    }
    LOG(INFO) << "  " << addr << ": " << op->result->AsString()
              << (op->result->predicate ? "?" : "")
              << " := " << op->AsString();
    addr++;
  }

  if (live_ranges) {
    expr.ComputeLiveRanges();
    for (int i = 0; i < expr.vars().size(); ++i) {
      auto *v = expr.vars()[i];
      LOG(INFO) << v->AsString() << " live from " << v->first->index << " to " << v->last->index;
    }
  }

  Express instrs;
  bool success = expr.Generate(model, &instrs);
  if (!success) {
    LOG(ERROR) << "Code generation failed";
    return;
  }

  bool raw_instruction = false;
  if (raw_instruction) {
    LOG(INFO) << "Instructions: " << (success ? "OK" : "FAIL") << ", "
              << instrs.MaxActiveTemps() << " temps";
    for (auto *instr : instrs.ops()) {
      LOG(INFO) << "  " << instr->AsInstruction() << " ; "
                << instr->result->AsString() << "=" << instr->AsString();
    }
  }

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


  Test("@0=Tanh(%0)");
  Test("$2=Sigmoid(Add(%2,%3));@0=Add(Mul($2,Tanh(Add(%0,%1))),Mul(Sub(_1,$2),%4));@1=Tanh(@0)");
  Test("@0=Mul(Sigmoid(Add(%0,%1)),%2)");
  Test("!0=Exp(%0);@0=Id(!0)");
  Test("$2=Add(%4,%5);@0=Mul(Mul($2,%0),Mul(%3,Sub(_1,%3)));@1=Add(%6,Mul(Mul(%3,$2),Sub(_1,Square(%0))))");
  Test("$0=Neg(%0);@0=Mul($0,Div(_1,Max(%1,_1)));@1=Mul($0,Div(_1,Max(%2,_1)));@2=Mul($0,Div(_1,Max(%3,_1)));@3=Mul($0,Div(_1,Max(%4,_1)))"); return 0;
  Test("@0=Log(%0)");
  Test("@0=Id(Add(%0,Mul(%2,%1)))");
#endif
  //Test("@0=Exp(%0)");
  //Test("@0=Tanh(%0)");
  //Test("@0=Log(%0)");
  //Test("@0=Sigmoid(%0)");
  Test("@0=Id(#0)");

}

