#include <iostream>
#include <string>

#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "myelin/express.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/kernel/tensorflow.h"

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

  auto *x = flow.AddVariable("x", dt, {128});
  auto *y = flow.AddVariable("y", dt, {128});
  auto *z = flow.AddVariable("z", dt, {128});

  //int32 yval = FLAGS_n;
  //y->SetData(&yval, sizeof(yval));

  auto *op = flow.AddOperation(func, "expr", "Calculate", {x, y}, {z});
  op->SetAttr("expr", expression);

  Network network;
  CHECK(network.Compile(flow, library));
  Cell *cell = network.GetCell("test");
  cell->WriteCodeToFile("/tmp/expr.bin");
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

  //Test("@0=Sub(Add(%0,#1),#1)");
  //Test("@0=Div(Mul(Sub(Add(%0,#1),#1),#1),#1)");
  //Test("@0=Sub(Add(Mul(Div(%0,%1),%1),%1),%1)");
  //Test("@0=Max(Add(Mul(%0,#1),#1),#1)");
  //Test("@0=Relu(Add(!0,!1))");
  Test("@0=Mul(Tanh(!0),Sigmoid(!1))");

  return 0;
}

