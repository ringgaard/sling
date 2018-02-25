// Copyright 2018 Google Inc.
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

#include "sling/myelin/gradient.h"

#include <vector>

#include "sling/base/logging.h"

namespace sling {
namespace myelin {

// Return last part of name.
static string basename(const string &name) {
  int slash = name.rfind('/');
  if (slash == -1) return name;
  return name.substr(slash + 1);
}

Gradients::Gradients(Flow *flow,
                     Flow::Function *primal,
                     std::vector<Flow::Variable *> &vars)
    : Builder(flow, "gradients/" + primal->name) {
  // Add instance reference.
  instance_ = Name(Instance(primal), "primal");

  // Create adjoints.
  for (Flow::Variable *v : vars) {
    if (v->constant()) continue;
    auto *dv = Var("d_" + basename(v->name), v->type, v->shape);
    if (v->in()) dv->flags |= Flow::Variable::OUT;
    if (v->out()) dv->flags |= Flow::Variable::IN;
    dv->ref = v->ref;
    if (v->ref && v->producer != nullptr && !v->consumers.empty()) {
      // For recurrences that are both produced and consumed by the function
      // an additional accumulator is added to sum both contributions to the
      // gradient.
      auto *acc = Var("acc_" + basename(v->name), v->type, v->shape);
      adjoints_[v] = acc;
      terms_[acc] = dv;
    } else {
      adjoints_[v] = dv;
    }
  }
}

Flow::Variable *Gradients::GetReference(Flow::Variable *x) {
  Flow::Variable *&r = refs_[x];
  if (r == nullptr) {
    r = Name(Ref(instance_, x), basename(x->name));
    refs_[x] = r;
  }
  return r;
}

Flow::Function *Gradients::Finalize() {
  for (auto &it : adjoints_) {
    Flow::Variable *v = it.first;
    Flow::Variable *dv = it.second;
    Flow::Variable *terms = terms_[dv];
    if (terms != nullptr) {
      if (v->learnable()) {
        // Accumulate gradients for learnable variables.
        CHECK(dv->consumers.empty());
        AssignAdd(dv, terms);
        dv->flags |= Flow::Variable::OUT;
      } else {
        // Bind terms to adjoint.
        string name = OpName("Identity");
        flow()->AddOperation(func(), name, "Identity", {terms}, {dv});
      }
    }
  }

  // Return final gradient function.
  return func();
}

Flow::Function *Gradient(Flow *flow,
                         Flow::Function *func,
                         const Transformations &library) {
  // Get variables for gradients.
  std::vector<Flow::Variable *> vars;
  std::vector<Flow::Operation *> ops;
  flow->Order(func, &ops, &vars);

  // Derive gradients backwards from outputs to inputs (reverse mode).
  Gradients g(flow, func, vars);
  for (int i = ops.size() - 1; i >= 0; --i) {
    Flow::Operation *op = ops[i];
    auto f = library.gradients().find(op->type);
    if (f == library.gradients().end()) {
      LOG(FATAL) << "No gradient function for " << op->type;
    }
    auto *gradfunc = f->second;
    gradfunc(op, &g);
  }

  // Return gradient function.
  return g.Finalize();
}

}  // namespace myelin
}  // namespace sling

