#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/myelin/express.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/elf-linker.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/kernel/tensorflow.h"

DEFINE_int32(n, 100, "Constant argument");

DEFINE_bool(sse, true, "SSE support");
DEFINE_bool(sse2, true, "SSE2 support");
DEFINE_bool(sse3, true, "SSE3 support");
DEFINE_bool(sse41, true, "SSE 4.1 support");
DEFINE_bool(avx, true, "AVX support");
DEFINE_bool(avx2, true, "AVX2 support");
DEFINE_bool(fma3, true, "FMA3 support");

using namespace sling;
using namespace sling::myelin;

void Test(const string &expression) {
  Library library;
  RegisterTensorflowLibrary(&library);

  Express expr;
  expr.Parse(expression, true);

  Flow flow;
  auto *func = flow.AddFunction("test");

  Type dt = DT_FLOAT;
  Shape shape({128});

  std::vector<Flow::Variable *> inputs;
  std::vector<Flow::Variable *> outputs;
  char varno = 'a';
  for (auto *v : expr.vars()) {
    if (v->type == Express::INPUT) {
      auto *v = flow.AddVariable(string(1, varno++), dt, shape);
      inputs.push_back(v);
    } else if (v->type == Express::OUTPUT) {
      auto *v = flow.AddVariable(string(1, varno++), dt, shape);
      outputs.push_back(v);
    }
  }

  auto *op = flow.AddOperation(func, "expr", "Calculate", inputs, outputs);
  op->SetAttr("expr", expression);

  ElfLinker linker;
  Network network;
  network.set_linker(&linker);
  CHECK(network.Compile(flow, library));

  linker.Link();
  linker.Write("/tmp/expr.o");
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  if (!FLAGS_sse) jit::CPU::Disable(jit::SSE);
  if (!FLAGS_sse2) jit::CPU::Disable(jit::SSE2);
  if (!FLAGS_sse3) jit::CPU::Disable(jit::SSE3);
  if (!FLAGS_sse41) jit::CPU::Disable(jit::SSE4_1);
  if (!FLAGS_avx) jit::CPU::Disable(jit::AVX);
  if (!FLAGS_avx2) jit::CPU::Disable(jit::AVX2);
  if (!FLAGS_fma3) jit::CPU::Disable(jit::FMA3);

  //jit::CPU::Enable(jit::AVX512F);

  //Test("@0=Sub(Add(%0,#1),#1)");
  //Test("@0=Div(Mul(Sub(Add(%0,#1),#1),#1),#1)");
  //Test("@0=Sub(Add(Mul(Div(%0,%1),%1),%1),%1)");
  //Test("@0=Max(Add(Mul(%0,#1),#1),#1)");
  //Test("@0=Relu(Add(!0,!1))");
  //Test("@0=Mul(Tanh(!0),Sigmoid(!1))");

  //Test("@0=Sigmoid(%0)");
  //Test("@0=Log(%0)");
  //Test("@0=Cond(Not(And(CmpLt(%0,%1),%2)),%0,%1)");
  //Test("@0=Not(And(CmpLt(%0,%1),%2));@1=Cond(Not(@0),%0,_0)");
  Test("@0=Select(CmpGt(%0,_0),_1)");

  return 0;
}

