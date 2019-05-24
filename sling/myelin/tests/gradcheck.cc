#include <math.h>
#include <random>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/gradient.h"

using namespace sling;
using namespace sling::jit;
using namespace sling::myelin;

DEFINE_int32(dim, 16, "vector dimensions");
DEFINE_int32(n, 5, "");
DEFINE_int32(k, 3, "");
DEFINE_int32(m, 4, "");
DEFINE_int64(seed, 0, "random seed");

// Random generator.
std::mt19937_64 prng;

// Check if two values are equal within a tolerance.
bool IsClose(float a, float b, float atol = 1e-05, float rtol = 0.001) {
  return fabs(a - b) <= atol + rtol * fabs(b);
}

// Check if two tensors are element-wise equal within a tolerance.
bool AllClose(const TensorData &a, const TensorData &b,
              float atol = 1e-05, float rtol = 0.001) {
  int n = a.shape().elements();
  CHECK_EQ(b.shape().elements(), n);
  CHECK_EQ(a.type(), DT_FLOAT);
  CHECK_EQ(b.type(), DT_FLOAT);

  for (int i = 0; i < n; ++i) {
    float aval = a.nth<float>(i);
    float bval = b.nth<float>(i);
    if (!IsClose(aval, bval, rtol, atol)) return false;
  }

  return true;
}

// Fill tensor with random value.
void Fill(TensorData *data, float mean, float stddev) {
  std::uniform_real_distribution<float> rnd(mean, stddev);
  for (int i = 0; i < data->shape().elements(); ++i) {
    data->nth<float>(i) = rnd(prng);
  }
}

// Check gradients computed via small finite differences against analytical
// gradients.
bool CheckGrad(Flow *flow, Flow::Function *func, float eps = 1e-6) {
  Compiler compiler;

#if 0
  // Get inputs and outputs.
  std::vector<Flow::Variable *> in;
  std::vector<Flow::Variable *> out;
  for (auto *var : flow->vars()) {
    if (var->in()) in.push_back(var);
    if (var->out()) out.push_back(var);
  }
#endif

  // Construct analytical gradient function.
  Flow::Function *grad = Gradient(flow, func, *compiler.library());
  LOG(INFO) << "gradient " << grad->name;

#if 0
  // Construct difference quotient for computing the numerical gradient.
  // This is computing f(a)- f(b) / a-b for all outputs which is an
  // approximation for df/da if h=a-b is a small random pertubation vector.
  FlowBuilder tf(flow, "numgrad");
  auto *a = tf.Name(tf.Instance(func), "a");
  auto *b = tf.Name(tf.Instance(func), "b");
#endif

  // Compile network.
  Network net;
  compiler.Compile(flow, &net);
  Cell *forward = net.GetCell(func->name);
  //Cell *backward = net.GetCell(gradient);

  // Get inputs and outputs.
  std::vector<Tensor *> inputs;
  std::vector<Tensor *> outputs;
  std::vector<Tensor *> dinputs;
  std::vector<Tensor *> doutputs;
  for (Tensor *t : net.parameters()) {
    if (t->cell() == forward) {
      if (t->in()) {
        LOG(INFO) << "input " << t->name() << " grad " << t->Gradient()->name();
        inputs.push_back(t);
        doutputs.push_back(t->Gradient());
      }
      if (t->out()) {
        LOG(INFO) << "output " << t->name() << " grad " << t->Gradient()->name();
        outputs.push_back(t);
        dinputs.push_back(t->Gradient());
      }
    }
  }

  // Fill inputs with random values.
  Instance f(forward);
  f.Clear();
  for (Tensor *t : inputs) {
    auto input = f[t];
    Fill(&input, 0, 10);
  }

  // Compute forward function.
  f.Compute();
  LOG(INFO) << "forward:\n" << f.ToString();

  return true;
}

// Check binary function.
bool CheckBinary(const string &optype) {
  Flow flow;
  FlowBuilder tf(&flow, "func");
  auto *x = tf.Var("x", DT_FLOAT, {FLAGS_dim});
  auto *y = tf.Var("y", DT_FLOAT, {FLAGS_dim});
  auto *z = tf.Name(tf.Op(optype, {x, y}), "z");
  x->set_in()->set_unique();
  y->set_in()->set_unique();
  z->set_out();

  return CheckGrad(&flow, tf.func());
}

// Check unary function.
bool CheckUnary(const string &optype) {
  Flow flow;
  FlowBuilder tf(&flow, "func");
  auto *x = tf.Var("x", DT_FLOAT, {FLAGS_dim});
  auto *y = tf.Name(tf.Op(optype, {x}), "y");
  x->set_in()->set_unique();
  y->set_out();

  return CheckGrad(&flow, tf.func());
}

// Check matrix multiplication.
bool CheckMatMul() {
  Flow flow;
  FlowBuilder tf(&flow, "func");
  auto *x = tf.Var("x", DT_FLOAT, {FLAGS_m, FLAGS_k});
  auto *y = tf.Var("y", DT_FLOAT, {FLAGS_k, FLAGS_n});
  auto *z = tf.Name(tf.MatMul(x, y), "z");
  x->set_in()->set_unique();
  y->set_in()->set_unique();
  z->set_out();

  return CheckGrad(&flow, tf.func());
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  prng.seed(FLAGS_seed);

  //CheckBinary("Add");
  //CheckBinary("Sub");
  CheckMatMul();
  //CheckUnary("Square");

  return 0;
}

