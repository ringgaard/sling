// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SLING_MYELIN_BUILDER_H_
#define SLING_MYELIN_BUILDER_H_

#include <map>
#include <vector>

#include "sling/base/types.h"
#include "sling/myelin/flow.h"

namespace sling {
namespace myelin {

// A scope is used for defined a name space for variables and operations.
class Scope {
 public:
  Scope(Scope *parent, const string &name, bool relative = true);
  ~Scope();

 protected:
  // Return unique name for operation.
  string OpName(const string &op);

  // Return scope name prefix.
  const string prefix() const { return name_; }

 private:
  Scope *root_;     // root scope
  Scope *parent_;   // parent scope of this scope
  Scope *current_;  // current inner-most scope for root scope
  string name_;     // name prefix for scope

  // Next unused operation number for each operation type.
  std::map<string, int> opnum_;
};

// Flow builder utility for building flows from expressions, e.g.:
//   Flow flow;
//   FlowBuilder tf(&flow, "mnist");
//   auto *w = tf.Const(weights, DT_FLOAT, {784, 10});
//   auto *b = tf.Const(bias, DT_FLOAT, {10});
//   auto *x = tf.Var("x", DT_FLOAT, {1, 784});
//   auto *y = tf.Add(tf.MatMul(x, w), b);
class FlowBuilder : public Scope {
 public:
  // Flow typedefs.
  typedef Flow::Variable Variable;
  typedef Flow::Operation Operation;
  typedef Flow::Function Function;
  typedef std::vector<Variable *> Args;

  // Initialize builder for existing function.
  FlowBuilder(Flow *flow, Function *func)
      : Scope(nullptr, func->name),
        flow_(flow),
        func_(func) {}

  // Initialize builder for new function.
  FlowBuilder(Flow *flow, const string &name)
      : Scope(nullptr, name), flow_(flow) {
    func_ = flow->AddFunction(name);
  }

  // Add variable to flow.
  Variable *Var(const string &name, Type type, const Shape &shape);

  // Add learnable parameter variable to flow.
  Variable *Parameter(const string &name, Type type, const Shape &shape);

  // Initialize variable with random values. Returns the variable itself.
  Variable *RandomUniform(Variable *var);
  Variable *RandomNormal(Variable *var);
  Variable *RandomOrtho(Variable *var);

  // Add input variable to function.
  Variable *Placeholder(const string &name, Type type, const Shape &shape,
                        bool ref = false);

  // Change name of variable. Returns the variable itself.
  Variable *Name(Variable *var, const string &name);

  // Add operation to function and return output variable.
  Variable *Op(const string &op,
               const Args &args,
               Type type,
               const Shape &shape);

  // Add operation to function and return output variable. The output is
  // shaped using broadcast semantics.
  Variable *Op(const string &op, const Args &args);

  // Add operation with no output to function.
  Operation *RawOp(const string &op, const Args &args);

  // Mark variable as non-differentiable.
  Variable *NoGradient(Variable *x) {
    if (x->producer != nullptr) {
      x->producer->set(Flow::Operation::NOGRADIENT);
    }
    return x;
  }

  // Add constant to flow.
  Variable *Const(const void *data, Type type, const Shape &shape) {
    return flow_->AddConstant(OpName("const"), type, shape, data);
  }
  Variable *Const(float value) { return Const(&value, DT_FLOAT, {}); }
  Variable *Const(double value) { return Const(&value, DT_DOUBLE, {}); }
  Variable *Const(int value) { return Const(&value, DT_INT32, {}); }
  Variable *Const(double value, Type type);
  Variable *Const(std::vector<float> &value) {
    int size = value.size();
    return Const(value.data(), DT_FLOAT, {size});
  }
  Variable *Const(std::vector<int> &value) {
    int size = value.size();
    return Const(value.data(), DT_INT32, {size});
  }
  Variable *Const(const std::vector<int> &value) {
    int size = value.size();
    return Const(value.data(), DT_INT32, {size});
  }
  Variable *Const(const Shape &shape) { return Const(shape.dims()); }

  Variable *Zero(Type type = DT_FLOAT);
  Variable *One(Type type = DT_FLOAT);
  Variable *Two(Type type = DT_FLOAT);
  Variable *OneHot(Variable *index, int depth, Variable *value = nullptr);

  // Add instance reference to other function.
  Variable *Instance(Function *func);

  // Add reference to variable in external instance.
  Variable *Ref(Variable *instance, Variable *external);

  // Math functions.
  Variable *Add(Variable *x, Variable *y) { return Op("Add", {x, y}); }
  Variable *Sub(Variable *x, Variable *y) { return Op("Sub", {x, y}); }
  Variable *Mul(Variable *x, Variable *y) { return Op("Mul", {x, y}); }
  Variable *Div(Variable *x, Variable *y) { return Op("Div", {x, y}); }
  Variable *Minimum(Variable *x, Variable *y) { return Op("Minimum", {x, y}); }
  Variable *Maximum(Variable *x, Variable *y) { return Op("Maximum", {x, y}); }
  Variable *Neg(Variable *x) { return Op("Neg", {x}); }
  Variable *Square(Variable *x) { return Op("Square", {x}); }
  Variable *Sqrt(Variable *x) { return Op("Sqrt", {x}); }
  Variable *Rsqrt(Variable *x) { return Op("Rsqrt", {x}); }
  Variable *Reciprocal(Variable *x) { return Op("Reciprocal", {x}); }
  Variable *Abs(Variable *x) { return Op("Abs", {x}); }
  Variable *Sign(Variable *x) { return Op("Sign", {x}); }
  Variable *Log(Variable *x) { return Op("Log", {x}); }
  Variable *Exp(Variable *x) { return Op("Exp", {x}); }
  Variable *Pow(Variable *x, Variable *y) { return Op("Pow", {x, y}); }
  Variable *Erf(Variable *x) { return Op("Erf", {x}); }
  Variable *Sigmoid(Variable *x) { return Op("Sigmoid", {x}); }
  Variable *Relu(Variable *x) { return Op("Relu", {x}); }
  Variable *Identity(Variable *x) { return Op("Identity", {x}); }

  // Trigonometric functions.
  Variable *Cos(Variable *x) { return Op("Cos", {x}); }
  Variable *Sin(Variable *x) { return Op("Sin", {x}); }
  Variable *Tan(Variable *x) { return Op("Tan", {x}); }
  Variable *Cot(Variable *x) { return Op("Cot", {x}); }
  Variable *Sec(Variable *x) { return Op("Sec", {x}); }
  Variable *Csc(Variable *x) { return Op("Csc", {x}); }

  // Inverse trigonometric functions.
  Variable *Acos(Variable *x) { return Op("Acos", {x}); }
  Variable *Asin(Variable *x) { return Op("ASin", {x}); }
  Variable *Atan(Variable *x) { return Op("Atan", {x}); }
  Variable *Acot(Variable *x) { return Op("Acot", {x}); }
  Variable *Asec(Variable *x) { return Op("Asec", {x}); }
  Variable *Acsc(Variable *x) { return Op("Acsc", {x}); }

  // Hyperbolic functions.
  Variable *Cosh(Variable *x) { return Op("Cosh", {x}); }
  Variable *Sinh(Variable *x) { return Op("Sinh", {x}); }
  Variable *Tanh(Variable *x) { return Op("Tanh", {x}); }
  Variable *Coth(Variable *x) { return Op("Coth", {x}); }
  Variable *Sech(Variable *x) { return Op("Sech", {x}); }
  Variable *Csch(Variable *x) { return Op("Csch", {x}); }

  // Inverse hyperbolic functions.
  Variable *Acosh(Variable *x) { return Op("Acosh", {x}); }
  Variable *Asinh(Variable *x) { return Op("ASinh", {x}); }
  Variable *Atanh(Variable *x) { return Op("Atanh", {x}); }
  Variable *Acoth(Variable *x) { return Op("Acoth", {x}); }
  Variable *Asech(Variable *x) { return Op("Asech", {x}); }
  Variable *Acsch(Variable *x) { return Op("Acsch", {x}); }

  // Comparison.
  Variable *Equal(Variable *x, Variable *y) {
    return NoGradient(Op("Equal", {x, y}));
  }
  Variable *NotEqual(Variable *x, Variable *y) {
    return NoGradient(Op("NotEqual", {x, y}));
  }
  Variable *Less(Variable *x, Variable *y) {
    return NoGradient(Op("Less", {x, y}));
  }
  Variable *LessEqual(Variable *x, Variable *y) {
    return NoGradient(Op("LessEqual", {x, y}));
  }
  Variable *Greater(Variable *x, Variable *y) {
    return NoGradient(Op("Greater", {x, y}));
  }
  Variable *GreaterEqual(Variable *x, Variable *y) {
    return NoGradient(Op("GreaterEqual", {x, y}));
  }

  Variable *IsZero(Variable *x) { return Equal(x, Zero(x->type)); }
  Variable *IsPositive(Variable *x) { return Greater(x, Zero(x->type)); }
  Variable *IsNegative(Variable *x) { return Less(x, Zero(x->type)); }

  // Logic operators.
  Variable *And(Variable *x, Variable *y) {
    return NoGradient(Op("And", {x, y}));
  }
  Variable *Or(Variable *x, Variable *y) {
    return NoGradient(Op("Or", {x, y}));
  }
  Variable *Xor(Variable *x, Variable *y) {
    return NoGradient(Op("Xor", {x, y}));
  }
  Variable *AndNot(Variable *x, Variable *y) {
    return NoGradient(Op("AndNot", {x, y}));
  }
  Variable *Not(Variable *x) {
    return NoGradient(Op("Not", {x}));
  }

  Variable *Cond(Variable *cond, Variable *x, Variable *y) {
    return Op("Cond", {cond, x, y});
  }
  Variable *Select(Variable *cond, Variable *x) {
    return Op("Select", {cond, x});
  }

  // Matrix multiplication.
  Variable *MatMul(Variable *x, Variable *y);

  // Matrix transpose.
  Variable *Transpose(Variable *x) {
    return Op("Transpose", {x}, x->type, x->shape.transposed());
  }

  // Tensor transpose.
  Variable *Transpose(Variable *x, const Shape &perm) {
    auto *t = Op("Transpose", {x}, x->type, x->shape.permuted(perm));
    t->producer->SetAttr("perm", perm);
    return t;
  }

  // Reductions.
  Variable *Reduce(const string &op, Variable *x,
                   int axis = -1, bool keepdims = false);

  Variable *Sum(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("Sum", x, axis, keepdims);
  }
  Variable *Product(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("Product", x, axis, keepdims);
  }
  Variable *Max(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("Max", x, axis, keepdims);
  }
  Variable *Min(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("Min", x, axis, keepdims);
  }
  Variable *All(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("All", x, axis, keepdims);
  }
  Variable *Any(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("Any", x, axis, keepdims);
  }
  Variable *Mean(Variable *x, int axis = -1, bool keepdims = false) {
    float size = x->shape.axisdim(axis);
    return Div(Sum(x, axis, keepdims), Const(size));
  }
  Variable *Count(Variable *p, Type type = DT_FLOAT) {
    return NoGradient(Op("Count", {p}, type, {}));
  }

  // Arg Min/Max.
  Variable *ArgM(const string &op, Variable *x, int axis, Variable **m) {
    Variable *argm;
    if (axis == -1) {
      argm = Op(op, {x}, DT_INT32, {});
    } else {
      argm = Reduce(op, x, axis);
      argm->type = DT_INT32;
    }
    if (m != nullptr) {
      string name = argm->producer->name + ":1";
      *m = flow_->AddVariable(name, x->type, argm->shape);
      argm->producer->AddOutput(*m);
    }
    return NoGradient(argm);
  }

  Variable *ArgMin(Variable *x, int axis = -1, Variable **min = nullptr) {
    return ArgM("ArgMin", x, axis, min);
  }
  Variable *ArgMax(Variable *x, int axis = -1, Variable **max = nullptr) {
    return ArgM("ArgMax", x, axis, max);
  }

  // Clip value.
  Variable *Clip(Variable *x, Variable *low, Variable *high) {
    return Minimum(Maximum(x, low), high);
  }
  Variable *Clip(Variable *x, float low, float high) {
    return Clip(x, Const(low), Const(high));
  }

  // Dot product between two vectors.
  Variable *DotProduct(Variable *x, Variable *y) {
    return Sum(Mul(x,y));
  }

  // L2 norm of vector.
  Variable *Norm(Variable *v, int axis = -1, bool keepdims = false) {
    return Sqrt(Sum(Square(v), axis, keepdims));
  }

  // Cosine similarity.
  Variable *CosSim(Variable *x, Variable *y) {
    return Div(DotProduct(x, y), Mul(Norm(x), Norm(y)));
  }

  // Cosine distance.
  Variable *CosDist(Variable *x, Variable *y) {
    return Sub(One(), CosSim(x, y));
  }

  // Normalize.
  Variable *Normalize(Variable *x, int axis = -1, bool keepdims = false) {
    return Mul(x, Reciprocal(Sum(x, axis, keepdims)));
  }

  // SoftMax.
  Variable *SoftMax(Variable *x, int axis = -1) {
    auto *softmax = Op("SoftMax", {x});
    if (axis != -1 ) softmax->producer->SetAttr("axis", axis);
    return softmax;
  }
  Variable *LogSoftMax(Variable *x) { return Log(SoftMax(x)); }
  Variable *LogSumExp(Variable *x, int axis = -1, bool keepdims = false) {
    return Reduce("LogSumExp", x, axis, keepdims);
  }

  // Shape.
  Variable *TensorShape(Variable *x) {
    return Op("Shape", {x}, DT_INT32, {x->rank()});
  }
  Variable *TensorSize(Variable *x) {
    return Op("Size", {x}, DT_INT32, {});
  }
  Variable *TensorRank(Variable *x) {
    return Op("Rank", {x}, DT_INT32, {});
  }

  Variable *Reshape(Variable *x, Variable *shape) {
    return Op("Reshape", {x, shape});
  }
  Variable *Reshape(Variable *x, const Shape &shape) {
    return Op("Reshape", {x, Const(shape)}, x->type, shape);
  }
  Variable *Squeeze(Variable *x, int axis) {
    return Reshape(x, x->shape.squeezed(axis));
  }
  Variable *ExpandDims(Variable *x, int axis) {
    return Reshape(x, x->shape.expanded(axis));
  }
  Variable *ReverseDims(Variable *x) {
    return Reshape(x, x->shape.transposed());
  }
  Variable *Broadcast(Variable *x, const Shape &shape) {
    return Op("Identity", {x}, x->type, shape);
  }
  Variable *Resize(Variable *x, const Shape &shape) {
    auto *y = Op("Resize", {x}, x->type, shape);
    y->producer->SetAttr("shape", shape);
    return y;
  }

  // Gather for embedding lookups.
  Variable *Gather(Variable *params,
                   Variable *indices,
                   Variable *oov = nullptr);

  Variable *PoolingGather(const string &op,
                          Variable *params,
                          Variable *indices,
                          int batch = 0);
  Variable *GatherSum(Variable *params, Variable *indices, int batch = 0) {
    return PoolingGather("GatherSum", params, indices, batch);
  }
  Variable *GatherAvg(Variable *params, Variable *indices, int batch = 0) {
    return PoolingGather("GatherAvg", params, indices, batch);
  }
  Variable *GatherMax(Variable *params, Variable *indices, int batch = 0) {
    return PoolingGather("GatherMax", params, indices, batch);
  }

  // Scatter for sparse embedding update.
  Variable *Scatter(Variable *f, Variable *v, Shape shape,
                    int batch = 0, bool pooled = false) {
    auto *r = Op("Scatter", {f, v}, v->type, shape);
    if (batch != 0) r->producer->SetAttr("batch", batch);
    if (pooled) r->producer->SetAttr("pooled", true);
    return r;
  }
  Variable *Scatter(Variable *f, Variable *v, Variable *oov, Shape shape,
                    int batch = 0, bool pooled = false) {
    auto *r = Op("Scatter", {f, v, oov}, v->type, shape);
    if (batch != 0) r->producer->SetAttr("batch", batch);
    if (pooled) r->producer->SetAttr("pooled", true);
    return r;
  }

  // Assignment.
  Operation *Assign(Variable *var, Variable *value) {
    return RawOp("Assign", {var, value});
  }

  Operation *AssignAdd(Variable *var, Variable *value) {
    return RawOp("Assign", {var, Add(var, value)});
  }

  Variable *Accumulate(Variable *var, Variable *value) {
    return Op("Assign", {var, value})->set_ref();
  }

  Operation *AssignAddScatter(Variable *M, Variable *f, Variable *v) {
    return RawOp("AssignAddScatter", {M, f, v});
  }

  // Bind variable to existing variable.
  void Bind(Variable *target, Variable *x) {
    auto *op = RawOp("Identity", {x});
    op->AddOutput(target);
  }

  // Concatenation.
  Variable *Concat(const std::vector<Variable *> &parts, int axis = 0);

  // Splitting.
  std::vector<Variable *> Split(Variable *v, int splits, int axis = 0);

  // Slicing.
  Variable *Slice(Variable *v, Variable *begin, const Shape &size) {
    return Op("Slice", {v, begin, Const(size)}, v->type, size);
  }
  Variable *Slice(Variable *v, const Shape &begin, const Shape &size) {
    return Op("Slice", {v, Const(begin), Const(size)}, v->type, size);
  }

  // Add input mapped through embedding.
  Variable *Feature(const string &name, int range, int size, int dim) {
    auto *M = Parameter(name + "_embeddings", DT_FLOAT, {range, dim});
    RandomNormal(M);
    if (size == 1) {
      auto *f = Placeholder(name, DT_INT32, {size, 1});
      return Gather(M, f);
    } else {
      auto *f = Placeholder(name, DT_INT32, {1, size, 1});
      return GatherSum(M, f, 1);
    }
  }

  // Feed-forward (FF) network.
  Variable *FNN(Variable *input,
                std::vector<int> layers,
                bool bias = false,
                const string &activation = "Relu");

  // Return function for builder.
  Function *func() const { return func_; }

  // Return flow for builder.
  Flow *flow() const { return flow_; }

 private:
  // Flow for builder.
  Flow *flow_;

  // Function for builder.
  Function *func_;
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_BUILDER_H_

