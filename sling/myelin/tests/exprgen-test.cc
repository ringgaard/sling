#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/myelin/express.h"
#include "sling/myelin/compiler.h"

DEFINE_int32(n, 128, "Constant argument");
DEFINE_string(expr, "@0=Sigmoid(%0)", "Expression to compile");

using namespace sling;
using namespace sling::myelin;

void Test(const string &expression) {
  Compiler compiler;

  Express expr;
  expr.Parse(expression);

  Flow flow;
  auto *func = flow.AddFunction("test");

  Type dt = DT_FLOAT;
  Shape shape({FLAGS_n});

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

  Network network;
  compiler.Compile(&flow, &network);
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  Test(FLAGS_expr);

  return 0;
}

