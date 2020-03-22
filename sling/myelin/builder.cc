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

#include "sling/myelin/builder.h"

namespace sling {
namespace myelin {

Scope::Scope(Scope *parent, const string &name, bool relative)
    : parent_(parent) {
  if (parent == nullptr) {
    // Top-level scope.
    root_ = current_ = this;
    name_ = name;
  } else {
    // Inner scope.
    root_ = parent_->root_;
    CHECK(root_->current_ == parent_);
    root_->current_ = current_ = this;
    if (relative) {
      name_ = parent_->name_ + "/" + name;
    } else {
      name_ = name;
    }
  }
}

Scope::~Scope() {
  CHECK(root_->current_ == this);
  root_->current_ = parent_;
}

string Scope::OpName(const string &op) {
  string name = current_->name_;
  name.push_back('/');
  name.append(op);
  int num = opnum_[op]++;
  if (num > 0) {
    name.push_back('_');
    name.append(std::to_string(num));
  }
  return name;
}

Flow::Variable *FlowBuilder::Var(const string &name, Type type,
                                 const Shape &shape) {
  return flow_->AddVariable(prefix() + "/" + name, type, shape);
}

Flow::Variable *FlowBuilder::Parameter(const string &name,
                                       Type type,
                                       const Shape &shape) {
  Variable *var = Var(name, type, shape)->set_learnable();
  return var;
}

Flow::Variable *FlowBuilder::RandomUniform(Variable *var) {
  var->init = Flow::Variable::INIT_UNIFORM;
  return var;
}

Flow::Variable *FlowBuilder::RandomNormal(Variable *var) {
  var->init = Flow::Variable::INIT_NORMAL;
  return var;
}

Flow::Variable *FlowBuilder::RandomOrtho(Variable *var) {
  var->init = Flow::Variable::INIT_ORTHO;
  return var;
}

Flow::Variable *FlowBuilder::Placeholder(const string &name,
                                         Type type,
                                         const Shape &shape,
                                         bool ref) {
  Variable *input = Var(name, type, shape)->set_in();
  if (ref) input->set_ref();
  return input;
}

Flow::Variable *FlowBuilder::Name(Variable *var, const string &name) {
  var->name = prefix() + "/" + name;
  return var;
}

Flow::Variable *FlowBuilder::Op(const string &op,
                                const Args &args,
                                Type type,
                                const Shape &shape) {
  string name = OpName(op);
  Variable *result = flow_->AddVariable(name + ":0", type, shape);
  flow_->AddOperation(func_, name, op, args, {result});
  return result;
}

Flow::Variable *FlowBuilder::Op(const string &op,
                                const Args &args) {
  // Use first argument for return type.
  Type type = args.empty() ? DT_INVALID : args[0]->type;

  // Determine output shape.
  Shape shape;
  if (args.size() == 1) {
    // Output shape is input shape for single-argument ops.
    shape = args[0]->shape;
  } else {
    // Determine output rank.
    int rank = 0;
    for (Flow::Variable *arg : args) {
      if (arg->rank() > rank) rank = arg->rank();
    }
    shape.fill(rank, 1);

    // Determine output shape based on broadcast semantics.
    for (Flow::Variable *arg : args) {
      int depth = rank - arg->rank();
      for (int d = 0; d < arg->rank(); ++d) {
        if (shape.dim(d + depth) < arg->dim(d)) {
          shape.set(d + depth, arg->dim(d));
        }
      }
    }
  }

  return Op(op, args, type, shape);
}

Flow::Operation *FlowBuilder::RawOp(const string &op, const Args &args) {
  string name = OpName(op);
  return flow_->AddOperation(func_, name, op, args, {});
}

Flow::Variable *FlowBuilder::Const(double value, Type type) {
  switch (type) {
    case DT_FLOAT: {
      float v = value;
      return Const(v);
    }
    case DT_DOUBLE: {
      double v = value;
      return Const(v);
    }
    case DT_INT64: {
      int64 v = value;
      return Const(&v, DT_INT64, {});
    }
    case DT_INT32: {
      int32 v = value;
      return Const(&v, DT_INT32, {});
    }
    case DT_INT16: {
      int16 v = value;
      return Const(&v, DT_INT16, {});
    }
    case DT_INT8: {
      int8 v = value;
      return Const(&v, DT_INT16, {});
    }
    default: LOG(FATAL) << "Constant type not supported";
  }
}

Flow::Variable *FlowBuilder::Zero(Type type) {
  switch (type) {
    case DT_FLOAT: return Const(0.0f);
    case DT_DOUBLE: return Const(0.0);
    default: return Const(nullptr, type, {});
  }
}

Flow::Variable *FlowBuilder::One(Type type) {
  switch (type) {
    case DT_FLOAT: return Const(1.0f);
    case DT_DOUBLE: return Const(1.0);
    case DT_INT32: return Const(1);
    default: LOG(FATAL) << "Constant type not supported";
  }
}

Flow::Variable *FlowBuilder::Two(Type type) {
  switch (type) {
    case DT_FLOAT: return Const(2.0f);
    case DT_DOUBLE: return Const(2.0);
    case DT_INT32: return Const(2);
    default: LOG(FATAL) << "Constant type not supported";
  }
}

Flow::Variable *FlowBuilder::OneHot(Variable *index,
                                    int depth,
                                    Variable *value) {
  Shape s = index->shape;
  s.add(depth);
  Variable *result;
  if (value != nullptr) {
    s.append(value->shape);
    result = Op("OneHot", {index, value}, value->type, s);
  } else {
    result = Op("OneHot", {index}, DT_FLOAT, s);
  }
  result->producer->SetAttr("depth", depth);
  return result;
}

Flow::Variable *FlowBuilder::Instance(Function *func) {
  Variable *instance = Var(func->name, DT_RESOURCE, {});
  instance->set_ref();
  return instance;
}

Flow::Variable *FlowBuilder::MatMul(Variable *x, Variable *y) {
  Variable *result = Op("MatMul", {x, y});
  if (x->rank() == 2 && y->rank() == 2) {
    result->shape = Shape({x->dim(0), y->dim(1)});
  }
  return result;
}

Flow::Variable *FlowBuilder::Ref(Variable *instance, Variable *external) {
  Variable *ref = Op("Reference", {instance});
  ref->type = external->type;
  ref->shape = external->shape;
  ref->set_ref();
  ref->producer->SetAttr("var", external->name);
  return ref;
}

Flow::Variable *FlowBuilder::Concat(const std::vector<Variable *> &parts,
                                    int axis) {
  CHECK(!parts.empty());
  Shape shape = parts[0]->shape;
  int n = parts.size();
  int width = 0;
  for (Variable *v : parts) {
    DCHECK_LT(axis, v->rank()) << v->name;
    width += v->shape.dim(axis);
  }
  shape.set(axis, width);
  std::vector<Variable *> args = parts;
  args.push_back(Const(axis));
  auto *concat = Op("Concat", args, parts[0]->type, shape);
  concat->producer->SetAttr("N", n);
  return concat;
}

std::vector<Flow::Variable *> FlowBuilder::Split(Variable *v, int splits,
                                                 int axis) {
  CHECK(v->dim(axis) % splits == 0)
    << "Cannot split " << v->shape.ToString() << " into " << splits
    << " parts along dimension " << axis;
  std::vector<Variable *> parts;
  Operation *op = RawOp("Split", {v, Const(splits), Const(axis)});
  Shape shape = v->shape;
  shape.set(axis, shape.dim(axis) / splits);
  for (int i = 0; i < splits; ++i) {
    string name = op->name + ":" + std::to_string(i);
    Variable *out = flow_->AddVariable(name, v->type, shape);
    op->AddOutput(out);
    parts.push_back(out);
  }
  return parts;
}

Flow::Variable *FlowBuilder::Gather(Variable *params,
                                    Variable *indices,
                                    Variable *oov) {
  std::vector<Variable *> args = {params, indices};
  if (oov != nullptr) args.push_back(oov);

  Shape s;
  if (indices->shape.scalar()) {
    s = params->shape.inside(params->rank() - 1);
  } else {
    int b = indices->shape.rank() - 1;
    int n = indices->dim(-1);
    s = indices->shape.outside(b) + params->shape.inside(n);
  }

  return Op("Gather", args, params->type, s);
}

Flow::Variable *FlowBuilder::PoolingGather(const string &op,
                                           Variable *params,
                                           Variable *indices,
                                           int batch) {
  int n = indices->shape.scalar() ? 1 : indices->dim(-1);
  auto *r = Op(op, {params, indices}, params->type, params->shape.inside(n));
  if (batch != 0) {
    r->producer->SetAttr("batch", batch);
    r->shape = indices->shape.outside(batch) + params->shape.inside(n);
  }
  return r;
}

Flow::Variable *FlowBuilder::Reduce(const string &op, Variable *x,
                                    int axis, bool keepdims) {
  auto *reduce = Op(op, {x}, x->type, x->shape.reduced(axis, keepdims));
  if (axis != -1) reduce->producer->SetAttr("axis", axis);
  if (keepdims) reduce->producer->SetAttr("keepdims", true);
  return reduce;
}

Flow::Variable *FlowBuilder::FNN(Variable *input,
                                 std::vector<int> layers,
                                 bool bias,
                                 const string &activation) {
  Variable *v = input;
  for (int l = 0; l < layers.size(); ++l) {
    // Get dimensions for next layer.
    Type type = v->type;
    int height = v->dim(1);
    int width = layers[l];

    // Add weight matrix.
    auto *W = Parameter("W" + std::to_string(l), type, {height, width});
    RandomNormal(W);
    v = MatMul(v, W);

    // Optionally add bias.
    if (bias) {
      auto *b = Parameter("b" + std::to_string(l), type, {1, width});
      v = Add(v, b);
    }

    // Add activation function between layers.
    if (l != layers.size() - 1) {
      v = Op(activation, {v});
    }
  }

  return v;
}

}  // namespace myelin
}  // namespace sling

